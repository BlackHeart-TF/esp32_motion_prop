/*
 * Loop one WAV into the PCM5102 and nothing else.
 *
 *   DAC SCK  -> GPIO 17   (or tie SCK to GND instead)
 *   DAC BCK  -> GPIO 16
 *   DAC RLCK -> GPIO 7
 *   DAC DIN  -> GPIO 15
 *   DAC VIN  -> ESP32 5V
 *   DAC GND  -> ESP32 GND
 *   LOUT or ROUT -> amp or powered speaker
 *
 * From this folder:
 *   idf.py set-target esp32s3
 *   idf.py -p COM3 build flash monitor
 */

#include <stdint.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dac_test";

#define PIN_I2S_MCLK GPIO_NUM_17
#define PIN_I2S_BCK  GPIO_NUM_16
#define PIN_I2S_WS   GPIO_NUM_7
#define PIN_I2S_DOUT GPIO_NUM_15
#define SAMPLE_RATE  44100
#define CHUNK        256

extern const uint8_t scream_wav_start[] asm("_binary_scream_wav_start");
extern const uint8_t scream_wav_end[] asm("_binary_scream_wav_end");

static i2s_chan_handle_t s_tx;
static int16_t s_buf[CHUNK * 2];

static const int16_t *find_pcm(const uint8_t *wav, size_t length, size_t *sample_count)
{
    if (length < 12 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return NULL;
    }

    size_t offset = 12;
    while (offset + 8 <= length) {
        uint32_t chunk_size = 0;
        memcpy(&chunk_size, wav + offset + 4, sizeof(chunk_size));
        if (memcmp(wav + offset, "data", 4) == 0) {
            if (offset + 8 + chunk_size > length || (chunk_size % 2) != 0) {
                return NULL;
            }
            *sample_count = chunk_size / sizeof(int16_t);
            return (const int16_t *)(wav + offset + 8);
        }
        offset += 8 + chunk_size + (chunk_size & 1);
    }
    return NULL;
}

static void i2s_init(void)
{
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 8;
    channel.dma_frame_num = CHUNK;
    channel.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&channel, &s_tx, NULL));

    /* 16-bit Philips stereo. BCK is 32x the sample rate, which the
     * PCM5102 accepts with SCK grounded or with SCK wired to GPIO 17. */
    i2s_std_config_t standard = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
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
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &standard));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
}

static void write_samples(const int16_t *pcm, size_t count)
{
    size_t done = 0;
    while (done < count) {
        size_t n = count - done;
        if (n > CHUNK) {
            n = CHUNK;
        }
        for (size_t i = 0; i < n; i++) {
            s_buf[i * 2] = pcm[done + i];
            s_buf[i * 2 + 1] = pcm[done + i];
        }
        size_t written = 0;
        ESP_ERROR_CHECK(i2s_channel_write(s_tx, s_buf, n * 2 * sizeof(int16_t), &written, 2000));
        done += n;
    }
}

void app_main(void)
{
    size_t wav_bytes = (size_t)(scream_wav_end - scream_wav_start);
    size_t samples = 0;
    const int16_t *pcm = find_pcm(scream_wav_start, wav_bytes, &samples);
    if (pcm == NULL || samples == 0) {
        ESP_LOGE(TAG, "scream.wav is not 16-bit PCM");
        return;
    }

    i2s_init();
    unsigned ms = (unsigned)((uint64_t)samples * 1000 / SAMPLE_RATE);
    ESP_LOGI(TAG, "looping %u samples (%u ms)", (unsigned)samples, ms);
    ESP_LOGI(TAG, "SCK->GPIO%d  BCK->GPIO%d  RLCK->GPIO%d  DIN->GPIO%d  VIN->5V",
             PIN_I2S_MCLK, PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DOUT);
    ESP_LOGI(TAG, "listen on LOUT or ROUT through an amp");

    while (1) {
        write_samples(pcm, samples);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
