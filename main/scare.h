#pragma once

#include <stdint.h>

/* Run one scare, then go back to waiting on the sensor. */
void scare_once(void);

/* Drive open for the saved open time, then hold. The sensor stays off. Returns 0, or -1 if the motor is busy. */
int scare_open(void);

/* Drive shut for the saved close time, then arm the sensor. Returns 0, or -1 if the motor is busy. */
int scare_reset(void);
