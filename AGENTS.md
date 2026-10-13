# Ambien-ESP32 - notes for coding agents

Battery-powered ESP32 air-quality monitor with a 4.2" 400x300 black/white e-ink display. It wakes from deep sleep,
reads CO2 (Sensirion SCD4x, optional) and temperature / humidity (SHT30), draws the values and goes back to sleep.
Settings are made in a web portal served from the device's own access point or over the local WiFi. Optionally
every measurement is published over MQTT, with Home Assistant MQTT discovery.

## Rules

- Never commit or push unless the user explicitly asks.
- Build and flash with PlatformIO from PowerShell, not Git Bash (see Build). Several boards get swapped on the same
  USB port: check the MAC address in the upload log and say which board was flashed.
- Keep texts on screen left-aligned, except the button labels (centered in their bars).
- Gaps and offsets on the scaled 3D texts are expressed in font pixels (1 font pixel = the scale, e.g. 4 or 5 px).
- Comments only where the code is not self-explanatory: the why behind a value, hardware / library quirks, gotchas.
  No comments that restate the code, label sections or describe obvious functions; no commented-out code.
- Never put local paths, user names, credentials or hostnames in tracked files.

## Build and flash

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run                                  # build
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload --upload-port COM6     # flash (CH340, usually COM6)
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list                          # find the port
```

- `pio` is not on the Git Bash PATH, and when PlatformIO reinstalls esptool under Git Bash it fails with
  "MSys/Mingw is not supported".
- Harmless noise in every build: `Filesystem error in safe_remove_directory_pattern ... AzureIoT ... .idx`.
- `pio pkg update` rewrites `platformio.ini` (adds a template header and a pinned duplicate of the library). Restore
  the plain `lib_deps` list afterwards.
- Board: `esp32dev`, Arduino framework. Uploading firmware does not erase LittleFS, so `/config.json` survives.

## Hardware

| Function | GPIO |
|---|---|
| E-ink CS / DC / RST / BUSY | 5 / 17 / 16 / 4 (HW SPI: SCK 18, MOSI 23) |
| Button 1 (left) / Button 2 (right) | 35 / 32, active HIGH, `INPUT_PULLDOWN`, also the deep sleep EXT1 wakeup |
| Battery voltage (divider x2) | 34 (ADC1 channel 6) |
| SCD4x, SHT30 (0x44) | I2C, default pins |

## Code map

| File | What it does |
|---|---|
| `src/main.cpp` | Wakeup handling and the whole device flow (states, servers, sleep), battery reading |
| `src/eink_fns.*` | Display object and every screen (sensor page, info pages), fonts, icons, text helpers |
| `src/web_portal.*` | Captive portal / settings page (`/`, POST `/` saves and restarts, `/calibrate` = SCD4x forced recalibration to 430 ppm). The page is the `SETTINGS_PAGE` template with `%NAME%` placeholders, filled by `getSettingsPage()` |
| `src/mqtt_fns.*` | MQTT client (PubSubClient): publishes a measurement and, when requested, the Home Assistant discovery config |
| `lib/AmbienMqtt/` | Arduino-independent MQTT payload / discovery JSON builders (unit tested) |
| `test/` | Unity tests of `lib/` |
| `tools/render_portal.py` | Screenshots of the settings page (see Tests and tools) |
| `README.md`, `images/` | Public readme; `images/` holds copies of renders from `screenshots/` (refresh them when screens change) |
| `tools/make_webfonts.py`, `src/web_fonts.h` | PixelFonts converted to WOFF2 web fonts for the portal (generated header, served under `/fonts/`) |
| `src/wifi_manager.*` | WiFi station connect and the setup AP (`Ambien_` + last 4 hex digits of the MAC, `deviceSuffix()`, open network, `192.168.0.1`) |
| `src/utils.*` | Global `json` config, `readConfig()` / `saveConfig()` (`/config.json` on LittleFS), `co2SensorEnabled()`, `offlineMode()` |
| `src/scd4x_fns.*`, `src/sht30_fns.*` | Sensor init and reading (globals `scd4x_co2`, `sht30_temp`, `sht30_humidity`) |

The project is called Ambien (Breathe is its old name); only the project folder and the GitHub repository are
still named Breathe-ESP32.

## Device flow

`setup()` reads the config and the wakeup cause, `loop()` runs one path and ends in deep sleep.

- **Timer wakeup / power-on:** measure and show the sensor page, then sleep `wake_interval` minutes.
  - No WiFi settings and not in offline mode: fallback setup AP (below).
  - Offline mode: measure without WiFi.
  - The saved WiFi can't be reached: "No connection" screen, normal wake timer, so it retries at the next interval.
- **Button 1:** AP server ("Access Point" page, buttons SWITCH TO LOCAL / SLEEP, 10 min).
- **Button 2:** local server ("Local Server" page, buttons SWITCH TO AP / SLEEP, 10 min).
  - After a failed connection (`RTC_DATA_ATTR wifiConnectFailed`) it is "TRY AGAIN" = measure now.
  - In offline mode it is "REFRESH" = measure now.
- **Without WiFi settings:** button 1 starts the fallback AP, button 2 turns on offline mode and measures.
- **Fallback setup AP:** 10 min, buttons SLEEP / OFFLINE MODE.
  - SLEEP or timeout: "First run?" screen, sleep until a button press (no timer).
  - In offline mode, SLEEP or timeout goes back to offline measuring instead.
- **Server session ended / config saved (software reset after the portal saves):** "Config saved" screen, 5 s sleep,
  then a normal measurement (the first reading can be skewed by heat from the radio).
- Saving WiFi settings in the portal turns offline mode off.

## Config keys (`/config.json`)

`ssid`, `pass`, `wake_interval` (minutes, default 30), `co2_sensor` (`"disabled"` skips every SCD4x call and shows the
no-CO2 layout), `offline_mode` (bool), `temp_scale` (saved, not used yet), `mqtt_enabled` (bool), `mqtt_broker`,
`mqtt_port`, `mqtt_user`, `mqtt_pass`, `mqtt_topic` (empty = `ambien/<device id>`), `hass_register` (bool; `"yes"`
from older firmware also counts). `temp_topic` / `humidity_topic` / `battery_topic` of older firmware are removed on
the next save.

## MQTT and Home Assistant

- Online wakeups with `mqtt_enabled` publish one JSON message, not retained, to `<mqtt_topic>/state`:
  `{"temperature":23.5,"humidity":45,"co2":812,"battery":4.05}` (`co2` only with the sensor enabled and a valid
  reading; temperature always in degrees C). Then the radio is switched off before the display refresh.
- Device id: `ambien_` + the same 4 MAC hex digits as the setup AP, lowercase (`Ambien_A590` -> `ambien_a590`,
  `deviceSuffix()` / `mqtt_deviceId()`); also the MQTT client id. Home Assistant device name `Ambien A590`.
- `hass_register`: the next online wakeup publishes Home Assistant device-based discovery (one retained message on
  `homeassistant/device/<device id>/config` with `dev`, `o` (origin) and `cmps`: temperature, humidity, CO2,
  battery voltage), then clears the flag. Every sensor has `expire_after` = 7200 s (2 hours, fixed), so Home
  Assistant shows it unavailable after 2 hours without a measurement. The device sleeps, so it can't answer HA's birth message;
  that is why discovery is retained and the state is not.
- Connect / socket timeouts are short: an unreachable broker costs a few seconds and never blocks the measurement.
- Discovery JSON is ~1 KB: PubSubClient's buffer is raised to 1536 bytes (`MQTT_BUFFER_SIZE`).

## Tests and tools

- `test/test_mqtt_payloads`: Unity tests of `lib/AmbienMqtt`.
  - On the ESP32 (board connected): `pio test -e esp32dev --upload-port COM6`, then flash the firmware again.
  - On the PC: `pio test -e native` needs gcc. Without gcc on Windows, run them in Docker:
    `docker run --rm -v "$PWD:/src" -w /src python:3.12-slim sh -c "apt-get update -qq && apt-get install -y -qq g++ && pip install -q platformio && pio test -e native"`
    (run from the repo root; from Git Bash prefix `MSYS_NO_PATHCONV=1`).
- Web portal look: e-paper style in three colors (CSS variables `--paper`, `--ink`, `--gray` in `HTML_HEADER`). Text uses the PixelFonts as web fonts: `python tools/make_webfonts.py`
  regenerates `src/web_fonts.h` (needs `pip install fonttools brotli` and a prior `pio run`). 1 font pixel = 1/16 em,
  so keep font sizes at multiples of 16px and line heights at the font size + a multiple of 8px (whole-pixel
  baseline). Text is hard-edged through SVG alpha-threshold filters, only at devicePixelRatio 1 (class `crisp`
  on `<html>`): `#crisp` (alpha >= 0.6) for light text on dark, `#crisp-dark-text` (alpha >= 0.5) for dark text
  on light, chosen per theme in the CSS (the button label always uses the other one). Dark mode is the default;
  the header sun/moon button sets `data-theme="light"` on `<html>` (remembered in localStorage), which swaps the
  three color variables. Text needs a transparent background (text on a filled box gets its own filtered `span`,
  like the button). Anything centered, including the page column itself, must be in `snapToPixels()`: an odd
  free width puts it at x.5 px and the threshold erases 1px strokes.
- `python tools/render_portal.py`: fills the portal's HTML templates from `src/web_portal.cpp` with sample values and
  screenshots them with headless Edge / Chrome into `screenshots/portal_*`. When a placeholder is added to
  `SETTINGS_PAGE`, add a sample value to the script (it stops on unfilled placeholders).

## Display

- Panel: HINK-E042A13, 400x300, SSD1683-compatible. Driver: `GxEPD2_420_GDEY042T81` from the official
  `zinggjm/GxEPD2` (the old `GxEPD2_HINK` fork is no longer needed). Rotation 2.
- `eink_init()` calls `display.epd2.selectFastFullUpdate(false)`: the driver's fast full refresh (temperature forced
  to 100 C) leaves this panel gray. The normal refresh is crisp and takes ~2.9 s.
- `display.init(115200, false)`: one refresh pass per wakeup instead of a clearing pass + the image. Switch back to
  `true` if ghosting ever shows.
- Never use partial refresh: the panel's OTP partial waveform flashes and darkens the whole screen.
- `eink_init()` sets the CS / RST / DC pins as outputs before `display.init()` to avoid ESP32 core 3.x GPIO errors.

## Fonts and screens

- All text uses the PixelFonts library (`https://github.com/mcer12/PixelFonts`). Big text is a 1 px font scaled
  with `setTextSize()` through `eink_setFont(font, scale)`; always set the scale (1 = native) when switching fonts.
- Sensor page:
  - CO2 value: `pixelfont_3d_condensed_17x7_bold` x5.
  - PPM / TEMP / HUM labels: `pixelfont_3d_condensed_5x5_thin` x4.
  - Temperature / humidity values: `pixelfont_3d_condensed_extended_latin_13x7_semibold` x5.
  - With the CO2 sensor disabled the values use `..._extended_latin_17x7_bold` x5, with the thermometer and drop
    icons (`ICON_*` bitmaps: 16 rows, one `uint16_t` per row, MSB = left column, drawn x5 by `eink_drawIcon()`).
- Info pages (Access Point, Local Server, Config saved, First run?, No connection) share `eink_displayInfoPage()`.
  - Title: `pixelfont_3d_condensed_13x7_semibold` x3. A title too wide for the screen wraps onto a second line.
  - Labels and highlighted words: `LABEL_FONT`. Everything else, including the sensor page footer: `TEXT_FONT`.
    Both are set at the top of `eink_fns.cpp` (currently `pixelfont_13x7_semibold` / `pixelfont_13x7_thin`).
- Button labels: `pixelfont_13x7_semibold`, white on black bars.
- `eink_printLines()` text format: `'\n'` separates lines.
  - `"Label: value"` lines get the label in `LABEL_FONT` and all values of the block aligned after the longest label.
  - Text between two `EMPHASIS` (`"\x02"`) markers is printed in `LABEL_FONT`.
  - An empty line is a 10 px paragraph gap.
  - Other lines wider than the screen are word-wrapped. Still keep texts short enough to avoid a single-word last line.
- Degree sign: `display.write(0xB0)` (or `char(0xB0)` in a `String`) with an `extended_latin` font. A UTF-8 `"°"` is
  two bytes and would also draw `Â`.
- Values: humidity is capped at 99% and negative temperatures are rounded to whole degrees, so the side-by-side
  layout always fits.

## Files not in git

`.gitignore` covers `.pio/`, local `.vscode` files, `__pycache__/` and `screenshots/` (rendered mockups of the screens, generated
locally).
