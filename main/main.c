#include "config.h"
#include "audio.h"
#include "leds.h"
#include "motor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "scare";

static void run_scare(void)
{
    ESP_LOGI(TAG, "motion — extend, scream, pause, retract");

    motor_forward();
    vTaskDelay(pdMS_TO_TICKS(MOTOR_OUT_MS));
    motor_brake();

    leds_set_look(LED_LOOK_RED);
    audio_play_scream();
    leds_set_look(LED_LOOK_WHITE);

    vTaskDelay(pdMS_TO_TICKS(SCARE_PAUSE_MS));

    motor_reverse();
    vTaskDelay(pdMS_TO_TICKS(MOTOR_BACK_MS));
    motor_coast();
}

/* Drive home while the PIR settles, so a half-out prop starts at zero. */
static void home_during_warmup(void)
{
    int limit = PIR_WARMUP_MS > MOTOR_BACK_MS ? PIR_WARMUP_MS : MOTOR_BACK_MS;
    ESP_LOGI(TAG, "PIR warmup %d ms, returning motor to zero", limit);

    motor_reverse();
    int elapsed = 0;
    while (elapsed < limit) {
        vTaskDelay(pdMS_TO_TICKS(50));
        elapsed += 50;
        if (elapsed >= MOTOR_BACK_MS) {
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

    ESP_LOGI(TAG, "armed");
    int previous = gpio_get_level(PIN_PIR);
    while (1) {
        int level = gpio_get_level(PIN_PIR);
        if (level && !previous) {
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
    leds_init();

    xTaskCreate(scare_task, "scare", 6144, NULL, 5, NULL);
}
