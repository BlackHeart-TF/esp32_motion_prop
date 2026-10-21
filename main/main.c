#include "config.h"
#include "audio.h"
#include "ble_listen.h"
#include "console.h"
#include "leds.h"
#include "motor.h"
#include "scare.h"
#include "settings.h"
#include "usb_msc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "scare";

static volatile int s_once;
static volatile int s_stop;
static volatile int s_busy;
static volatile int s_hold;
static volatile int s_req;

static int wait_ms(uint32_t ms)
{
    uint32_t elapsed = 0;
    while (elapsed < ms) {
        if (s_stop) {
            return 1;
        }
        uint32_t step = 50;
        if (elapsed + step > ms) {
            step = ms - elapsed;
        }
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
    }
    return s_stop ? 1 : 0;
}

static void release_all(void)
{
    s_once = 0;
    s_stop = 0;
    s_req = 0;
    s_busy = 0;
    audio_stop();
    motor_coast();
    leds_set_look(LED_LOOK_OFF);
    leds_set_status(LED_STATUS_ARMED);
}

static void run_scare(void)
{
    uint32_t out_ms = settings_out_ms();
    uint32_t in_ms = settings_in_ms();
    ESP_LOGI(TAG, "motion — creak open %u ms, scream, pause, close %u ms",
             (unsigned)out_ms, (unsigned)in_ms);

    s_busy = 1;
    leds_set_status(LED_STATUS_SCARE);
    leds_set_look(LED_LOOK_WHITE);
    audio_play_creak();
    motor_forward();
    if (wait_ms(out_ms)) {
        release_all();
        return;
    }
    motor_brake();
    audio_fade();

    leds_set_look(LED_LOOK_RED);
    audio_play_scream();
    if (s_stop) {
        release_all();
        return;
    }
    leds_set_look(LED_LOOK_WHITE);

    if (wait_ms(SCARE_PAUSE_MS)) {
        release_all();
        return;
    }

    motor_reverse();
    if (wait_ms(in_ms)) {
        release_all();
        return;
    }
    motor_coast();
    audio_stop();
    leds_set_look(LED_LOOK_OFF);
    if (s_hold) {
        leds_set_status(LED_STATUS_BOOT);
    } else {
        leds_set_status(LED_STATUS_ARMED);
    }
    s_busy = 0;
}

void scare_once(void)
{
    s_stop = 0;
    s_once = 1;
}

int scare_open(void)
{
    if (s_busy || s_once || s_req) {
        return -1;
    }
    s_hold = 1;
    s_req = 1;
    return 0;
}

int scare_reset(void)
{
    if (s_busy || s_once || s_req) {
        return -1;
    }
    s_req = 2;
    return 0;
}

/* Drive home while the PIR settles, so a half-out prop starts at zero. */
static void home_during_warmup(void)
{
    int back_ms = (int)settings_in_ms();
    int limit = PIR_WARMUP_MS > back_ms ? PIR_WARMUP_MS : back_ms;
    ESP_LOGI(TAG, "PIR warmup %d ms, returning motor to zero", limit);

    motor_reverse();
    int elapsed = 0;
    while (elapsed < limit) {
        vTaskDelay(pdMS_TO_TICKS(50));
        elapsed += 50;
        if (elapsed >= back_ms) {
            motor_coast();
        }
    }
    motor_coast();
}

static void scare_task(void *arg)
{
    (void)arg;

    home_during_warmup();

#if SCARE_ONCE_ON_BOOT
    run_scare();
    vTaskDelay(pdMS_TO_TICKS(PIR_COOLDOWN_MS));
#endif

    leds_set_status(LED_STATUS_ARMED);
    ESP_LOGI(TAG, "armed");
    int previous = gpio_get_level(PIN_PIR);
    while (1) {
        if (s_req == 1) {
            uint32_t ms = settings_out_ms();
            s_req = 0;
            s_hold = 1;
            s_busy = 1;
            ESP_LOGI(TAG, "open %u ms, then hold", (unsigned)ms);
            motor_forward();
            wait_ms(ms);
            motor_brake();
            s_busy = 0;
            leds_set_look(LED_LOOK_OFF);
            leds_set_status(LED_STATUS_BOOT);
            ESP_LOGI(TAG, "holding, sensor off until reset");
            continue;
        }
        if (s_req == 2) {
            uint32_t ms = settings_in_ms();
            s_req = 0;
            s_busy = 1;
            ESP_LOGI(TAG, "reset %u ms, then sensor on", (unsigned)ms);
            motor_reverse();
            wait_ms(ms);
            motor_coast();
            s_hold = 0;
            s_busy = 0;
            leds_set_look(LED_LOOK_OFF);
            leds_set_status(LED_STATUS_ARMED);
            ESP_LOGI(TAG, "armed");
            continue;
        }
        if (s_once) {
            s_once = 0;
            run_scare();
            continue;
        }

        int level = gpio_get_level(PIN_PIR);
        if (!s_hold && level && !previous) {
            run_scare();
            vTaskDelay(pdMS_TO_TICKS(PIR_COOLDOWN_MS));
            while (gpio_get_level(PIN_PIR)) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            level = 0;
        }
        previous = level;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    settings_init();
    leds_init();
    console_start();

    if (usb_msc_claimed_at_boot()) {
        leds_set_status(LED_STATUS_USB);
        ESP_LOGI(TAG, "mass storage mode, scare stayed off. Reset with USB unplugged to arm.");
        return;
    }

    gpio_config_t pir = {
        .pin_bit_mask = 1ULL << PIN_PIR,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pir));

    motor_init();
    motor_coast();
    audio_init();

    xTaskCreatePinnedToCore(scare_task, "scare", 6144, NULL, 5, NULL, 0);
    ble_listen_start();
}
