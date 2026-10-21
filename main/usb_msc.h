#pragma once

#include <stdbool.h>

/* Start USB device mode and wait briefly for a host to configure us.
 * A host means this board was enumerated as a device, and the storage
 * partition stays exported as a mass-storage drive.
 * No host shuts USB back down so the scare can run. */
bool usb_msc_claimed_at_boot(void);
