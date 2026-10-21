#include "settings.h"

#include "config.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

static const char *NS = "scare";
static const char *KEY_OUT = "out_ms";
static const char *KEY_IN = "in_ms";

/* 32-bit stores are atomic on this chip, so the scare task can read these
 * while a command writes one of them. */
static volatile uint32_t s_out_ms = MOTOR_OUT_MS;
static volatile uint32_t s_in_ms = MOTOR_BACK_MS;
static nvs_handle_t s_nvs;
static int s_ready;

static uint32_t load_ms(const char *key, const char *label, uint32_t fallback)
{
    uint32_t value = 0;
    esp_err_t err = nvs_get_u32(s_nvs, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "%s %u ms (default)", label, (unsigned)fallback);
        return fallback;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s unreadable (%s), using %u ms",
                 label, esp_err_to_name(err), (unsigned)fallback);
        return fallback;
    }
    if (value < MOTOR_MS_MIN || value > MOTOR_MS_MAX) {
        ESP_LOGW(TAG, "%s saved as %u ms is outside %d..%d, using %u ms",
                 label, (unsigned)value, MOTOR_MS_MIN, MOTOR_MS_MAX, (unsigned)fallback);
        return fallback;
    }
    ESP_LOGI(TAG, "%s %u ms (saved)", label, (unsigned)value);
    return value;
}

void settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing unusable nvs (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed (%s), using default times", esp_err_to_name(err));
        return;
    }

    err = nvs_open(NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs open failed (%s), using default times", esp_err_to_name(err));
        return;
    }

    s_ready = 1;
    s_out_ms = load_ms(KEY_OUT, "open", MOTOR_OUT_MS);
    s_in_ms = load_ms(KEY_IN, "close", MOTOR_BACK_MS);
}

uint32_t settings_out_ms(void)
{
    return s_out_ms;
}

uint32_t settings_in_ms(void)
{
    return s_in_ms;
}

static esp_err_t save_ms(const char *key, volatile uint32_t *slot, uint32_t ms)
{
    if (ms < MOTOR_MS_MIN || ms > MOTOR_MS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_set_u32(s_nvs, key, ms);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        return err;
    }
    *slot = ms;
    return ESP_OK;
}

esp_err_t settings_set_out_ms(uint32_t ms)
{
    return save_ms(KEY_OUT, &s_out_ms, ms);
}

esp_err_t settings_set_in_ms(uint32_t ms)
{
    return save_ms(KEY_IN, &s_in_ms, ms);
}
