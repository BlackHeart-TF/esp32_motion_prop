# Halloween scare

ESP-IDF firmware for an ESP32 (or ESP32-S3). A PIR sensor starts this sequence:

1. The rings stay white while the motor runs forward (the prop lunges out).
2. The rings fade red while a scream plays through the PCM5102, then fade back to white.
3. It waits one second.
4. The motor runs back, still on white.

Pins, LED counts, motor timing, and speed are all in `main/config.h`.

## Wiring

Use a common ground for every board. Give the NeoPixels their own 5V supply.

| Signal | GPIO | Connect to |
| --- | --- | --- |
| PIR OUT | 4 | HC-SR312 OUT (3.3V or 5V) |
| Motor IN1 | 46 | H-bridge IN1 / AIN1 |
| Motor IN2 | 3 | H-bridge IN2 / AIN2 |
| Motor EN | 9 | H-bridge EN / ENA / PWMA (remove the L298N ENA jumper) |
| I2S SCK | 17 | PCM5102 SCK |
| I2S BCK | 16 | PCM5102 BCK |
| I2S WS | 7 | PCM5102 LCK / LRCK |
| I2S DIN | 15 | PCM5102 DIN |
| NeoPixels | 18 | First ring DIN. Chain that ring's DOUT to the second ring's DIN (32 LEDs) |

On this ESP32-S3, leave GPIO 26–37 alone. Those pins are connected to the module flash and PSRAM, and using them boot-loops the chip.

PCM5102 notes:

- Connect **SCK to GPIO 17**. Power the module **VIN from 5V**.
- Sound comes from **LOUT / ROUT** into a powered speaker or a small amplifier.
- If it stays silent, tie the **XSMT / MUTE** pad to 3.3V.

H-bridge notes:

- Motor power goes to the driver supply pins, not through the ESP32.
- EN is GPIO 9. Boot logs `EN GPIO 9 OK` when that pin goes high, then low.
- If the prop moves the wrong way, set `MOTOR_SWAP_DIRECTION` to `1`.
- If an L298N motor feels weak, set `MOTOR_PWM_HZ` to `1000`.

## Screams

Put 44.1 kHz, 16-bit, mono WAV files in `screams/`. The build packs every `.wav` in that folder and flashes them with the firmware. Each trigger plays one of them at random.

## Build and flash

From this directory, with ESP-IDF exported:

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

The PIR is ignored for 20 seconds after boot. An HC-SR312 settles in a few seconds, so `PIR_WARMUP_MS` can be lowered. Set `SCARE_ONCE_ON_BOOT` to `1` to fire one scare at the end of that wait without waving at the sensor.
