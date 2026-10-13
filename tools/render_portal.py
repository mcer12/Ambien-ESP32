"""Fill the portal templates from src/web_portal.cpp with sample values and screenshot them with headless Edge /
Chrome into screenshots/portal_<variant>_<phone|desktop>.png. Needs Pillow and the fonts from make_webfonts.py.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "screenshots")
BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
]

# Placeholder values, mirroring WebPortal::getSettingsPage() / getTitleBlock()
COMMON = {
    "%SUBTITLE%": "CO2 + Temperature + Humidity", "%DEVICE_NAME%": "Ambien-A590",
    "%BATTERY_LEVEL%": "l3", "%BATTERY_VOLTAGE%": "3.95 V", "%FW_VERSION%": "1.0",
    "%SSID%": "HomeWifi", "%PASS%": "placeholder",
    "%WAKE_1%": "", "%WAKE_10%": "", "%WAKE_30%": " selected", "%WAKE_60%": "",
    "%SCALE_C%": " selected", "%SCALE_F%": "",
    "%CO2_ON%": " selected", "%CO2_OFF%": "",
    "%MQTT_TOPIC_DEFAULT%": "ambien/ambien_a590",
    "%CALIBRATE_LINK%": '<p class="hint"><a href="/calibrate">Calibrate CO2 sensor</a></p>',
}
VARIANTS = {
    "mqtt_off": {
        "%MQTT_CHECKED%": "", "%MQTT_STYLE%": "display:none",
        "%MQTT_BROKER%": "", "%MQTT_PORT%": "1883", "%MQTT_USER%": "", "%MQTT_PASS%": "",
        "%MQTT_TOPIC%": "", "%HASS_CHECKED%": "",
    },
    "mqtt_on": {
        "%MQTT_CHECKED%": " checked", "%MQTT_STYLE%": "",
        "%MQTT_BROKER%": "192.168.1.10", "%MQTT_PORT%": "1883", "%MQTT_USER%": "ambien",
        "%MQTT_PASS%": "placeholder", "%MQTT_TOPIC%": "", "%HASS_CHECKED%": " checked",
        "%BATTERY_LEVEL%": "charging", "%BATTERY_VOLTAGE%": "Charging",  # above 4.3 V
    },
}
# Variants rendered in the light theme (same placeholder values as the variant named after "light_")
LIGHT = {"light_mqtt_on": "mqtt_on"}
SIZES = {"phone": 500, "desktop": 1280}  # headless Edge/Chrome windows are at least ~500 px wide


def raw_literal(source, name):
    match = re.search(r"static const char " + name + r'\[\] PROGMEM = R"rawliteral\((.*?)\)rawliteral";', source, re.S)
    if not match:
        sys.exit(f"{name} not found in src/web_portal.cpp")
    return match.group(1)


def crop_background(path):
    from PIL import Image
    img = Image.open(path).convert("RGB")
    w, h = img.size
    background = img.getpixel((w - 1, h - 1))  # page background (--paper)
    bottom = h
    while bottom > 1 and all(img.getpixel((x, bottom - 1)) == background for x in range(0, w, 4)):
        bottom -= 1
    img.crop((0, 0, w, min(h, bottom + 30))).save(path)


def main():
    source = open(os.path.join(ROOT, "src", "web_portal.cpp"), encoding="utf-8").read()
    header, page, footer, title = (raw_literal(source, n) for n in ("HTML_HEADER", "SETTINGS_PAGE", "HTML_FOOTER", "TITLE_BLOCK"))
    page = page.replace("%TITLE%", title)
    footer = footer.replace("%FW_VERSION%", "1.0")
    browser = next((b for b in BROWSERS if os.path.exists(b)), None)
    if not browser:
        sys.exit("Edge or Chrome not found")
    os.makedirs(OUT, exist_ok=True)

    for variant in list(VARIANTS) + list(LIGHT):
        values = VARIANTS[LIGHT.get(variant, variant)]
        html = page
        for key, value in {**COMMON, **values}.items():
            html = html.replace(key, value)
        left = sorted(set(re.findall(r"%[A-Z0-9_]+%", html)))
        if left:
            sys.exit(f"Unfilled placeholders: {left} (update tools/render_portal.py)")
        page_header = header.replace("<html>", '<html data-theme="light">', 1) if variant in LIGHT else header
        html_path = os.path.join(OUT, f"portal_{variant}.html")
        open(html_path, "w", encoding="utf-8").write(page_header + html + footer)

        for size, width in SIZES.items():
            png = os.path.join(OUT, f"portal_{variant}_{size}.png")
            # Fresh profile dir: a running Edge/Chrome would otherwise take over the job and never exit, and a kept
            # profile would serve the previous web fonts from its cache
            for attempt in range(3):  # a fresh headless profile occasionally hangs on startup: retry
                profile = tempfile.mkdtemp(prefix="render_portal_")
                try:
                    subprocess.run([browser, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--no-first-run",
                                    "--no-default-browser-check", f"--user-data-dir={profile}",
                                    "--force-device-scale-factor=1", f"--window-size={width},3000",
                                    f"--screenshot={png}", "file:///" + html_path.replace("\\", "/")],
                                   check=True, capture_output=True, timeout=90)
                    break
                except subprocess.TimeoutExpired:
                    if attempt == 2:
                        raise
                finally:
                    shutil.rmtree(profile, ignore_errors=True)
            crop_background(png)
            print(png)


if __name__ == "__main__":
    main()
