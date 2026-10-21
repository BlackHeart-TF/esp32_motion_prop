#include "leds.h"

#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "leds";

static led_strip_handle_t s_ring;
static led_strip_handle_t s_status;
static volatile led_look_t s_look = LED_LOOK_OFF;
static volatile led_status_t s_status_look = LED_STATUS_BOOT;

static void paint(int r, int g, int b)
{
    if (s_ring == NULL || RING_LED_COUNT == 0) {
        return;
    }
    for (uint32_t i = 0; i < RING_LED_COUNT; i++) {
        led_strip_set_pixel(s_ring, i, (uint32_t)r, (uint32_t)g, (uint32_t)b);
    }
    led_strip_refresh(s_ring);
}

static void paint_status(void)
{
    if (s_status == NULL) {
        return;
    }
    int level = STATUS_BRIGHTNESS;
    int r = 0;
    int g = 0;
    int b = 0;
    switch (s_status_look) {
    case LED_STATUS_USB:
        b = level;
        break;
    case LED_STATUS_ARMED:
        g = level;
        break;
    case LED_STATUS_SCARE:
        r = level;
        break;
    case LED_STATUS_BOOT:
    default:
        r = level;
        g = level / 2;
        break;
    }
    led_strip_set_pixel(s_status, 0, (uint32_t)r, (uint32_t)g, (uint32_t)b);
    led_strip_refresh(s_status);
}

static int approach(int current, int target)
{
    int delta = target - current;
    if (delta > LED_FADE_STEP) {
        return current + LED_FADE_STEP;
    }
    if (delta < -LED_FADE_STEP) {
        return current - LED_FADE_STEP;
    }
    return target;
}

static void led_task(void *arg)
{
    (void)arg;
    int r = 0;
    int g = 0;
    int b = 0;

    while (1) {
        int level = (s_look == LED_LOOK_RED) ? LED_BRIGHTNESS : 0;
        int white = (s_look == LED_LOOK_WHITE) ? LED_BRIGHTNESS : 0;
        r = approach(r, (s_look == LED_LOOK_RED) ? level : white);
        g = approach(g, white);
        b = approach(b, white);
        paint(r, g, b);
        paint_status();
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

static led_strip_handle_t new_strip(int gpio, uint32_t count)
{
    led_strip_config_t strip = {
        .strip_gpio_num = gpio,
        .max_leds = count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };
    led_strip_handle_t handle = NULL;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip, &rmt, &handle));
    ESP_ERROR_CHECK(led_strip_clear(handle));
    return handle;
}

void leds_init(void)
{
    s_status = new_strip(PIN_STATUS, 1);
    s_ring = new_strip(PIN_RING, RING_LED_COUNT);
    ESP_LOGI(TAG, "status LED on GPIO %d, %d ring LEDs on GPIO %d",
             PIN_STATUS, RING_LED_COUNT, PIN_RING);
    xTaskCreate(led_task, "leds", 4096, NULL, 4, NULL);
}

void leds_set_look(led_look_t look)
{
    s_look = look;
}

void leds_set_status(led_status_t status)
{
    s_status_look = status;
}
