# Ambien-ESP32 - notes for coding agents

Battery-powered ESP32 air-quality monitor with a 4.2" 400x300 black/white e-ink display. It wakes from deep sleep,
reads CO2 (Sensirion SCD4x, optional) and temperature / humidity (SHT30), draws the values and goes back to sleep.
Settings are made in a web portal served from the device's own access point or over the local WiFi. Optionally
every measurement is published over MQTT, with Home Assistant MQTT discovery.

## Rules

- Never commit or push unless the user explicitly asks.
- Build and flash with PlatformIO from PowerShell, not Git Bash (see Build). Several boards get swapped on the same
  USB port: check the MAC address in the upload log and say which board was flashed.
- Keep texts on screen left-aligned; the button labels sit at the left and right screen edge (see Fonts and
  screens).
- Gaps and offsets on the scaled 3D texts are expressed in font pixels (1 font pixel = the scale, e.g. 4 or 5 px).
- Comments only where the code is not self-explanatory: the why behind a value, hardware / library quirks, gotchas.
  No comments that restate the code, label sections or describe obvious functions; no commented-out code.
  One line each, never a paragraph: what needs more than that belongs in this file, not in the source.
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
| `src/web_portal.*` | Captive portal / settings page (`/`, POST `/` saves and restarts, `/calibrate` arms the SCD4x forced recalibration for the next measurement (`?cancel=1` disarms), `/diag` = sensor and device data, POST `/theme` saves the light / dark switch right away). The page is the `SETTINGS_PAGE` template with `%NAME%` placeholders, filled by `getSettingsPage()` |
| `src/diagnostics.*` | `diagnosticsHtml()`: SCD4x, SHT30, ESP32, eFuse and WiFi data as an HTML fragment, fetched by the footer's gear icon into the panel under the footer. SCD4x settings are only readable while it idles, ambient pressure only while it measures |
| `src/mqtt_fns.*` | MQTT client (PubSubClient): publishes a measurement and, when requested, the Home Assistant discovery config |
| `lib/AmbienMqtt/` | Arduino-independent MQTT payload / discovery JSON builders (unit tested) |
| `test/` | Unity tests of `lib/` |
| `tools/render_portal.py` | Screenshots of the settings page (see Tests and tools) |
| `pcb/` | EasyEDA (Standard) schematic / board source, gerbers, BOM and pick-and-place (exports, edited in EasyEDA only) |
| `README.md`, `images/` | Public readme; `images/` holds copies of renders from `screenshots/` (refresh them when screens change) |
| `tools/make_webfonts.py`, `src/web_fonts.h` | PixelFonts converted to WOFF2 web fonts for the portal (generated header, served under `/fonts/`) |
| `src/wifi_manager.*` | WiFi station connect (hostname `ambien-` + the same 4 hex digits, lowercase) and the setup AP (`Ambien_` + last 4 hex digits of the MAC, `deviceSuffix()`, open network, `192.168.0.1`) |
| `src/utils.*` | Global `json` config, `readConfig()` / `saveConfig()` (`/config.json` on LittleFS), `co2SensorEnabled()`, `offlineMode()` |
| `src/scd4x_fns.*`, `src/sht30_fns.*` | Sensor init and reading (globals `scd4x_co2`, `sht30_temp`, `sht30_humidity`) |

The project is called Ambien (Breathe is its old name); only the project folder and the GitHub repository are
still named Breathe-ESP32.

## Device flow

`setup()` reads the config and the wakeup cause, `loop()` runs one path and ends in deep sleep.

- **Timer wakeup / power-on:** measure and show the sensor page, then sleep `wake_interval` minutes.
  - CO2 warm-up (`warmUpCo2Sensor()`): the SCD4x only measures (low power periodic, ~3.2 mA) for `CO2_WARMUP_S`
    (3 min, in `scd4x_fns.h`) before each reading and idles (~0.2 mA) otherwise. The calibration uses the same
    constant: Sensirion needs >3 min of the normal operation mode before a forced recalibration, and a
    calibration taken at a different warm-up than the readings leaves a permanent offset behind. A wakeup without `RTC_DATA_ATTR co2WarmedUp` starts
    it and sleeps the warm-up with the radio off; the next wakeup reads it, stops it and sleeps `wake_interval`
    minus the warm-up, so readings stay `wake_interval` apart. With the 1 minute interval it keeps measuring.
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
- **Server sessions** (AP, local, fallback AP): the SCD4x idles (`idleCo2Sensor()` stops a measurement left running).
  `/calibrate` only sets `calibrate_pending`; `/diag` only stops and restarts the sensor if it was measuring.
- **Server session ended / config saved (software reset after the portal saves):** "Config saved" screen (with the
  CO2 sensor it says the first reading comes in 3 min, so the long sleep doesn't look like a hang), the SCD4x
  started and a warm-up sleep (5 s without the CO2 sensor), then a normal measurement (the first reading can
  be skewed by heat from the radio).
- **Forced recalibration** runs on the measurement path, not in the portal: a measurement wakeup with
  `calibrate_pending` reads the sensor, then calls `scd4x_calibrate()` (`calibrateCo2Sensor()` in `main.cpp`) and
  stores the correction in `cal_correction`. A failed recalibration stays armed and retries. The correction
  corrects the sensor for the state it is calibrated in, and only this path is in the state the readings are
  taken in: battery, radio off, warmed up through a deep sleep. Calibrating in the portal instead put the
  difference between an awake USB powered board and a sleeping battery powered one into every reading, by a
  different amount on every unit, which is what made two calibrated units read ~250 ppm apart while agreeing
  in the portal.
  - That wakeup read the sensor before recalibrating it, so its value is stale: the screen shows the
    "Calibrated" page instead of the sensor page, the MQTT message leaves `co2` out, and it sleeps one
    `CO2_WARMUP_S` (not `wake_interval`) so the first corrected reading follows in 3 min.
  - With the calibration armed, the "Config saved" screen says the sensor is calibrated at the next wakeup
    instead of showing the first-reading notice.
- ASC (the SCD4x's automatic self-calibration) is switched off at every start (`autoCalibrate` = false in the
  `scd4x.begin()` calls): it assumes the lowest CO2 it ever sees is 400 ppm and needs continuous operation with
  daily fresh air, so on a device that sleeps 28 of every 30 minutes indoors it slowly overwrites the forced
  recalibration with its own guess, by a different amount on every unit. The setting and the altitude only live
  in the sensor's RAM, so `scd4x_calibrate()` follows the recalibration with one `persistSettings()`: without it
  a battery swap restores the factory defaults (ASC on) and the drift starts again. It sits on the calibration
  path and not on the wakeup path because the EEPROM is only good for 2000 writes. The forced recalibration
  itself has its own EEPROM in the sensor and survives a power cycle either way. ASC corrects nothing before its
  initial period (44 h of measuring, ~880 wakeups at `CO2_WARMUP_S`), so it is a slow drift, never a fresh
  calibration gone wrong.
- Reading the SCD4x while it measures logs three `i2cWrite(): i2c_master_transmit failed` errors: `scd4x_begin()`
  sends idle-only commands (serial number, ASC) that the sensor refuses then. Harmless.
- Saving WiFi settings in the portal turns offline mode off.

## Config keys (`/config.json`)

`ssid`, `pass`, `wake_interval` (minutes, default 30), `co2_sensor` (`"disabled"` skips every SCD4x call and shows the
no-CO2 layout), `offline_mode` (bool), `dark_mode` (bool, on unless set to false: dark portal and inverted
e-ink screens), `temp_scale` (saved, not used yet), `mqtt_enabled` (bool), `mqtt_broker`,
`mqtt_port`, `mqtt_user`, `mqtt_pass`, `mqtt_topic` (empty = `ambien/<device id>`), `hass_register` (bool; `"yes"`
from older firmware also counts), `calibrate_pending` (bool, armed in `/calibrate`, cleared by the measurement
that recalibrates), `cal_correction` (ppm the last recalibration accepted, shown in the `/diag` panel). `temp_topic` / `humidity_topic` / `battery_topic` of older firmware are removed on
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
  the header sun/moon button swaps the three color variables by setting `data-theme="light"` on `<html>` and POSTs
  the choice to `/theme`, which saves `dark_mode` and lets the e-ink screens follow. The server fills the attribute
  into the `%THEME%` placeholder of `HTML_HEADER`, so the saved theme is there before the first paint.
  Text needs a transparent background (text on a filled box gets its own filtered `span`,
  like the button). Anything centered, including the page column itself, must be in `snapToPixels()`: an odd
  free width puts it at x.5 px and the threshold erases 1px strokes.
- `python tools/render_portal.py`: fills the portal's HTML templates from `src/web_portal.cpp` with sample values and
  screenshots them with headless Edge / Chrome into `screenshots/portal_*`. When a placeholder is added to
  `SETTINGS_PAGE`, add a sample value to the script (it stops on unfilled placeholders). The `*_diag` variants show
  the data panel open with `DIAG_SAMPLE`: keep it in step with `src/diagnostics.cpp`.

## Display

- Panel: HINK-E042A13, 400x300, SSD1683-compatible. Driver: `GxEPD2_420_GDEY042T81` from the official
  `zinggjm/GxEPD2` (the old `GxEPD2_HINK` fork is no longer needed). Rotation 2.
- `eink_init()` calls `display.epd2.selectFastFullUpdate(false)`: the driver's fast full refresh (temperature forced
  to 100 C) leaves this panel gray. The normal refresh is crisp and takes ~2.9 s.
- `display.init(115200, false)`: one refresh pass per wakeup instead of a clearing pass + the image. Switch back to
  `true` if ghosting ever shows.
- Never use partial refresh: the panel's OTP partial waveform flashes and darkens the whole screen.
- The border around the image follows the waveform LUT selected by SSD1683 command 0x3C: LUT1 (`0x01`, what the
  driver's init sends) leaves it white, LUT2 (`0x02`) black. GxEPD2 keeps `_writeCommand()` protected and sends its
  init (with a SWRESET) at the first write to the panel, so `eink_beginFrame()` writes a screen buffer first, then
  sends 0x3C over raw SPI, then draws: the order matters, the panel forgets the setting at every hibernate.
- `eink_init()` sets the CS / RST / DC pins as outputs before `display.init()` to avoid ESP32 core 3.x GPIO errors.

## Fonts and screens

- `dark_mode` inverts every screen: `paperColor()` / `inkColor()` in `eink_fns.cpp` replace the fixed
  `GxEPD_WHITE` / `GxEPD_BLACK`, the button bars and the icons included. The portal's theme switch redraws the
  page on the panel during a server session (`WebPortal::themeChanged()`, the `redraw` lambda in `main.cpp`).

- All text uses the PixelFonts library (`https://github.com/mcer12/PixelFonts`). Big text is a 1 px font scaled
  with `setTextSize()` through `eink_setFont(font, scale)`; always set the scale (1 = native) when switching fonts.
- Sensor page:
  - CO2 value: `pixelfont_3d_condensed_17x7_bold` x5.
  - PPM / TEMP / HUM labels: `pixelfont_3d_condensed_5x5_thin` x4.
  - Temperature / humidity values: `pixelfont_3d_condensed_extended_latin_13x7_semibold` x5.
  - With the CO2 sensor disabled the values use `..._extended_latin_17x7_bold` x5, with the thermometer and drop
    icons (`ICON_*` bitmaps: 16 rows, one `uint16_t` per row, MSB = left column, drawn x5 by `eink_drawIcon()`).
- Info pages (Access Point, Local Server, Config saved, Calibrated, First run?, No connection) share
  `eink_displayInfoPage()`.
  - Title: `pixelfont_3d_condensed_13x7_semibold` x3. A title too wide for the screen wraps onto a second line.
  - Labels and highlighted words: `LABEL_FONT`. Everything else, including the sensor page footer: `TEXT_FONT`.
    Both are set at the top of `eink_fns.cpp` (currently `pixelfont_13x7_semibold` / `pixelfont_13x7_thin`).
- Button labels: `pixelfont_13x7_semibold` at the left and right screen edge, no bars. A line runs over the
  label, flush with the text, and turns down to the bottom edge above the dot on the physical button it
  belongs to, so the label points at its button. The two buttons sit close together in the middle of the
  enclosure: `BUTTON_DOT_GAP` in `eink_fns.cpp` is their dot centre to dot centre distance, and the drops are
  placed symmetrically around the screen centre.
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
