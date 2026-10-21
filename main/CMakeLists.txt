idf_component_register(
    SRCS "main.c" "audio.c" "motor.c" "leds.c" "settings.c" "console.c" "usb_msc.c" "ble_listen.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_i2s esp_partition esp_hw_support esp_common log freertos nvs_flash console esp_tinyusb wear_levelling fatfs bt mbedtls esp_timer
)
