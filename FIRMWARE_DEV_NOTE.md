# ESP32-C5 CYD Firmware Build Requirements and Workflow

This note follows the local workflow style from `..\esp32-cyd-2.8\FIRMWARE_DEV_NOTE.md`, adapted for the ESP32-C5 CYD 2.8 inch board in this project.

Last verified: 2026-06-24

## Project

| Item | Value |
| --- | --- |
| Project root | `C:\Users\stell\OneDrive\Documents\app\esp32-cyd-2.8-c5` |
| Firmware entry point | `src\main.cpp` |
| PlatformIO config | `platformio.ini` |
| Internet radio roadmap | `INTERNET_RADIO_PLAN.md` |
| PlatformIO environment | `nm_cyd_c5_wifi_test` |
| Target board profile | `esp32-c5-devkitc-1` |
| Hardware | ESP32-C5 CYD / likely NM-CYD-C5 |
| Framework | Arduino |
| UI | LVGL v8.3.11 on ST7789 via Arduino_GFX |
| Serial port | `COM9` |

## Local Tool Paths

PlatformIO is installed in the user PlatformIO virtual environment, but `pio` is not available on the current PowerShell PATH.

Use the explicit PlatformIO path:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" --version
```

Known local paths:

| Tool | Path / command | Observed version |
| --- | --- | --- |
| PlatformIO CLI | `C:\Users\stell\.platformio\penv\Scripts\pio.exe` | PlatformIO Core 6.1.18 |
| PlatformIO Python | `C:\Users\stell\.platformio\penv\Scripts\python.exe` | Python 3.11.7 |
| esptool | `C:\Users\stell\.platformio\penv\Scripts\esptool.exe` | esptool 5.1.0 via pioarduino package |
| PATH Python alias | `C:\Users\stell\AppData\Local\Microsoft\WindowsApps\python.exe` | Store alias; do not use for this firmware workflow |

Set UTF-8 mode before PlatformIO/esptool commands so Windows console output does not crash on progress characters:

```powershell
$env:PYTHONUTF8 = "1"
```

## Current PlatformIO Notes

The pioarduino ESP32-C5 platform is pinned in `platformio.ini`:

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.36/platform-espressif32.zip
```

This platform expects PlatformIO Core `6.1.18`. A newer `6.1.19` core was present earlier, and PlatformIO may warn:

```text
Obsolete PIO Core v6.1.18 is used (previous was 6.1.19)
```

Keep using the explicit `pio.exe` path until the user PlatformIO install is cleaned up.

If pioarduino dependency bootstrap fails with a package-name mismatch for the pioarduino PlatformIO fork, manually install the ESP helper dependencies while excluding the broken self-reinstall entry:

```powershell
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& "$env:USERPROFILE\.platformio\penv\Scripts\uv.exe" pip install --python=$py --upgrade littlefs-python>=0.16.0 fatfs-ng>=0.1.14 pyyaml>=6.0.2 rich-click>=1.8.6 zopfli>=0.2.2 intelhex>=2.3.0 rich>=14.0.0 "urllib3<2" cryptography>=45.0.3 certifi>=2025.8.3 ecdsa>=0.19.1 bitstring>=4.3.1 "reedsolo>=1.5.3,<1.8" esp-idf-size>=2.0.0 esp-coredump>=1.14.0
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install "click==8.1.7"
```

## Firmware Build

Build from the project root:

```powershell
$env:PYTHONUTF8 = "1"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e nm_cyd_c5_wifi_test
```

Observed successful build:

| Metric | Value |
| --- | --- |
| Platform | Espressif 32 `55.3.36` / pioarduino |
| Arduino framework | `3.3.6` |
| Toolchain | `toolchain-riscv32-esp @ 14.2.0+20251107` |
| RAM | about 39.5% used |
| Flash | about 31.1% of the 4 MB app partition |

The LVGL firmware exceeded the default OTA app partition size:

```text
Image length 1333200 doesn't fit in partition length 1310720
```

`platformio.ini` now points to `partitions.csv`, which gives the factory app a `0x300000` / 3 MB slot and leaves the rest of the 16 MB flash for SPIFFS.

## LVGL Setup

The LVGL setup follows `..\esp32-cyd-2.8`:

- `lvgl/lvgl@8.3.11`
- `-I include`
- `-D LV_CONF_INCLUDE_SIMPLE`
- `-D LV_CONF_PATH=lv_conf.h`
- compact `include\lv_conf.h`
- manual `lv_tick_inc(...)` plus `lv_timer_handler()`

The C5-specific display path uses `moononournation/GFX Library for Arduino@1.6.6` instead of TFT_eSPI, because this board needs the ESP32-C5 Arduino display path and the vendor pinout:

| Signal | GPIO |
| --- | --- |
| TFT SCK | `6` |
| TFT MISO | `2` |
| TFT MOSI | `7` |
| TFT CS | `23` |
| TFT DC | `24` |
| TFT RST | `-1` |
| Backlight | `25` |

The screen draws a live Wi-Fi status panel with state, SSID, BSSID, channel, RSSI, IPv4, and a progress/status bar.

## Flash Workflow

PlatformIO can build the image, but direct `pio run -t upload` is not the reliable path for this board right now.

Observed upload issues:

- Upload at `921600` baud reached the chip but failed after switching baud with `Invalid head of packet`.
- Upload at `115200` baud with the default stub flasher reached the chip but failed when the stub tried to read flash ID.
- `--no-stub flash-id` worked and reported `16MB` flash.
- Direct `esptool` flash works when using `--no-stub`, fixed `--flash-size 16MB`, `115200` baud, and `PYTHONUTF8=1`.

Recommended flash command:

```powershell
$env:PYTHONUTF8 = "1"
.\scripts\flash-wifi-test.ps1
```

Equivalent direct command:

```powershell
$env:PYTHONUTF8 = "1"
& "$env:USERPROFILE\.platformio\penv\Scripts\esptool.exe" --chip esp32c5 --port COM9 --baud 115200 --no-stub --before default-reset --after hard-reset write-flash -z --flash-mode dio --flash-freq 80m --flash-size 16MB 0x2000 .pio\build\nm_cyd_c5_wifi_test\bootloader.bin 0x8000 .pio\build\nm_cyd_c5_wifi_test\partitions.bin 0xe000 "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin" 0x10000 .pio\build\nm_cyd_c5_wifi_test\firmware.bin
```

## Serial Monitor Workflow

Standard `pio device monitor` opened COM9 but showed no sketch output in this environment. The board did print when COM9 was opened with DTR and RTS released.

Use:

```powershell
.\scripts\monitor-com9.ps1
```

If COM9 is busy after an interrupted monitor/upload:

```powershell
Get-Process | Where-Object { $_.ProcessName -match 'pio|python' -and $_.Path -like "$env:USERPROFILE\.platformio\*" } | Stop-Process -Force
```

## Verified Wi-Fi Result

The flashed test firmware successfully proved 5 GHz Wi-Fi operation:

| Item | Value |
| --- | --- |
| SSID | `Innovation Lab` |
| Selected BSSID | `3E:3F:1B:F3:53:5E` |
| Channel | `157` / 5 GHz |
| RSSI | about `-48 dBm` |
| IPv4 | `10.17.184.109` |
| Gateway | `10.128.128.128` |
| DNS | `10.128.128.128` |

Latest LVGL firmware verification after flashing the partition-fixed image on 2026-06-24:

```text
ESP32-C5 5 GHz Wi-Fi connection test
Selected 5 GHz AP: channel=157 BSSID=3E:3F:1B:F3:53:5E RSSI=-47 dBm
Wi-Fi connected.
SSID: Innovation Lab
BSSID: 3E:3F:1B:F3:53:5E
Channel: 157 (5 GHz)
RSSI: -48 dBm
IPv4: 10.17.184.109
Still connected on channel 157 with RSSI -48 dBm
```

Serial output also showed the chip feature line:

```text
Features: Wi-Fi 6 (dual-band), BT 5 (LE), IEEE802.15.4, Single Core + LP Core, 240MHz
```

## PlatformIO Firmware Specs

From `platformio.ini`:

```ini
[env:nm_cyd_c5_wifi_test]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.36/platform-espressif32.zip
board = esp32-c5-devkitc-1
framework = arduino
board_build.flash_size = 16MB
board_build.partitions = partitions.csv
monitor_speed = 115200
upload_speed = 115200
upload_port = COM9
monitor_port = COM9
upload_flags =
  --no-stub
lib_deps =
  lvgl/lvgl@8.3.11
  moononournation/GFX Library for Arduino@1.6.6
build_flags =
  -I include
  -D LV_CONF_INCLUDE_SIMPLE
  -D LV_CONF_PATH=lv_conf.h
  -D ARDUINO_USB_CDC_ON_BOOT=0
  -D ARDUINO_USB_MODE=0
```

## Generated Artifacts

These should stay out of Git:

```text
.pio/
*.bin
*.elf
*.map
*.log
src/secrets.h
```
