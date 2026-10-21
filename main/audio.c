#include "audio.h"

#include "config.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"

static const char *TAG = "audio";

#define CHUNK_FRAMES 128
#define SCREAM_MAGIC 0x4D524353u /* 'SCRM' little-endian */

typedef struct {
    uint32_t offset;
    uint32_t nbytes;
} scream_entry_t;

static i2s_chan_handle_t s_tx;
static int32_t s_buf[CHUNK_FRAMES * 2];
static const uint8_t *s_map;
static uint32_t s_count;
static esp_partition_mmap_handle_t s_mmap;

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

static void load_screams(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "screams");
    if (part == NULL) {
        ESP_LOGE(TAG, "screams partition not found");
        return;
    }

    const void *mapped = NULL;
    esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                                       &mapped, &s_mmap);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "screams mmap failed (%s)", esp_err_to_name(err));
        return;
    }

    s_map = mapped;
    uint32_t magic = 0;
    uint32_t count = 0;
    memcpy(&magic, s_map, sizeof(magic));
    memcpy(&count, s_map + 4, sizeof(count));
    if (magic != SCREAM_MAGIC || count == 0 || count > 32) {
        ESP_LOGE(TAG, "screams image is missing or corrupt");
        s_count = 0;
        return;
    }

    s_count = count;
    ESP_LOGI(TAG, "%u screams ready", (unsigned)s_count);
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
    load_screams();

    i2s_chan_info_t info = {0};
    ESP_ERROR_CHECK(i2s_channel_get_info(s_tx, &info));
    ESP_LOGI(TAG, "PCM5102 I2S %d Hz, MCLK gpio %d (%u Hz), BCK %d, WS %d, DIN %d, BCLK %u Hz",
             AUDIO_SAMPLE_RATE_HZ, PIN_I2S_MCLK, (unsigned)info.mclk_hz,
             PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DOUT, (unsigned)info.bclk_hz);
#if AUDIO_BOOT_TONE
    play_boot_tone();
#endif
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

void audio_play_scream(void)
{
    if (s_count == 0 || s_map == NULL) {
        ESP_LOGW(TAG, "no screams loaded");
        return;
    }

    uint32_t index = esp_random() % s_count;
    scream_entry_t entry;
    memcpy(&entry, s_map + 8 + index * sizeof(entry), sizeof(entry));
    if (entry.nbytes < sizeof(int16_t)) {
        ESP_LOGW(TAG, "scream %u is empty", (unsigned)(index + 1));
        return;
    }

    const int16_t *pcm = (const int16_t *)(s_map + entry.offset);
    size_t frames = entry.nbytes / sizeof(int16_t);
    uint32_t ms = (uint32_t)((uint64_t)frames * 1000 / AUDIO_SAMPLE_RATE_HZ);
    ESP_LOGI(TAG, "playing scream %u (%u ms)", (unsigned)(index + 1), (unsigned)ms);

    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done;
        if (n > CHUNK_FRAMES) {
            n = CHUNK_FRAMES;
        }
        for (size_t i = 0; i < n; i++) {
            int32_t sample = ((int32_t)pcm[done + i]) << 16;
            s_buf[i * 2] = sample;
            s_buf[i * 2 + 1] = sample;
        }
        write_frames(n);
        done += n;
    }
    write_silence(80);
}
