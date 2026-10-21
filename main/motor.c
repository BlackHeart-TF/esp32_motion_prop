#include "motor.h"

#include "config.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "motor";

#define DUTY_MAX 1023

static void set_pwm(uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

static void set_inputs(int in1, int in2)
{
#if MOTOR_SWAP_DIRECTION
    int swap = in1;
    in1 = in2;
    in2 = swap;
#endif
    gpio_set_level(PIN_MOTOR_IN1, in1);
    gpio_set_level(PIN_MOTOR_IN2, in2);
}

/* 100% duty is a solid high, 0% a solid low, so the pad can be read back. */
static void verify_en(void)
{
    ESP_ERROR_CHECK(gpio_input_enable(PIN_MOTOR_PWM));

    set_pwm(DUTY_MAX);
    esp_rom_delay_us(2000);
    int high = gpio_get_level(PIN_MOTOR_PWM);
    uint32_t duty_high = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    set_pwm(0);
    esp_rom_delay_us(2000);
    int low = gpio_get_level(PIN_MOTOR_PWM);
    uint32_t duty_low = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    if (high == 1 && low == 0 && duty_high == DUTY_MAX && duty_low == 0) {
        ESP_LOGI(TAG, "EN GPIO %d OK", PIN_MOTOR_PWM);
    } else {
        ESP_LOGE(TAG, "EN GPIO %d stuck: high=%d duty=%lu, low=%d duty=%lu",
                 PIN_MOTOR_PWM, high, (unsigned long)duty_high,
                 low, (unsigned long)duty_low);
    }
}

void motor_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_IN1) | (1ULL << PIN_MOTOR_IN2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    set_inputs(0, 0);

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = MOTOR_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = PIN_MOTOR_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    verify_en();
    ESP_LOGI(TAG, "H-bridge ready, EN GPIO %d, speed %d%%, pwm %d Hz",
             PIN_MOTOR_PWM, MOTOR_SPEED_PERCENT, MOTOR_PWM_HZ);
}

static uint32_t run_duty(void)
{
    int percent = MOTOR_SPEED_PERCENT;
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    return (uint32_t)((DUTY_MAX * percent) / 100);
}

void motor_forward(void)
{
    set_inputs(1, 0);
    set_pwm(run_duty());
}

void motor_reverse(void)
{
    set_inputs(0, 1);
    set_pwm(run_duty());
}

void motor_brake(void)
{
    set_inputs(1, 1);
    set_pwm(DUTY_MAX);
}

void motor_coast(void)
{
    set_pwm(0);
    set_inputs(0, 0);
}
