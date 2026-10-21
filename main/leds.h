#pragma once

typedef enum {
    LED_LOOK_WHITE = 0,
    LED_LOOK_RED,
} led_look_t;

void leds_init(void);
void leds_set_look(led_look_t look);
