$ErrorActionPreference = "Stop"

$pioPython = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"

@'
import serial
import time

port = serial.Serial("COM9", 115200, timeout=0.5)
port.dtr = False
port.rts = False

print("listening on COM9 at 115200 with DTR/RTS released")
try:
    while True:
        data = port.readline()
        if data:
            print(data.decode(errors="replace").rstrip())
finally:
    port.close()
'@ | & $pioPython -
