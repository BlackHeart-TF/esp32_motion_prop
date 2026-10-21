#include "console.h"

#include "config.h"
#include "scare.h"
#include "settings.h"

#include "ble_listen.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "console";

static int parse_ms(const char *text, uint32_t *ms)
{
    if (text == NULL || text[0] < '0' || text[0] > '9') {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    *ms = (uint32_t)value;
    return 1;
}

static int set_or_show(const char *name, int argc, char **argv,
                       uint32_t (*get)(void), esp_err_t (*set)(uint32_t))
{
    if (argc == 1) {
        printf("%s %lu ms\n", name, (unsigned long)get());
        return 0;
    }
    if (argc != 2) {
        printf("usage: %s [ms]\n", name);
        return 1;
    }

    uint32_t ms = 0;
    if (!parse_ms(argv[1], &ms)) {
        printf("%s must be a whole number of milliseconds, %d to %d\n",
               name, MOTOR_MS_MIN, MOTOR_MS_MAX);
        return 1;
    }

    esp_err_t err = set(ms);
    if (err == ESP_ERR_INVALID_ARG) {
        printf("%s must be from %d to %d ms\n", name, MOTOR_MS_MIN, MOTOR_MS_MAX);
        return 1;
    }
    if (err != ESP_OK) {
        printf("could not save %s (%s)\n", name, esp_err_to_name(err));
        return 1;
    }
    printf("%s %lu ms, saved\n", name, (unsigned long)ms);
    return 0;
}

static int cmd_out(int argc, char **argv)
{
    return set_or_show("out", argc, argv, settings_out_ms, settings_set_out_ms);
}

static int cmd_in(int argc, char **argv)
{
    return set_or_show("in", argc, argv, settings_in_ms, settings_set_in_ms);
}

static int cmd_times(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("usage: times\n");
        return 1;
    }
    printf("out %lu ms\nin %lu ms\n",
           (unsigned long)settings_out_ms(),
           (unsigned long)settings_in_ms());
    return 0;
}

static int cmd_scare(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("usage: scare\n");
        return 1;
    }
    scare_once();
    printf("scare\n");
    return 0;
}

static int cmd_open(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("usage: open\n");
        return 1;
    }
    if (scare_open() != 0) {
        printf("motor is busy\n");
        return 1;
    }
    printf("open, sensor off until reset\n");
    return 0;
}

static int cmd_ble(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        ble_listen_clear();
        printf("ble phones cleared\n");
        return 0;
    }
    if (argc != 1) {
        printf("usage: ble [clear]\n");
        return 1;
    }
    if (!ble_listen_running()) {
        printf("ble off\n");
        return 0;
    }
    int any = 0;
    for (int i = 0; i < BLE_PHONE_SLOTS; i++) {
        uint32_t id = 0;
        uint32_t counter = 0;
        if (!ble_listen_phone(i, &id, &counter)) {
            continue;
        }
        printf("%08lx %lu\n", (unsigned long)id, (unsigned long)counter);
        any = 1;
    }
    if (!any) {
        printf("none\n");
    }
    return 0;
}

static int cmd_reset(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("usage: reset\n");
        return 1;
    }
    if (scare_reset() != 0) {
        printf("motor is busy\n");
        return 1;
    }
    printf("reset, then sensor on\n");
    return 0;
}

static void register_cmd(const char *name, const char *help, const char *hint,
                         esp_console_cmd_func_t func)
{
    const esp_console_cmd_t cmd = {
        .command = name,
        .help = help,
        .hint = hint,
        .func = func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "scare>";
    /* nvs_commit sits under the command, so leave the repl more than 4 KB. */
    repl_config.task_stack_size = 6144;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console init failed (%s)", esp_err_to_name(err));
        return;
    }

    register_cmd("out",
                 "Show or set how long the lid drives open, in milliseconds. Saved to flash.",
                 "[ms]", cmd_out);
    register_cmd("in",
                 "Show or set how long the lid drives shut, in milliseconds. Saved to flash.",
                 "[ms]", cmd_in);
    register_cmd("times", "Show the open and close times.", NULL, cmd_times);
    register_cmd("scare", "Run one scare.", NULL, cmd_scare);
    register_cmd("open",
                 "Drive open for the saved open time, then hold. The sensor stays off.",
                 NULL, cmd_open);
    register_cmd("reset",
                 "Drive shut for the saved close time, then arm the sensor.",
                 NULL, cmd_reset);
    register_cmd("ble",
                 "Show each companion phone and its counter, or 'ble clear' to forget them.",
                 "[clear]", cmd_ble);
    ESP_ERROR_CHECK(esp_console_register_help_command());

    printf("\nTimes: out [ms]   in [ms]   times   help\n");
    printf("Test:  scare   open   reset\n");
    printf("BLE:   ble   ble clear\n");
    printf("Range %d..%d ms. A scare already running keeps the times it started with.\n",
           MOTOR_MS_MIN, MOTOR_MS_MAX);

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console start failed (%s)", esp_err_to_name(err));
    }
}
