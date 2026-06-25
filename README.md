# ESP32-C5 CYD Wi-Fi Test Firmware

This firmware scans for `Innovation Lab`, selects the strongest 5 GHz BSSID, connects to that exact AP, and prints connection diagnostics over serial. If `Innovation Lab` is unavailable or cannot connect, it falls back to `TP-Link_ABD8`.

## Build and Flash

Use the local workflow in `FIRMWARE_DEV_NOTE.md`. In this environment, `pio` is not on PATH, so use the explicit PlatformIO path.

Build:

```powershell
$env:PYTHONUTF8 = "1"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e nm_cyd_c5_wifi_test
```

Flash:

```powershell
$env:PYTHONUTF8 = "1"
.\scripts\flash-wifi-test.ps1
```

Monitor:

```powershell
.\scripts\monitor-com9.ps1
```

The project is configured for `COM9` and `115200` baud in `platformio.ini`, but direct esptool flashing with `--no-stub` and fixed `16MB` flash size is currently the reliable path for this board.
Serial logging is configured for the CH340 USB-to-UART port, which is the port Windows reported as `COM9`.

## Expected Serial Output

The serial monitor should show:

- scan results for all visible networks
- `Innovation Lab` entries with channels above 14 marked as `5 GHz`
- fallback attempts for `TP-Link_ABD8` if the primary network is unavailable
- selected BSSID/channel/RSSI
- connection result with IP address

If it connects successfully to `Innovation Lab` and reports a channel above 14, the board is using 5 GHz Wi-Fi. The fallback network may connect on either 2.4 GHz or 5 GHz.

## Credentials

Local credentials live in `src/secrets.h`, which is ignored by Git. Use `src/secrets.example.h` as the template if you need to recreate it.
