#pragma once

/*
 * Halloween scare prop — edit this file to match your wiring.
 *
 * Target: ESP32 or ESP32-S3. Build from this folder:
 *   idf.py set-target esp32
 *   idf.py build flash monitor
 *
 * Share one ground between the ESP32, the H-bridge logic supply, the
 * NeoPixel 5V supply, the PIR, and the PCM5102.
 *
 * PCM5102 is a line-level DAC. Connect its L/R output to a powered
 * speaker or a small amp (PAM8403 and similar). A bare speaker on the
 * DAC output will be almost silent. Connect the module SCK pin to
 * PIN_I2S_MCLK. Power the module VIN from 5V. Sound comes from LOUT
 * and ROUT into an amplifier, not from the digital pins. If a pad on
 * the back is labeled XSMT or MUTE, tie that pad to 3.3V.
 *
 * NeoPixel rings need their own 5V supply. Two 16-LED rings at the
 * brightness below are on the order of an amp at full white — do not
 * power them from the ESP32 5V pin. One data wire feeds both rings:
 * GPIO to the first DIN, then that ring's DOUT to the next DIN.
 * A 330 ohm resistor in series with the data line is worth adding.
 * Data is 3.3V; short wires to WS2812 rings usually work without a
 * level shifter.
 *
 * H-bridge (L298N, TB6612, DRV8833 style):
 *   IN1 / AIN1 -> PIN_MOTOR_IN1
 *   IN2 / AIN2 -> PIN_MOTOR_IN2
 *   EN / ENA / PWMA -> PIN_MOTOR_PWM   (remove the L298N ENA jumper)
 *   Motor power supply -> driver VM / 12V pin, grounds tied together
 * If the prop lunges the wrong way, set MOTOR_SWAP_DIRECTION to 1.
 * If an L298N motor is weak, drop MOTOR_PWM_HZ to 1000.
 *
 * This board is an ESP32-S3 with flash and 8MB PSRAM in the module.
 * GPIO 26 through 37 belong to those chips. Driving any of them
 * resets the board in a loop.
 */

/* PIR (HC-SR312 OUT). Idle low, high when motion is seen. */
#define PIN_PIR                 4  //middle on sensor

/* H-bridge */
#define PIN_MOTOR_IN1           46
#define PIN_MOTOR_IN2           3
#define PIN_MOTOR_PWM           9   /* bridge EN / ENA */
#define MOTOR_SWAP_DIRECTION    0
#define MOTOR_PWM_HZ            20000
#define MOTOR_SPEED_PERCENT     100
/* Defaults until `out` / `in` have been saved from the serial console. */
#define MOTOR_OUT_MS            10000
#define MOTOR_BACK_MS           11000
#define MOTOR_MS_MIN            100
#define MOTOR_MS_MAX            120000

/* PCM5102 I2S. Connect the DAC SCK pin to PIN_I2S_MCLK. */
#define PIN_I2S_MCLK            17
#define PIN_I2S_BCK             16 //bck
#define PIN_I2S_WS              7  //rlck
#define PIN_I2S_DOUT            15 //din
#define AUDIO_SAMPLE_RATE_HZ    44100
#define AUDIO_BOOT_TONE         1  /* short beep at startup, set to 0 once the DAC is confirmed */
/* /storage/creaks plays once while the lid opens. A clip still running fades out for the scream. */

/* Onboard WS2812 used as a status lamp. Not part of the eye rings. */
#define PIN_STATUS              48
#define STATUS_BRIGHTNESS       32

/* Both WS2812 rings on one data pin. Chain DOUT of the first into DIN of the second. */
#define PIN_RING                18
#define RING_LED_COUNT          16
#define LED_BRIGHTNESS          120   /* 1..255, applied to white and to red */
#define LED_FADE_STEP           8     /* color change per LED_TICK_MS */
#define LED_TICK_MS             15

/* Scare timing */
#define SCARE_PAUSE_MS          1000  /* silence between the scream and the return */
#define PIR_WARMUP_MS           10000 /* HC-SR312 settles in a few seconds; this waits longer */
#define PIR_COOLDOWN_MS         2500  /* ignore the sensor after a scare */

/* Set to 1 to run the scare once after warmup, without waiting for the PIR. */
#define SCARE_ONCE_ON_BOOT      0

/*
 * Companion advertisement. This board only listens.
 *
 * Manufacturer data, after the AD type byte 0xFF:
 *   0..1    company id, little-endian
 *   2..5    magic
 *   6       command
 *   7..10   phone id, first 4 bytes of SHA-256(ANDROID_ID)
 *   11..14  counter, little-endian, must be greater than the last one stored for that phone
 *   15..22  first 8 bytes of HMAC-SHA1 over bytes 2..14
 *
 * The key is BLE_KEY_BYTES. Counters live in RAM until the next boot.
 * Change the key before this prop leaves the bench.
 */
#define BLE_PHONE_SLOTS         4
#define BLE_COMPANY_ID          0xFFFF
#define BLE_MAGIC_0             0xC0
#define BLE_MAGIC_1             0xFF
#define BLE_MAGIC_2             0x1E
#define BLE_MAGIC_3             0x57
#define BLE_CMD_SCARE           0x01
#define BLE_CMD_OPEN            0x02
#define BLE_CMD_RESET           0x03
#define BLE_TAG_LEN             8
#define BLE_KEY_BYTES \
    0x6B, 0xC1, 0x09, 0xE4, 0x52, 0x8A, 0x17, 0xD3, 0xF0, 0x3E, \
    0x91, 0x44, 0xAB, 0x76, 0x2C, 0x58, 0x0F, 0xDE, 0x85, 0x19
