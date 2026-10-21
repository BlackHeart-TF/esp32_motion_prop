#include "ble_listen.h"

#include "config.h"
#include "scare.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_store.h"

void ble_store_config_init(void);

#include "mbedtls/md.h"

static const char *TAG = "ble";

static const uint8_t s_secret[] = { BLE_KEY_BYTES };

struct ble_phone {
    uint32_t id;
    uint32_t counter;
    uint32_t stamp;
};

static int s_running;
static struct ble_phone s_phones[BLE_PHONE_SLOTS];
static uint32_t s_stamp;
static int64_t s_warn_at;

static void warn_later(const char *msg)
{
    int64_t now = esp_timer_get_time();
    if (now < s_warn_at) {
        return;
    }
    s_warn_at = now + 5000000;
    ESP_LOGW(TAG, "%s", msg);
}

static int find_phone(uint32_t id)
{
    for (int i = 0; i < BLE_PHONE_SLOTS; i++) {
        if (s_phones[i].id == id) {
            return i;
        }
    }
    return -1;
}

static int pick_slot(void)
{
    int oldest = 0;
    for (int i = 0; i < BLE_PHONE_SLOTS; i++) {
        if (s_phones[i].id == 0) {
            return i;
        }
        if (s_phones[i].stamp < s_phones[oldest].stamp) {
            oldest = i;
        }
    }
    return oldest;
}

static int tag_ok(const uint8_t *msg, const uint8_t *tag)
{
    uint8_t mac[20];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (info == NULL) {
        return 0;
    }
    if (mbedtls_md_hmac(info, s_secret, sizeof(s_secret), msg, 13, mac) != 0) {
        return 0;
    }
    uint8_t diff = 0;
    for (int i = 0; i < BLE_TAG_LEN; i++) {
        diff |= (uint8_t)(mac[i] ^ tag[i]);
    }
    return diff == 0;
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void consider(const uint8_t *body, uint8_t len)
{
    if (len < 23) {
        return;
    }
    if (body[0] != (uint8_t)(BLE_COMPANY_ID & 0xff)
        || body[1] != (uint8_t)((BLE_COMPANY_ID >> 8) & 0xff)) {
        return;
    }
    if (body[2] != BLE_MAGIC_0 || body[3] != BLE_MAGIC_1
        || body[4] != BLE_MAGIC_2 || body[5] != BLE_MAGIC_3) {
        return;
    }

    uint8_t cmd = body[6];
    uint32_t id = read_le32(body + 7);
    uint32_t counter = read_le32(body + 11);
    int slot = find_phone(id);
    if (id == 0 || counter == 0) {
        return;
    }
    if (slot >= 0 && counter <= s_phones[slot].counter) {
        return;
    }
    if (!tag_ok(body + 2, body + 15)) {
        warn_later("companion packet failed the key check");
        return;
    }

    const char *name;
    int rc = 0;
    switch (cmd) {
    case BLE_CMD_SCARE:
        scare_once();
        name = "scare";
        break;
    case BLE_CMD_OPEN:
        rc = scare_open();
        name = "open";
        break;
    case BLE_CMD_RESET:
        rc = scare_reset();
        name = "reset";
        break;
    default:
        warn_later("companion packet had an unknown command");
        return;
    }
    if (rc != 0) {
        warn_later("companion command ignored, motor busy");
        return;
    }

    if (slot < 0) {
        slot = pick_slot();
    }
    s_phones[slot].id = id;
    s_phones[slot].counter = counter;
    s_phones[slot].stamp = ++s_stamp;
    ESP_LOGI(TAG, "%s %08lx %u", name, (unsigned long)id, (unsigned)counter);
}

static void on_adv(const uint8_t *data, uint8_t len)
{
    uint8_t i = 0;
    while (i + 1 < len) {
        uint8_t field = data[i];
        if (field == 0 || (uint16_t)i + 1u + field > len) {
            return;
        }
        const uint8_t *ad = &data[i + 1];
        if (ad[0] == 0xFF && field >= 24) {
            consider(ad + 1, (uint8_t)(field - 1));
        }
        i = (uint8_t)(i + 1 + field);
    }
}

static void start_scan(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        on_adv(event->disc.data, event->disc.length_data);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        start_scan();
        return 0;
    default:
        return 0;
    }
}

static void start_scan(void)
{
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "no address (%d)", rc);
        return;
    }

    struct ble_gap_disc_params disc = {0};
    disc.passive = 1;
    disc.itvl = 0x80;
    disc.window = 0x80;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY && rc != BLE_HS_EBUSY) {
        ESP_LOGE(TAG, "scan failed (%d)", rc);
    }
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset (%d)", reason);
}

static void on_sync(void)
{
    start_scan();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_listen_start(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble init failed (%s)", esp_err_to_name(err));
        return;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    s_running = 1;
    ESP_LOGI(TAG, "listening");
}

int ble_listen_running(void)
{
    return s_running;
}

int ble_listen_phone(int index, uint32_t *id, uint32_t *counter)
{
    if (index < 0 || index >= BLE_PHONE_SLOTS || s_phones[index].id == 0) {
        return 0;
    }
    if (id != NULL) {
        *id = s_phones[index].id;
    }
    if (counter != NULL) {
        *counter = s_phones[index].counter;
    }
    return 1;
}

void ble_listen_clear(void)
{
    s_stamp = 0;
    for (int i = 0; i < BLE_PHONE_SLOTS; i++) {
        s_phones[i].id = 0;
        s_phones[i].counter = 0;
        s_phones[i].stamp = 0;
    }
}
