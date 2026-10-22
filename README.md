# Halloween scare

ESP-IDF firmware for an ESP32 (or ESP32-S3). A PIR sensor starts this sequence:

1. The rings fade up to white while the motor runs forward (the prop lunges out).
2. The rings fade red while a scream plays through the PCM5102, then fade back to white.
3. It waits one second.
4. The motor runs back, still on white, then the rings fade off until the next trigger.

Pins, LED counts, and speed are in `main/config.h`. Open and close times start from the values in that file. Change them from the serial console; the new times are stored in flash and kept across reboots.

## Wiring

These pins are for a Freenove ESP32-S3. Use a common ground for every board. Had to run neopixels on 3V supply so the signal would latch.

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
| Status LED | 48 | Onboard NeoPixel. Amber while booting, blue in mass storage, green when armed, red during a scare |


PCM5102 notes:

- Connect **SCK to GPIO 17**. Power the module **VIN from 5V**.
- Sound comes from **LOUT / ROUT** into a powered speaker or a small amplifier.
- If it stays silent, tie the **XSMT / MUTE** pad to 3.3V.

H-bridge notes:

- Motor power goes to the driver supply pins, not through the ESP32.
- EN is GPIO 9. Boot logs `EN GPIO 9 OK` when that pin goes high, then low.
- If the prop moves the wrong way, set `MOTOR_SWAP_DIRECTION` to `1`.
- If an L298N motor feels weak, set `MOTOR_PWM_HZ` to `1000`.

## Open and close times

Type these on the board's UART at 115200 baud. USB Serial/JTAG is off, so the monitor has to be on that UART. A scare already running keeps the times it started with. The next scare uses the new ones.

```
out            print the open time
out 10000      drive open for 10000 ms, and save it
in             print the close time
in 11000       drive shut for 11000 ms, and save it
times          print both
help           list commands
scare          run one scare
open           drive open for the saved time, then hold with the sensor off
reset          drive shut for the saved time, then arm the sensor
ble            print each companion phone and its counter
ble clear      forget those phones
```

`open` uses the saved open time and leaves the motor braked. The motion sensor does nothing until `reset`, which runs the saved close time and turns the sensor back on.

Allowed range is 100 to 120000 ms. With nothing saved yet, the times are `MOTOR_OUT_MS` and `MOTOR_BACK_MS`.

## Flash

`partitions.csv` is the 16 MB prop. `partitions_8mb.csv` is the 8 MB bench chip. The image header has to match the chip or startup asserts before the app runs. This tree is set for the bench chip. For the prop, set flash size to 16 MB and the partition file to `partitions.csv`.

| Partition | 8 MB bench | 16 MB prop | Use |
| --- | --- | --- | --- |
| nvs | 24 KB | 24 KB | Saved open and close times |
| factory | 1984 KB | 1984 KB | The application |
| storage | 6 MB | 14 MB | USB drive. Screams and the other files go here |
| phy_init | 4 KB | 4 KB | Radio calibration data |

The map moved, so erase once before flashing this build: `idf.py erase-flash`. Saved open and close times do not come along.

## USB

The native USB connector (GPIO 19 and 20) is the mass-storage port. At boot the firmware waits 3 seconds. If a computer enumerates the board, the `storage` partition shows up as a drive and the scare stays off. The first time, the computer may ask to format that drive. Reset with the cable unplugged to arm the prop. A USB charger that never enumerates a device leaves the scare running.

## Companion app

The Android app is in `android/`. Open that folder in Android Studio. Scare, Open, and Reset each broadcast for two seconds.

The board listens. It does not advertise. Ignored traffic stays off the console. A hit logs the command name.

The phone id is the first 4 bytes of SHA-256 over Android's secure id. The board keeps that id and the highest counter accepted for it, up to 4 phones, until the next boot. A repeat of an old number is dropped for that boot. The tag is the first 8 bytes of HMAC-SHA1 over the magic, the command, the phone id, and the counter, using `BLE_KEY_BYTES`. `ble clear` forgets the phones for this boot. The phone keeps its own count across coffin reboots. If that count runs past 32 bits, the phone starts at 1 again, and the coffin has to boot once before it accepts the new count.

```
FF FF                company id 0xFFFF, little-endian
C0 FF 1E 57          magic
01                   command: 01 scare, 02 open, 03 reset
II II II II          phone id
NN NN NN NN          counter, little-endian
TT TT TT TT TT TT TT TT   tag
```

`open` and `reset` match the console commands. `scare` runs one scare.

## Screams

Copy 44.1 kHz, 16-bit, mono WAV files into a `screams` folder on the storage drive. Each trigger plays one of them at random. Other files can live on that same drive, next to the folder. Creaks go in `creaks` and use the same format.

```bash
ffmpeg -i input.mp3 -ac 1 -ar 44100 -c:a pcm_s16le output.wav
```

The scripts write `scream01.wav`, `scream02.wav`, and so on into the folder you give them. The second argument is that prefix. The original files stay put.

```bash
tools/convert_clips.sh raw/screams scream
```

```bat
tools\convert_clips.bat raw\creaks creak
```

## Build and flash

From this directory, with ESP-IDF exported:

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

`tools/prepare_image.sh` merges that build into one file, `build/coffin.bin`. The bootloader, partition table, and app are already placed inside it. `tools/flash_board.sh build/coffin.bin` flashes that file. A port can follow the file if more than one board is plugged in. The flash size in the file is whatever this build was set to.

The PIR is ignored for 20 seconds after boot. An HC-SR312 settles in a few seconds, so `PIR_WARMUP_MS` can be lowered. Set `SCARE_ONCE_ON_BOOT` to `1` to fire one scare at the end of that wait without waving at the sensor.

## License

Copyright (C) 2026 [Your Name]

This project is licensed under the GNU General Public License
v3.0 (GPL-3.0-only). See [LICENSE](LICENSE) for details.

This project uses third-party GPL-licensed software included
as a Git submodule. The original copyright and license
notices are retained in that submodule.