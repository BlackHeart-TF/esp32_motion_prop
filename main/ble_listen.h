#pragma once

#include <stdint.h>

/* Passive scan. The board does not advertise. */
void ble_listen_start(void);

int ble_listen_running(void);

/* 1 if this slot holds a phone. index is 0 .. BLE_PHONE_SLOTS-1. */
int ble_listen_phone(int index, uint32_t *id, uint32_t *counter);

/* Forget every phone so a reinstalled app can start again. */
void ble_listen_clear(void);
