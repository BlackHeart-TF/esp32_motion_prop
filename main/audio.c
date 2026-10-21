#include "audio.h"

#include "config.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

static const char *TAG = "audio";

#define CHUNK_FRAMES 128
#define MAX_CLIPS    64
#define STORAGE_PATH "/storage"
#define SCREAM_DIR   STORAGE_PATH "/screams"
#define CREAK_DIR    STORAGE_PATH "/creaks"

#define AUDIO_CMD_CREAK  1
#define AUDIO_CMD_SCREAM 2

typedef struct {
    uint8_t kind;
    uint32_t gen;
} audio_cmd_t;

static i2s_chan_handle_t s_tx;
static int32_t s_buf[CHUNK_FRAMES * 2];
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static int s_mounted;
static int s_scream_count;
static char s_screams[MAX_CLIPS][64];
static int s_creak_count;
static char s_creaks[MAX_CLIPS][64];
static QueueHandle_t s_cmds;
static SemaphoreHandle_t s_ready;
static SemaphoreHandle_t s_scream_done;
static volatile uint32_t s_stop_gen;
static volatile int s_fade;
static volatile int s_playing;
static int s_task_up;

static void audio_task(void *arg);

#if AUDIO_BOOT_TONE
static void play_boot_tone(void);
#endif

static void write_frames(size_t frames)
{
    size_t bytes = frames * 2 * sizeof(int32_t);
    size_t written = 0;
    esp_err_t err = i2s_channel_write(s_tx, s_buf, bytes, &written, 2000);
    if (err != ESP_OK || written != bytes) {
        ESP_LOGW(TAG, "I2S write failed (%s), %u/%u bytes",
                 esp_err_to_name(err), (unsigned)written, (unsigned)bytes);
    }
}

static void write_silence(int milliseconds)
{
    int frames = (AUDIO_SAMPLE_RATE_HZ * milliseconds) / 1000;
    /* One full pass of every DMA buffer, so a leftover beep sample
     * cannot sit in the last buffer and repeat. */
    int min_frames = 8 * CHUNK_FRAMES;
    if (frames < min_frames) {
        frames = min_frames;
    }
    frames = ((frames + CHUNK_FRAMES - 1) / CHUNK_FRAMES) * CHUNK_FRAMES;
    memset(s_buf, 0, sizeof(s_buf));
    while (frames > 0) {
        write_frames(CHUNK_FRAMES);
        frames -= CHUNK_FRAMES;
    }
}

static int read_u32(FILE *file, uint32_t *out)
{
    uint8_t raw[4];
    if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) {
        return 0;
    }
    *out = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) |
           ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
    return 1;
}

static int wav_name(const char *name)
{
    size_t len = strlen(name);
    return len >= 5 && strcasecmp(name + len - 4, ".wav") == 0;
}

static void list_wavs(const char *dir_path, char names[][64], int *count, const char *label)
{
    *count = 0;
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        ESP_LOGW(TAG, "no %s folder on the storage drive", dir_path);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!wav_name(ent->d_name)) {
            continue;
        }
        size_t len = strlen(ent->d_name);
        if (len >= 64) {
            ESP_LOGW(TAG, "skipping %s, name is too long", ent->d_name);
            continue;
        }
        if (*count >= MAX_CLIPS) {
            ESP_LOGW(TAG, "only the first %d %s are used", MAX_CLIPS, label);
            break;
        }
        memcpy(names[*count], ent->d_name, len + 1);
        (*count)++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "%d %s in %s", *count, label, dir_path);
}

static void mount_storage(void)
{
    esp_vfs_fat_mount_config_t cfg = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    cfg.max_files = 4;
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(STORAGE_PATH, "storage", &cfg, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "storage mount failed (%s). Format the drive and put wavs in screams/ and creaks/",
                 esp_err_to_name(err));
        return;
    }
    s_mounted = 1;
    list_wavs(SCREAM_DIR, s_screams, &s_scream_count, "screams");
    list_wavs(CREAK_DIR, s_creaks, &s_creak_count, "creaks");
}

/* Leaves the file positioned at the PCM. Returns 0 when the clip is not playable. */
static int open_wav(FILE *file, const char *name, uint32_t *data_bytes)
{
    char id[4];
    uint32_t chunk = 0;
    if (fread(id, 1, 4, file) != 4 || memcmp(id, "RIFF", 4) != 0) {
        return 0;
    }
    if (!read_u32(file, &chunk)) {
        return 0;
    }
    if (fread(id, 1, 4, file) != 4 || memcmp(id, "WAVE", 4) != 0) {
        return 0;
    }

    int have_fmt = 0;
    while (fread(id, 1, 4, file) == 4) {
        if (!read_u32(file, &chunk)) {
            return 0;
        }
        if (memcmp(id, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (chunk < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt)) {
                return 0;
            }
            uint16_t format = (uint16_t)fmt[0] | ((uint16_t)fmt[1] << 8);
            uint16_t channels = (uint16_t)fmt[2] | ((uint16_t)fmt[3] << 8);
            uint32_t rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                            ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            uint16_t bits = (uint16_t)fmt[14] | ((uint16_t)fmt[15] << 8);
            if (format != 1 || channels != 1 || rate != AUDIO_SAMPLE_RATE_HZ || bits != 16) {
                ESP_LOGW(TAG, "%s must be %d Hz, 16-bit, mono PCM", name, AUDIO_SAMPLE_RATE_HZ);
                return 0;
            }
            if (chunk > 16 && fseek(file, (long)(chunk - 16), SEEK_CUR) != 0) {
                return 0;
            }
            if ((chunk & 1) && fseek(file, 1, SEEK_CUR) != 0) {
                return 0;
            }
            have_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            if (!have_fmt || chunk < sizeof(int16_t)) {
                return 0;
            }
            *data_bytes = chunk & ~1u;
            return 1;
        } else if (fseek(file, (long)(chunk + (chunk & 1)), SEEK_CUR) != 0) {
            return 0;
        }
    }
    return 0;
}

void audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = CHUNK_FRAMES;
    /* After the queue drains, send zeros instead of replaying the last buffer. */
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, NULL));

    /* 32-bit Philips slots (64 BCK clocks per sample). PCM5102 reads the
     * top of each slot, so 16-bit audio is shifted up 16 bits below.
     * MCLK is 512x the sample rate, which this DAC accepts on SCK. */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCK,
            .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_512;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    write_silence(60);
    mount_storage();

    i2s_chan_info_t info = {0};
    ESP_ERROR_CHECK(i2s_channel_get_info(s_tx, &info));
    ESP_LOGI(TAG, "PCM5102 I2S %d Hz, MCLK gpio %d (%u Hz), BCK %d, WS %d, DIN %d, BCLK %u Hz",
             AUDIO_SAMPLE_RATE_HZ, PIN_I2S_MCLK, (unsigned)info.mclk_hz,
             PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DOUT, (unsigned)info.bclk_hz);

    s_cmds = xQueueCreate(4, sizeof(audio_cmd_t));
    s_ready = xSemaphoreCreateBinary();
    s_scream_done = xSemaphoreCreateBinary();
    if (s_cmds == NULL || s_ready == NULL || s_scream_done == NULL) {
        ESP_LOGE(TAG, "audio queue create failed");
        return;
    }
    if (xTaskCreatePinnedToCore(audio_task, "audio", 8192, NULL, 8, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "audio task create failed");
        return;
    }
    s_task_up = 1;
    if (xSemaphoreTake(s_ready, pdMS_TO_TICKS(3000)) != pdTRUE) {
        ESP_LOGW(TAG, "audio task did not start");
    }
}

#if AUDIO_BOOT_TONE
static void play_boot_tone(void)
{
    const int period = AUDIO_SAMPLE_RATE_HZ / 880;
    int phase = 0;
    int frames = AUDIO_SAMPLE_RATE_HZ / 3;
    while (frames > 0) {
        int n = frames > CHUNK_FRAMES ? CHUNK_FRAMES : frames;
        memset(s_buf, 0, sizeof(s_buf));
        for (int i = 0; i < n; i++) {
            int16_t sample = (phase < period / 2) ? 18000 : -18000;
            if (++phase >= period) {
                phase = 0;
            }
            int32_t word = ((int32_t)sample) << 16;
            s_buf[i * 2] = word;
            s_buf[i * 2 + 1] = word;
        }
        write_frames(CHUNK_FRAMES);
        frames -= n;
    }
    write_silence(100);
}
#endif

static void cut_playback(void)
{
    esp_err_t err = i2s_channel_disable(s_tx);
    if (err == ESP_OK) {
        err = i2s_channel_enable(s_tx);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s restart failed (%s)", esp_err_to_name(err));
    }
}

#define FADE_MS 400

/* gen is the stop generation captured when this clip was requested.
 * audio_stop() bumps s_stop_gen and cuts. audio_fade() ramps this clip out. */
static void play_wav(FILE *file, const char *name, int loop, uint32_t gen)
{
    uint32_t data_bytes = 0;
    if (!open_wav(file, name, &data_bytes)) {
        ESP_LOGW(TAG, "%s is not a playable wav", name);
        return;
    }
    long data_pos = ftell(file);
    if (data_pos < 0) {
        return;
    }

    uint32_t ms = (uint32_t)((uint64_t)(data_bytes / sizeof(int16_t)) * 1000 / AUDIO_SAMPLE_RATE_HZ);
    ESP_LOGI(TAG, "playing %s (%u ms)", name, (unsigned)ms);

    int fade_frames = (AUDIO_SAMPLE_RATE_HZ * FADE_MS) / 1000;
    int gain_step = (32767 + fade_frames - 1) / fade_frames;
    int gain = 32767;
    int fading = 0;

    s_fade = 0;
    s_playing = 1;
    uint32_t left = data_bytes;
    while (gen == s_stop_gen) {
        if (s_fade) {
            fading = 1;
        }
        if (left < sizeof(int16_t)) {
            if (!loop) {
                break;
            }
            if (fseek(file, data_pos, SEEK_SET) != 0) {
                break;
            }
            left = data_bytes;
            continue;
        }
        size_t frames = left / sizeof(int16_t);
        if (frames > CHUNK_FRAMES) {
            frames = CHUNK_FRAMES;
        }
        int16_t pcm[CHUNK_FRAMES];
        size_t got = fread(pcm, sizeof(int16_t), frames, file);
        if (got == 0) {
            break;
        }
        if (gen != s_stop_gen) {
            break;
        }
        for (size_t i = 0; i < got; i++) {
            int32_t sample = ((int32_t)pcm[i]) << 16;
            if (fading) {
                sample = (int32_t)(((int64_t)sample * gain) >> 15);
                gain -= gain_step;
                if (gain < 0) {
                    gain = 0;
                }
            }
            s_buf[i * 2] = sample;
            s_buf[i * 2 + 1] = sample;
        }
        write_frames(got);
        left -= (uint32_t)(got * sizeof(int16_t));
        if (fading && gain == 0) {
            break;
        }
    }

    s_playing = 0;
    s_fade = 0;
    if (gen != s_stop_gen) {
        cut_playback();
    } else {
        write_silence(80);
    }
}

static void play_creak(uint32_t gen)
{
    if (!s_mounted || s_creak_count == 0) {
        ESP_LOGW(TAG, "no creaks on the storage drive");
        return;
    }

    const char *name = s_creaks[esp_random() % (uint32_t)s_creak_count];
    char path[96];
    snprintf(path, sizeof(path), CREAK_DIR "/%s", name);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "could not open %s", name);
        return;
    }
    ESP_LOGI(TAG, "creak %s", name);
    play_wav(file, name, 0, gen);
    fclose(file);
}

static void play_scream(uint32_t gen)
{
    if (!s_mounted || s_scream_count == 0) {
        ESP_LOGW(TAG, "no screams on the storage drive");
        return;
    }

    const char *name = s_screams[esp_random() % (uint32_t)s_scream_count];
    char path[96];
    snprintf(path, sizeof(path), SCREAM_DIR "/%s", name);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "could not open %s", name);
        return;
    }
    play_wav(file, name, 0, gen);
    fclose(file);
}

static void audio_task(void *arg)
{
    (void)arg;
#if AUDIO_BOOT_TONE
    play_boot_tone();
#endif
    xSemaphoreGive(s_ready);

    audio_cmd_t cmd;
    while (xQueueReceive(s_cmds, &cmd, portMAX_DELAY) == pdTRUE) {
        int stale = cmd.gen != s_stop_gen;
        if (!stale && cmd.kind == AUDIO_CMD_CREAK) {
            play_creak(cmd.gen);
        } else if (!stale && cmd.kind == AUDIO_CMD_SCREAM) {
            play_scream(cmd.gen);
        }
        if (cmd.kind == AUDIO_CMD_SCREAM) {
            xSemaphoreGive(s_scream_done);
        }
    }
}

static void send_cmd(uint8_t kind, uint32_t gen)
{
    audio_cmd_t cmd = {
        .kind = kind,
        .gen = gen,
    };
    if (xQueueSend(s_cmds, &cmd, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "audio queue full");
    }
}

void audio_play_creak(void)
{
    if (!s_task_up) {
        return;
    }
    send_cmd(AUDIO_CMD_CREAK, s_stop_gen);
}

void audio_fade(void)
{
    if (!s_task_up || !s_playing) {
        return;
    }
    s_fade = 1;
    int waited = 0;
    while (s_playing && waited < FADE_MS + 300) {
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
    }
    s_fade = 0;
}

void audio_stop(void)
{
    s_stop_gen++;
}

void audio_play_scream(void)
{
    if (!s_task_up) {
        return;
    }
    s_stop_gen++;
    xSemaphoreTake(s_scream_done, 0);
    audio_cmd_t cmd = {
        .kind = AUDIO_CMD_SCREAM,
        .gen = s_stop_gen,
    };
    if (xQueueSend(s_cmds, &cmd, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "audio queue full, scream dropped");
        return;
    }
    xSemaphoreTake(s_scream_done, portMAX_DELAY);
}
