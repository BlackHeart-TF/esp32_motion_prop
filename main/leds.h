#pragma once

typedef enum {
    LED_LOOK_OFF = 0,
    LED_LOOK_WHITE,
    LED_LOOK_RED,
} led_look_t;

/* Onboard pixel. Amber while booting, blue in mass storage, green when armed, red during a scare. */
typedef enum {
    LED_STATUS_BOOT = 0,
    LED_STATUS_USB,
    LED_STATUS_ARMED,
    LED_STATUS_SCARE,
} led_status_t;

void leds_init(void);
void leds_set_look(led_look_t look);
void leds_set_status(led_status_t status);
