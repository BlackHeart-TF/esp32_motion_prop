#include "leds.h"

#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "leds";

static led_strip_handle_t s_ring;
static volatile led_look_t s_look = LED_LOOK_WHITE;

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
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

void leds_init(void)
{
    led_strip_config_t strip = {
        .strip_gpio_num = PIN_RING,
        .max_leds = RING_LED_COUNT,
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
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip, &rmt, &s_ring));
    ESP_ERROR_CHECK(led_strip_clear(s_ring));
    ESP_LOGI(TAG, "%d LEDs on GPIO %d", RING_LED_COUNT, PIN_RING);
    xTaskCreate(led_task, "leds", 4096, NULL, 4, NULL);
}

void leds_set_look(led_look_t look)
{
    s_look = look;
}
