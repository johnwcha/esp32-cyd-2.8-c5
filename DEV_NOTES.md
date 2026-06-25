# ESP32-C5 CYD 2.8 Inch Touch Screen Dev Notes

Last researched: 2026-06-24

## Connected Board

- Windows currently sees the attached board on `COM9` as `USB-SERIAL CH340 (COM9)`.
- Another CH340 serial device was also present on `COM10`, so use `COM9` explicitly when flashing or opening a serial monitor.

## Likely Board Identity

The board matches the NM-CYD-C5 / ESP32-C5 Cheap Yellow Display family:

- Board name: `NM-CYD-C5`
- MCU module: `ESP32-C5-WROOM-1`
- Memory: `16 MB flash + 8 MB PSRAM`
- Display: 2.8 inch TFT, `240x320` / `320x240`
- Default LCD driver: `ST7789`; board documentation says it can also be changed to `ILI9341`
- Touch: resistive touch, documented with `XPT2046` in the pinout notes
- USB: two USB-C ports:
  - native ESP32-C5 USB Type-C
  - USB-C to UART via `CH340`
- Storage: microSD slot
- Extras: WS2812 RGB LED, backlight control, GPIO headers, 12-pin FPC expansion connector
- Compatibility claim: intended as a replacement/upgrade for the classic ESP32 `ESP32-2432S028R` CYD form factor/interface

## ESP32-C5 SoC Notes

- CPU: 32-bit RISC-V, up to 240 MHz
- Wireless:
  - 2.4 GHz and 5 GHz Wi-Fi 6 / 802.11ax
  - compatible with 802.11a/b/g/n/ac
  - Bluetooth LE
  - IEEE 802.15.4 for Zigbee and Thread
- Espressif datasheet summary:
  - single high-performance RISC-V processor plus low-power RISC-V processor
  - external flash and PSRAM support
  - 29 GPIOs
  - rich peripherals including UART, SPI, I2C, I2S, LED PWM, RMT, ADC, USB Serial/JTAG, CAN FD, GDMA, and PARLIO

## Board Pinout

The RockBase/NM-CYD-C5 documentation says the LCD, touch, and SD card share one SPI bus:

| Device | SCK | MISO | MOSI | CS | IRQ |
| --- | ---: | ---: | ---: | ---: | --- |
| Display | IO6 | IO2 | IO7 | IO23 | none |
| Touch | IO6 | IO2 | IO7 | IO1 | none listed |
| SD card | IO6 | IO2 | IO7 | IO10 | none |

Additional pins:

| Function | Pin |
| --- | --- |
| Display backlight | IO25 |
| WS2812 RGB LED | IO27 |
| GPS LP-UART RX | IO4 |
| GPS LP-UART TX | IO5 |

I2C expansion header `CN1`:

| Header pin | Signal |
| ---: | --- |
| 1 | 3.3V |
| 2 | IO9 |
| 3 | IO8 |
| 4 | GND |

Expansion header `P1`:

| Header pin | Signal |
| ---: | --- |
| 1 | IO4 |
| 2 | IO8 |
| 3 | IO26 |
| 4 | GND |

12-pin FPC expansion `FPC2`:

| FPC pin | Signal |
| ---: | --- |
| 1 | IO2 |
| 2 | IO6 |
| 3 | IO7 |
| 4 | IO10 |
| 5 | GND |
| 6 | IO4 |
| 7 | IO8 |
| 8 | IO5 |
| 9 | IO9 |
| 10 | USB D- |
| 11 | USB D+ |
| 12 | GND |

## Internet Radio Audio Hardware

Use the same audio hardware as `..\ava-and-work\hardware.md`:

- amplifier: `MAX98357` / `MAX98357A` I2S 3 W Class D amplifier breakout
- speaker: 3 W, 8 ohm mini speaker

Provisional ESP32-C5 CYD to MAX98357A wiring:

| MAX98357A pin | ESP32-C5 CYD signal | ESP32-C5 GPIO / source | Notes |
| --- | --- | --- | --- |
| `BCLK` | I2S bit clock | `IO8` | Candidate pin on `CN1` pin 3 and `P1` pin 2. |
| `LRC` / `LRCLK` / `WS` | I2S word select | `IO9` | Candidate pin on `CN1` pin 2. |
| `DIN` | I2S audio data from ESP32 | `IO26` | Candidate pin on `P1` pin 3. |
| `GND` | Ground | `GND` | Must share ground with the ESP32-C5 board. |
| `VIN` / `VCC` | Amp power | Prefer `5V` if available | 5 V gives the 3 W amp more headroom; logic pins remain 3.3 V GPIO signals. |
| Speaker `+` | Speaker positive output | MAX98357A output only | Do not connect to ESP32 pins. |
| Speaker `-` | Speaker negative output | MAX98357A output only | Do not connect to ESP32 ground. |

Current firmware audio diagnostic:

- Boot and Play run a 20% volume I2S tone sweep across three GPIO maps.
- Listen for which on-screen/serial label produces sound:
  - `Dev note IO8/IO9/IO26`: `BCLK=IO8`, `LRC=IO9`, `DIN=IO26`
  - `P1 header IO8/IO4/IO26`: `BCLK=IO8`, `LRC=IO4`, `DIN=IO26`
  - `Clock swap IO9/IO8/IO26`: `BCLK=IO9`, `LRC=IO8`, `DIN=IO26`
- If none of the three maps produces sound, check MAX98357A `VIN`, `GND`, optional `SD` shutdown pin, and speaker output terminals before changing stream-decoder code.

Safety notes:

- Do not connect either speaker lead to ESP32 ground.
- Do not connect the speaker to the MAX98357A logic/power row labeled `LRC`, `BCLK`, `DIN`, `GAIN`, `SD`, `GND`, or `VIN`.
- The speaker connects only to the separate MAX98357A speaker output pads or terminals.
- Confirm the actual board header pin locations before applying power.
- Avoid the display/touch/SD shared SPI bus pins for I2S: `IO6`, `IO2`, `IO7`, display CS `IO23`, touch CS `IO1`, and SD CS `IO10`.

## Firmware / Build Notes

- See `FIRMWARE_DEV_NOTE.md` for the local build, flash, and serial-monitor workflow. It follows the pattern from `..\esp32-cyd-2.8\FIRMWARE_DEV_NOTE.md`.
- Use a recent Espressif Arduino/platform package. The NM-CYD-C5 README recommends `espressif32` library version `3.3.5` or newer, and shows a PlatformIO example using Arduino `3.3.6`.
- For `TFT_eSPI`, the NM-CYD-C5 docs mention adding `TFT_eSPI_ESP32_C5.c/h` processor support and updating `TFT_eSPI.c/h` with `CONFIG_IDF_TARGET_ESP32C5`.
- Because display, touch, and SD share SPI, firmware must manage chip-select lines carefully:
  - display CS: `IO23`
  - touch CS: `IO1`
  - SD CS: `IO10`
- For graphics work, start with `ST7789` and `240x320`; try `ILI9341` only if the panel does not initialize correctly.
- Touch should be treated as resistive/XPT2046 and calibrated in software.

## Notes and Caveats

- Vendor docs describe wireless as "Wi-Fi 6(802.11az)" in one table, but Espressif's datasheet identifies ESP32-C5 Wi-Fi 6 as `802.11ax`; treat `802.11az` in vendor text as likely a typo.
- Vendor/GitHub board docs describe Bluetooth as "BLE 5.3" or "Bluetooth 5"; Espressif's current ESP32-C5 datasheet says Bluetooth LE is Bluetooth Core 6.0 certified. Use Espressif docs when exact protocol certification matters.
- The classic CYD `ESP32-2432S028R` commonly uses a 2.8 inch ILI9341 240x320 resistive display, but the C5 upgrade documentation lists ST7789 as the default. Do not assume old CYD pin definitions will work unchanged.

## Sources

- RockBase NM-CYD-C5 GitHub: https://github.com/RockBase-iot/NM-CYD-C5
- NMMiner NM-CYD-C5 product page: https://www.nmminer.com/product/nm-cyd-c5/
- Espressif ESP32-C5 datasheet: https://documentation.espressif.com/esp32-c5_datasheet_en.pdf
- Classic CYD community repo for comparison: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display
- Classic CYD specs/pinout background: https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/
