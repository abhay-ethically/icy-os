# Icy OS Web OS for ESP32 WROOM-1

A Windows-style dark Web OS desktop that runs on an ESP32 WROOM-1. The ESP boots as a Wi-Fi access point (`Icy-OS`) and serves a single-page `index.html` from a MicroSD card over SPI. All live data, commands, and settings are exchanged over one WebSocket (`/ws`).

## Features

- **Desktop**: dark theme, taskbar, clock, Start menu, draggable/minimizable/closable windows.
- **System Status**: live free heap, uptime, connected stations, SD status, and last scan RSSI.
- **Terminal**: WebSocket command console with `help`, `sysinfo`, `ls`, `wifi scan`, `beep`, `ota`, `reboot`, `settings get`.
- **File Manager**: browse the MicroSD card with file sizes.
- **Wi-Fi Scanner**: table of nearby SSIDs with RSSI bars, channel, and security; auto-refresh every 30 s while the window is open.
- **Settings**: persist AP SSID, AP password, admin password, and buzzer GPIO to `settings.json` on the SD card.
- **User authentication**: login screen; the admin password is used as the WebSocket token.
- **Buzzer**: beeps on `beep` command and on invalid terminal input.
- **OTA**: HTTP `POST /update?token=<admin-password>` to upload a new firmware `.bin`.

## Hardware wiring

### ESP32 WROOM-1 to MicroSD SPI module

| SD module | ESP32 (VSPI) | GPIO |
|-----------|--------------|------|
| CS        | SS           | 5    |
| SCK       | SCK          | 18   |
| MOSI      | MOSI         | 23   |
| MISO      | MISO         | 19   |
| VCC       | 3.3 V        | 3.3V |
| GND       | GND          | GND  |

- Use a **3.3 V** MicroSD module or a level-shifter; do not feed 5 V to the card.
- Keep wires short, especially at higher SPI speeds.

### Buzzer (optional)

- Connect a passive buzzer between **GPIO 25** and GND through a 100–330 Ω resistor.
- The GPIO can be changed in the Settings app and saved to `settings.json`.

## Software requirements

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- ESP32-Dev board target in `platformio.ini`
- Dependencies (auto-downloaded by PlatformIO):
  - `ESPAsyncWebServer`
  - `AsyncTCP`
  - `ArduinoJson`

## Build and flash

1. **Open the project:**
   ```bash
   cd "Icy OS"
   ```

2. **Build the firmware:**
   ```bash
   pio run
   ```

3. **Upload the firmware to the ESP32:**
   ```bash
   pio run --target upload
   ```

4. **Prepare the MicroSD card:**
   - Format the card as **FAT32**.
   - Copy `data/index.html` to the card root.
   - Optionally add a `wallpaper.jpg` to the root for a custom desktop background; if missing, a dark gradient is used as a fallback.
   - Insert the card into the module and power the ESP32.

5. **Connect and use:**
   - On your phone or PC, join the `Icy-OS` Wi-Fi network with password `Password123` (default).
   - Open a browser and go to `http://192.168.4.1`.
   - Log in with username `admin` and password `admin` (default; change in Settings).

## First run

If `settings.json` does not exist on the SD card, the firmware creates one with these defaults:

```json
{
  "ssid": "Icy-OS",
  "password": "Password123",
  "adminPass": "admin",
  "buzzerGPIO": 25
}
```

You can edit the JSON directly on the SD card before the first boot, or use the Settings app and reboot.

## OTA update

1. Build a new firmware with `pio run`.
2. The compiled `.bin` is typically at `.pio/build/esp32dev/firmware.bin`.
3. Upload it from any device connected to `Icy-OS`:
   ```bash
   curl -X POST -F 'file=@.pio/build/esp32dev/firmware.bin' \
     'http://192.168.4.1/update?token=admin'
   ```
   Replace `admin` with your current admin password.

You can also use the `ota` command in the Terminal to see the endpoint URL.

## Terminal commands

| Command | Description |
|---------|-------------|
| `help` | Show all commands. |
| `sysinfo` | Show a one-shot system status dump. |
| `ls [path]` | List files and directories on the SD card. |
| `wifi scan` | Manually trigger a Wi-Fi scan. |
| `beep [ms]` | Sound the buzzer for the given milliseconds (default 200). |
| `settings get` | Show current settings. |
| `ota` | Show the OTA upload URL. |
| `reboot` | Restart the ESP32. |

## Notes

- The ESP32 is both an access point and a station (`WIFI_AP_STA`) so it can perform real `WiFi.scanNetworks()` scans. Active scanning briefly hops channels and may cause small AP hiccups; this is normal.
- All traffic is unencrypted HTTP. Use only on a trusted local network.
- Keep `index.html` under ~60 KB to avoid running out of RAM with multiple connected clients.
- If the SD card fails to mount, the web server still starts and `index.html` will not be served. Check wiring and card formatting.
- AP SSID and password changes require a reboot to take effect.
