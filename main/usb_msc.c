#include "usb_msc.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "wear_levelling.h"

static const char *TAG = "usb";

/* A computer usually finishes SetConfiguration well inside this. A charger
 * never does, so the prop still arms when that is the only thing on the plug. */
#define USB_HOST_WAIT_MS 3000

static volatile bool s_host_seen;
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static tinyusb_msc_storage_handle_t s_storage;

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL,
};

enum {
    EPNUM_MSC = 1,
};

enum {
    STRID_MSC = 4,
};

#define USB_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const tusb_desc_device_t s_dev_desc = {
    .bLength = sizeof(s_dev_desc),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = TINYUSB_ESPRESSIF_VID,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const uint8_t s_fs_cfg[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, USB_CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, STRID_MSC, EPNUM_MSC, 0x80 | EPNUM_MSC, 64),
};

static char s_serial[13] = "000000000000";

static const char *s_usb_strings[] = {
    (const char[]) {0x09, 0x04},
    "Coffin",
    "Coffin",
    s_serial,
    "Storage",
};

static void usb_event(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (event->id == TINYUSB_EVENT_ATTACHED) {
        s_host_seen = true;
    }
}

static void release_usb(void)
{
    if (s_storage != NULL) {
        esp_err_t err = tinyusb_msc_delete_storage(s_storage);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "storage delete failed (%s)", esp_err_to_name(err));
        }
        s_storage = NULL;
    }
    if (tud_inited()) {
        esp_err_t err = tinyusb_driver_uninstall();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "usb uninstall failed (%s)", esp_err_to_name(err));
        }
    }
    if (s_wl != WL_INVALID_HANDLE) {
        esp_err_t err = wl_unmount(s_wl);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "wear levelling unmount failed (%s)", esp_err_to_name(err));
        }
        s_wl = WL_INVALID_HANDLE;
    }
}

bool usb_msc_claimed_at_boot(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    if (part == NULL) {
        ESP_LOGW(TAG, "storage partition missing, skipping USB");
        return false;
    }

    esp_err_t err = wl_mount(part, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wear levelling mount failed (%s)", esp_err_to_name(err));
        s_wl = WL_INVALID_HANDLE;
        return false;
    }

    tinyusb_msc_storage_config_t storage_cfg = {
        .medium = {
            .wl_handle = s_wl,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        .fat_fs = {
            .base_path = NULL,
            .do_not_format = false,
            .format_flags = 0,
            .config = {
                .max_files = 4,
                .format_if_mount_failed = true,
            },
        },
    };
    err = tinyusb_msc_new_storage_spiflash(&storage_cfg, &s_storage);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mass storage init failed (%s)", esp_err_to_name(err));
        release_usb();
        return false;
    }

    s_host_seen = false;
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_BASE) == ESP_OK) {
        snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(usb_event);
    tusb_cfg.descriptor.device = &s_dev_desc;
    tusb_cfg.descriptor.full_speed_config = s_fs_cfg;
    tusb_cfg.descriptor.string = s_usb_strings;
    tusb_cfg.descriptor.string_count = sizeof(s_usb_strings) / sizeof(s_usb_strings[0]);
    err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb device init failed (%s)", esp_err_to_name(err));
        release_usb();
        return false;
    }

    ESP_LOGI(TAG, "USB device up, waiting %d ms for a host", USB_HOST_WAIT_MS);
    int waited = 0;
    while (waited < USB_HOST_WAIT_MS) {
        if (s_host_seen || tud_mounted()) {
            ESP_LOGI(TAG, "host enumerated this board, storage is the mass-storage drive");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        waited += 50;
    }

    ESP_LOGI(TAG, "no USB host, shutting USB down");
    release_usb();
    return false;
}
