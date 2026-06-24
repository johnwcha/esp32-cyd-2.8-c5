# ESP32-C5 CYD Internet Radio Plan

Last updated: 2026-06-24

## Goal

Build an internet radio receiver on the ESP32-C5 CYD 2.8 inch touch display:

- connect to Wi-Fi, preferably 5 GHz when available
- show available stations on the touch screen
- select a station from the display
- stream and decode internet radio audio
- output audio to an external DAC/amp and speaker
- show playback status, station name, signal/network state, and stream metadata where available

## Feasibility

This board is a good fit for a compact internet radio receiver, but it is not a complete audio board by itself.

Useful onboard resources:

- ESP32-C5-WROOM-1, 240 MHz RISC-V CPU
- 2.4 GHz and 5 GHz Wi-Fi 6
- 16 MB flash and 8 MB PSRAM
- 2.8 inch 240x320 TFT display
- resistive touch controller
- microSD slot
- I2S peripheral on the ESP32-C5
- exposed GPIO/FPC/header pins

Required external hardware:

- MAX98357 / MAX98357A I2S 3 W Class D amplifier breakout, matching `..\ava-and-work\hardware.md`
- 3 W, 8 ohm mini speaker, matching `..\ava-and-work\hardware.md`
- stable 5 V supply sized for the display, Wi-Fi, and amplifier

Selected audio module:

- MAX98357A I2S mono amplifier module, because it combines DAC and small speaker amplifier and needs only BCLK, LRCLK/WS, DIN, power, and GND.

Higher-quality alternative:

- PCM5102A I2S DAC into a separate amplifier or powered speaker.

## Current Baseline

The project already has a verified PlatformIO/Arduino firmware base:

- LVGL v8.3.11 display setup
- Arduino_GFX ST7789 output
- successful connection to `Innovation Lab` on 5 GHz channel 157
- serial workflow on `COM9`
- custom 16 MB flash partition layout with a larger app slot

This baseline should be preserved while the radio is added in stages.

## Proposed Firmware Architecture

Keep the firmware split into small responsibilities:

| Module | Responsibility |
| --- | --- |
| Display driver | ST7789 + LVGL flush/tick setup |
| Touch driver | XPT2046 read, calibration, LVGL input device |
| Wi-Fi manager | connect, reconnect, expose RSSI/channel/IP |
| Station model | station list, URL, codec preference, label/logo metadata |
| Radio player | start/stop stream, decode audio, track playback state |
| Audio output | I2S pin setup, DAC/amp output, volume/mute |
| UI screens | station list, now playing, network/audio status |

Early code can keep these as separate `.h/.cpp` files once the first proof grows past the current single-file sketch.

## Coding Phases

### Phase 0: Preserve Baseline

Purpose: keep the current verified Wi-Fi/LVGL firmware recoverable while development starts.

Code tasks:

- keep the current build, flash, and monitor scripts unchanged
- keep `src\secrets.h` local-only and continue using `src\secrets.example.h`
- add comments/constants for the planned audio pins, but do not enable I2S yet
- verify the current firmware still builds before each major phase

Acceptance:

- `pio run -e nm_cyd_c5_wifi_test` succeeds
- current LVGL Wi-Fi status screen still boots
- serial still confirms 5 GHz Wi-Fi connection

### Phase 1: Refactor Into Modules

Purpose: turn `src\main.cpp` into a radio-friendly structure without changing behavior.

Planned files:

| File | Responsibility |
| --- | --- |
| `src\main.cpp` | setup/loop orchestration only |
| `src\display_app.h/.cpp` | Arduino_GFX, LVGL init, tick/handler, flush |
| `src\wifi_manager.h/.cpp` | scan/connect/reconnect/status snapshot |
| `src\radio_ui.h/.cpp` | LVGL screen creation and status updates |
| `src\board_pins.h` | display, touch, SD, and planned I2S pin constants |

Acceptance:

- no behavior change from current firmware
- same Wi-Fi status appears on the screen
- same serial diagnostics are printed

### Phase 2: Add Touch Input

Purpose: make LVGL respond to the CYD touch panel before building the station selector.

Planned files:

| File | Responsibility |
| --- | --- |
| `src\touch_input.h/.cpp` | XPT2046 SPI touch read, calibration, LVGL indev callback |
| `src\board_pins.h` | touch CS and shared SPI pin constants |

Code tasks:

- initialize XPT2046 on shared SPI pins: SCK `IO6`, MISO `IO2`, MOSI `IO7`, CS `IO1`
- keep display CS and SD CS inactive while reading touch
- add basic calibration constants
- add a small touch diagnostic label or serial log during development

Acceptance:

- touch events register in LVGL
- a test button/list item can be tapped reliably
- display does not flicker or corrupt during touch reads

### Phase 3: Station UI With Simulated Player

Purpose: make the board behave like a touchscreen radio before audio hardware is connected.

Planned files:

| File | Responsibility |
| --- | --- |
| `src\station.h/.cpp` | compiled-in station list |
| `src\radio_player.h/.cpp` | fake player state machine |
| `src\radio_ui.h/.cpp` | station list, now-playing, controls |

Code tasks:

- define a small list of direct MP3 station URLs
- build a touchable station list
- build now-playing UI with station name, fake state, RSSI/channel/IP
- add previous, play/pause, next, and mute buttons
- simulate states: idle, connecting, buffering, playing, error

Acceptance:

- station selection works entirely from touch
- UI state changes are visible
- serial logs selected station name and URL
- no audio hardware required

### Phase 4: I2S Audio Smoke Test

Purpose: prove the MAX98357A and speaker wiring before streaming.

Planned files:

| File | Responsibility |
| --- | --- |
| `src\audio_output.h/.cpp` | ESP32-C5 I2S setup and tone generation |
| `src\board_pins.h` | MAX98357A BCLK/LRCLK/DIN pin constants |

Planned pin constants:

```cpp
constexpr int8_t kI2sBclk = 8;
constexpr int8_t kI2sLrclk = 9;
constexpr int8_t kI2sDout = 26;
```

Code tasks:

- configure I2S TX for the MAX98357A
- output a low-volume sine wave or simple tone
- add UI button or compile-time flag for tone test

Acceptance:

- audible clean test tone from the 3 W / 8 ohm speaker
- no display/touch/Wi-Fi regression
- speaker is confirmed connected only to amp output pads

### Phase 5: First Real Stream

Purpose: play one hardcoded internet radio MP3 stream.

Planned files:

| File | Responsibility |
| --- | --- |
| `src\radio_player.h/.cpp` | stream start/stop and player state |
| `src\audio_output.h/.cpp` | I2S output integration |
| `platformio.ini` | audio library dependency if compatible |

Code tasks:

- evaluate `schreibfaul1/ESP32-audioI2S` with ESP32-C5 Arduino
- start with one plain HTTP MP3 stream
- update UI with connecting, buffering, playing, and error states
- keep LVGL updates lightweight during playback

Acceptance:

- one station plays for at least 10 minutes
- stop/play control works
- Wi-Fi interruption produces a visible error/reconnect state

### Phase 6: Full Station Selector

Purpose: connect the station UI to real playback.

Code tasks:

- start selected station from the touch list
- stop current stream before switching stations
- add previous/next controls
- store last selected station in NVS
- add stream title/ICY metadata if the selected library exposes it

Acceptance:

- multiple stations can be selected from the touch screen
- last station resumes after reboot
- failed stream does not require power cycling

## Station Data

Start with a compiled-in station list so the first version is deterministic:

```cpp
struct Station {
  const char *name;
  const char *url;
  const char *codec;
};
```

Initial UI station list:

| Station | UI label / format | Starter URL |
| --- | --- | --- |
| KEXP | `AAC 160k` | `https://kexp.streamguys1.com/kexp160.aac` |
| SomaFM Groove Salad | `MP3 128k` | `https://ice5.somafm.com/groovesalad-128-mp3` |
| Radio Paradise | `MP3` | `https://stream.radioparadise.com/mp3-128` |
| BBC World Service | `MP3` | `http://stream.live.vc.bbcmedia.co.uk/bbc_world_service` |
| SomaFM Live | `MP3 128k` | `https://ice5.somafm.com/live-128-mp3` |

Sources checked for the initial station links:

- KEXP streaming URLs: https://www.kexp.org/streaming-urls/
- SomaFM Groove Salad direct stream links: https://somafm.com/groovesalad/directstreamlinks.html
- SomaFM Live direct stream links: https://somafm.com/live/directstreamlinks.html
- Radio Paradise stream links: https://radioparadise.com/listen/stream-links
- BBC World Service stream reference: https://gist.github.com/bpsib/67089b959e4fa898af69fea59ad74bc3

Later options:

- load `stations.json` from microSD
- expose a simple web config page on the ESP32
- store the selected favorite station in NVS
- add station logos from flash or microSD

First test streams should be:

- plain HTTP when possible
- MP3 at modest bitrate, ideally 64-128 kbps
- direct stream URLs, not playlist pages

HTTPS and AAC can come later after MP3/HTTP is stable.

## UI Plan

Use LVGL v8 and keep the interface efficient:

- station list screen with large touch rows
- now-playing screen with station name, stream state, RSSI/channel, IP, and volume
- bottom control bar: previous, play/pause, next, mute/volume
- status area for buffering, connecting, reconnecting, and decoder errors

Avoid heavy animations during playback. The ESP32-C5 is single-core, so smooth audio is more important than decorative UI.

## Audio Plan

Target pipeline:

```text
Wi-Fi stream -> stream buffer -> decoder -> I2S DMA -> DAC/amp -> speaker
```

Preferred library path to evaluate:

- `schreibfaul1/ESP32-audioI2S`

Reasons:

- designed for ESP32 internet audio
- supports MP3 and other common codecs
- intended for external I2S DAC/amp modules such as MAX98357A
- has mature station-stream examples

Risk:

- ESP32-C5 Arduino support is newer than classic ESP32/S3, so the library may require small compatibility fixes or a fallback ESP-IDF I2S path.

Fallback path:

- implement or adapt an ESP-IDF I2S output layer for ESP32-C5
- start with MP3 only using a known decoder path
- keep LVGL/touch update frequency low during playback

## Provisional I2S Pins

Do not use the shared display/touch/SD SPI bus pins unless absolutely necessary:

| Existing function | GPIO |
| --- | --- |
| SPI SCK | `IO6` |
| SPI MISO | `IO2` |
| SPI MOSI | `IO7` |
| Display CS | `IO23` |
| Touch CS | `IO1` |
| SD CS | `IO10` |
| Backlight | `IO25` |
| WS2812 | `IO27` |

Candidate exposed pins for audio:

| I2S signal | Candidate GPIO |
| --- | --- |
| BCLK | `IO8` |
| LRCLK/WS | `IO9` |
| DOUT/DIN to amp | `IO26` |

Alternate candidates if the physical header layout is easier:

- `IO4`
- `IO5`
- `IO8`
- `IO9`
- `IO26`

Final pin choice should wait until the amp/DAC module is connected and the board headers are confirmed with a meter or the vendor schematic.

## Hardware Bring-Up Checklist

Before connecting the amplifier:

- verify the board still boots and shows Wi-Fi status
- identify the chosen GPIO pins on the header/FPC
- confirm available power rails on the header
- decide whether the amp module should be powered from 3.3 V or 5 V/VIN

When the amp arrives:

- wire GND common between board and amp
- wire I2S BCLK, LRCLK/WS, and DATA
- wire amp `VIN`/`VCC`; prefer 5 V if available for speaker headroom, while keeping I2S logic at 3.3 V GPIO levels
- keep speaker volume low for first boot
- run a generated sine-wave I2S test before streaming radio

Speaker safety:

- connect the speaker only to the MAX98357A speaker output pads/terminals
- do not connect either speaker lead to ESP32 ground
- do not connect the speaker to the logic/power pin row labeled `LRC`, `BCLK`, `DIN`, `GAIN`, `SD`, `GND`, or `VIN`

## Implementation Milestones

### Milestone 1: UI Foundation

- refactor current single-file firmware into display, Wi-Fi, and UI modules
- add touch input driver and calibration constants
- replace Wi-Fi status card with a station-list/now-playing shell
- keep the existing Wi-Fi connection proof visible in the UI

Acceptance:

- display boots
- touch selection works
- Wi-Fi connects to `Innovation Lab`
- no audio hardware required

### Milestone 2: Radio State Machine Without Audio

- add station list model
- add selected station state
- add fake player states: idle, connecting, buffering, playing, error
- use UI buttons to switch stations and update status

Acceptance:

- station selection works by touch
- UI shows realistic playback state changes
- serial logs station URL and state transitions

### Milestone 3: I2S Hardware Smoke Test

- add I2S pin configuration
- output sine wave or simple tone to the external amp/DAC
- add mute/volume UI controls if supported by software output

Acceptance:

- audible clean tone from speaker
- no display/touch regression
- no Wi-Fi regression

### Milestone 4: First Internet Stream

- integrate audio streaming library
- connect to one hardcoded MP3 stream
- output decoded audio over I2S
- show buffering/playing/error state in LVGL

Acceptance:

- one station plays continuously for at least 10 minutes
- UI remains responsive enough to stop playback
- reconnect or error is visible when Wi-Fi is interrupted

### Milestone 5: Touch Station Selector

- connect station list UI to actual audio playback
- stop current stream cleanly before starting another
- add previous/next controls
- add station persistence in NVS

Acceptance:

- user can select stations entirely from touch screen
- selected station resumes after reboot
- failed stream does not require power cycling

### Milestone 6: Polish

- add ICY metadata if supported by the stream/library
- add signal quality display
- optional microSD `stations.json`
- optional station logos
- optional brightness control

## Main Risks

- Audio library compatibility with ESP32-C5 Arduino may need fixes.
- Single-core CPU means UI work must not block audio decoding.
- Some internet radio URLs are HTTPS, playlists, redirects, or unsupported codecs.
- Display, touch, and SD share SPI, so SD reads should be tested carefully during playback.
- GPIO availability depends on the exact board header/FPC access.

## First Development Step

Before the audio hardware arrives, implement Milestone 1 and Milestone 2:

1. split the current firmware into modules
2. add touch input to LVGL
3. build a station-selector UI
4. simulate player state changes
5. keep the current verified Wi-Fi connection logic

That will make the board feel like a radio before audio is wired, and it will reduce bring-up risk when the amp/speaker are added.

## Development Log

### 2026-06-24

- Added the first radio UI with five station rows: KEXP, SomaFM Groove Salad, Radio Paradise, BBC World Service, and SomaFM Live.
- Removed Wi-Fi diagnostics from the on-screen UI so the display reads as a radio interface.
- Removed the leftover LVGL progress bar from the bottom of the screen.
- Added an initial XPT2046 LVGL pointer driver using the shared SPI bus: SCK `IO6`, MISO `IO2`, MOSI `IO7`, touch CS `IO1`.
- Touch driver holds display CS `IO23` and SD CS `IO10` inactive while reading touch.
- Touch calibration starts with raw range `250..3800` for both axes and prints raw/calibrated coordinates to serial while pressed.
- Build and flash succeeded after adding touch support; boot still reconnects to `Innovation Lab` on 5 GHz.
