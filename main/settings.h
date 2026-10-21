#pragma once

#include <stdint.h>

#include "esp_err.h"

/* Open and close times live in the "scare" NVS namespace as out_ms and in_ms.
 * With nothing stored yet, the values in config.h are used. */

void settings_init(void);

uint32_t settings_out_ms(void);
uint32_t settings_in_ms(void);

/* Rejects values outside MOTOR_MS_MIN..MOTOR_MS_MAX. Writes flash before
 * the new value is visible to a scare. */
esp_err_t settings_set_out_ms(uint32_t ms);
esp_err_t settings_set_in_ms(uint32_t ms);
