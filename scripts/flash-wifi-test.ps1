$ErrorActionPreference = "Stop"

$env:PYTHONUTF8 = "1"

$projectRoot = Resolve-Path "$PSScriptRoot\.."
$pioPython = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
$esptool = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\esptool.exe"
$bootApp0 = Join-Path $env:USERPROFILE ".platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"
$buildDir = Join-Path $projectRoot ".pio\build\nm_cyd_c5_wifi_test"

& $esptool `
  --chip esp32c5 `
  --port COM9 `
  --baud 115200 `
  --no-stub `
  --before default-reset `
  --after hard-reset `
  write-flash `
  -z `
  --flash-mode dio `
  --flash-freq 80m `
  --flash-size 16MB `
  0x2000 (Join-Path $buildDir "bootloader.bin") `
  0x8000 (Join-Path $buildDir "partitions.bin") `
  0xe000 $bootApp0 `
  0x10000 (Join-Path $buildDir "firmware.bin")
