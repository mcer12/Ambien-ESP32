#include "web_portal.h"
#include "scd4x_fns.h"
#include "mqtt_fns.h"
#include "web_fonts.h"
#include "diagnostics.h"


static const char HTML_HEADER[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html%THEME%><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1, user-scalable=0">
<title>Ambien - ESP32 CO2 Monitor</title>
<style>
@font-face{font-family:PixelText;src:url(fonts/text.woff2) format("woff2");font-weight:400;}
@font-face{font-family:PixelText;src:url(fonts/text-bold.woff2) format("woff2");font-weight:700;}
@font-face{font-family:PixelSmall;src:url(fonts/small.woff2) format("woff2");}
@font-face{font-family:PixelTitle;src:url(fonts/title.woff2) format("woff2");}
/* 1 font pixel = 1/16 em: keep font sizes at multiples of 16px */
:root{--paper:#000;--ink:#fff;--gray:#a8a8a8;color-scheme:dark;}
:root[data-theme="light"]{--paper:#fff;--ink:#000;--gray:#575757;color-scheme:light;}
*{box-sizing:border-box;}
html,body{margin:0;padding:0;background:var(--paper);color:var(--ink);font:16px/24px PixelText,monospace;-webkit-font-smoothing:none;font-smooth:never;}
/* Browsers anti-alias text even on whole pixels; the filters keep only (nearly) fully covered pixels, so text needs a
   transparent background. Light on dark: #crisp, alpha >= 0.6 (0.5 fills 1px gaps between strokes). Dark on light has
   less coverage: #crisp-dark-text, alpha >= 0.5. Only at devicePixelRatio 1: at fractional scales the cut eats 1px
   strokes, at 2x+ text is crisp anyway. */
.crisp .wrapper{filter:url(#crisp);}
.crisp button span{filter:url(#crisp-dark-text);}
.crisp[data-theme="light"] .wrapper{filter:url(#crisp-dark-text);}
.crisp[data-theme="light"] button span{filter:url(#crisp);}
.crisp button:hover span{filter:url(#crisp);}
.crisp[data-theme="light"] button:hover span{filter:url(#crisp-dark-text);}
.wrapper,button span,.links a{position:relative;}
a{color:var(--ink);}
::selection{background-color:#ffd400;color:#000;}
.wrapper{max-width:600px;margin:auto;padding:32px 16px;}
.title{display:flex;justify-content:space-between;align-items:flex-end;gap:16px;margin:0 0 24px 0;}
.title h1{margin:0;font:48px/56px PixelTitle,monospace;}
.title p{margin:6px 0 0 0;color:var(--ink);font-weight:700;}
.version{margin:0 0 0 16px;font:32px/40px PixelTitle,monospace;color:var(--gray);}
.device{display:flex;align-items:center;}
.device-info{text-align:right;white-space:nowrap;}
.device-info b{display:block;}
/* 17px tall: 2px above and below the 13px text next to it */
.power{display:flex;justify-content:flex-end;align-items:flex-start;font-weight:700;}
.battery{display:flex;gap:2px;padding:2px;margin:4px 12px 0 0;position:relative;border:2px solid var(--ink);}
.battery::after{content:"";position:absolute;right:-6px;top:2px;width:2px;height:9px;background:var(--ink);}
.battery i{display:block;width:4px;height:9px;}
.l1 i:nth-child(-n+1),.l2 i:nth-child(-n+2),.l3 i:nth-child(-n+3),.l4 i{background:var(--ink);}
.battery .bolt,.charging i{display:none;}
.charging .bolt{display:block;}
button.theme-switch{display:block;flex:none;width:30px;height:30px;margin:0 0 0 16px;padding:0;border:0;color:var(--ink);background:transparent;cursor:pointer;}
button.theme-switch:hover{background:transparent;}
button.theme-switch svg{display:block;}
.theme-switch .sun,:root[data-theme="light"] .theme-switch .moon{display:none;}
:root[data-theme="light"] .theme-switch .sun{display:inline;}
.container{padding:24px;border:3px solid var(--ink);box-shadow:3px 3px 0 var(--ink);}
h2{margin:40px 0 6px 0;font:32px/40px PixelTitle,monospace;}
form>h2:first-child,.container>h2:first-child{margin-top:0;}
p{margin:0 0 16px 0;color:var(--gray);}
.row{margin-bottom:16px;}
label{display:block;margin-bottom:4px;font-weight:700;}
input[type="text"],input[type="number"],input[type="password"],select{display:block;width:100%;height:44px;margin:0;padding:0 12px;font:16px PixelText,monospace;color:var(--ink);background:transparent;border:2px solid var(--ink);border-radius:0;outline:none;-webkit-appearance:none;appearance:none;}
input:focus,select:focus{outline:2px solid var(--gray);outline-offset:2px;}
input::placeholder{color:var(--gray);opacity:1;}
option{color:var(--ink);background:var(--paper);}
.select{position:relative;}
.select select{padding-right:40px;cursor:pointer;}
.select::after{content:"";position:absolute;right:14px;top:19px;width:12px;height:6px;background:var(--ink);pointer-events:none;clip-path:polygon(0 0,12px 0,12px 2px,10px 2px,10px 4px,8px 4px,8px 6px,4px 6px,4px 4px,2px 4px,2px 2px,0 2px);}
.check{display:flex;align-items:center;cursor:pointer;margin:0;}
.check input{flex:none;width:20px;height:20px;margin:0 12px 0 0;border:2px solid var(--ink);background:transparent;border-radius:0;-webkit-appearance:none;appearance:none;cursor:pointer;}
.check input:checked{background:var(--ink);box-shadow:inset 0 0 0 3px var(--paper);}
.hint{margin:8px 0 0 0;overflow-wrap:anywhere;}
.status{color:var(--ink);font-weight:700;}
button{display:block;width:100%;height:48px;margin:32px 0 0 0;padding:0;font:700 16px PixelText,monospace;text-transform:uppercase;color:var(--paper);background:var(--ink);border:2px solid var(--ink);border-radius:0;cursor:pointer;transition:color .15s,background-color .15s;}
button:hover{color:var(--ink);background:var(--paper);}
.links{margin:24px 0 0 0;text-align:center;color:var(--ink);}
.footer{display:flex;justify-content:space-between;align-items:center;gap:16px;margin:8px 0 0 0;font-family:PixelSmall,monospace;color:var(--gray);}
.footer a{color:var(--gray);}
.footer .sep{margin:0 6px;}
button.diag-toggle{display:block;flex:none;width:16px;height:16px;margin:0;padding:0;border:0;color:var(--gray);background:transparent;cursor:pointer;}
button.diag-toggle:hover{background:transparent;}
button.diag-toggle[aria-expanded="true"]{color:var(--ink);}
button.diag-toggle svg{display:block;}
.diag{margin:24px 0 0 0;}
.diag h3{margin:24px 0 8px 0;font:700 16px/24px PixelText,monospace;}
.diag h3:first-child{margin-top:0;}
/* Fixed label column: a percentage would put the values at x.5 px, where the crisp filters erase 1px strokes */
.diag table{width:100%;border-collapse:collapse;table-layout:fixed;}
.diag td{padding:0;vertical-align:top;overflow-wrap:anywhere;font:16px/16px PixelSmall,monospace;}
.diag td:first-child{width:168px;padding-right:16px;color:var(--gray);}
@media all and (max-width:520px){
.wrapper{padding:24px 0;}
.title{padding:0 16px;}
.version,.title p{display:none;}
.container{padding:24px 0;margin:0 16px;border-width:3px 0;box-shadow:none;}
.footer,.diag{padding:0 16px;}
}
</style>
<script>function crispCheck(){document.documentElement.classList.toggle('crisp',window.devicePixelRatio===1);}
crispCheck();window.addEventListener('resize',crispCheck);</script>
</head><body>
<svg width="0" height="0" style="position:absolute"><filter id="crisp" color-interpolation-filters="sRGB"><feComponentTransfer><feFuncA type="discrete" tableValues="0 0 0 1 1"/></feComponentTransfer></filter><filter id="crisp-dark-text" color-interpolation-filters="sRGB"><feComponentTransfer><feFuncA type="discrete" tableValues="0 1"/></feComponentTransfer></filter></svg>
)rawliteral";

static const char HTML_FOOTER[] PROGMEM = R"rawliteral(
</div>
<div class="footer"><span>Ambien-ESP32 <span class="sep">|</span> Firmware v%FW_VERSION% <span class="sep">|</span> <a href="https://github.com/mcer12/Ambien-ESP32" target="_blank">Visit project's GitHub</a></span><button type="button" class="diag-toggle" aria-label="Sensor and device data" aria-expanded="false"><svg width="16" height="16" viewBox="0 0 16 16" shape-rendering="crispEdges" fill="currentColor"><path d="M6 0h4v2h-4zM2 2h2v1h-2zM6 2h4v1h-4zM12 2h2v1h-2zM2 3h12v1h-12zM3 4h10v1h-10zM3 5h4v1h-4zM9 5h4v1h-4zM0 6h6v1h-6zM10 6h6v1h-6zM0 7h5v2h-5zM11 7h5v2h-5zM0 9h6v1h-6zM10 9h6v1h-6zM3 10h4v1h-4zM9 10h4v1h-4zM3 11h10v1h-10zM2 12h12v1h-12zM2 13h2v1h-2zM6 13h4v1h-4zM12 13h2v1h-2zM6 14h4v2h-4z"/></svg></button></div>
<div class="diag" id="diag" hidden></div>
</div>
<script>
// Centered content can land on x.5 px, which the crisp filters would thin or erase: shift it onto a whole pixel
function snapToPixels() {
  // page column first: an odd free width centers it at x.5 px
  ['.wrapper', 'button span, .links a'].forEach(function (selector) {
    document.querySelectorAll(selector).forEach(function (el) {
      el.style.left = '0px';
      var x = el.getBoundingClientRect().left;
      el.style.left = (Math.round(x) - x) + 'px';
    });
  });
}
snapToPixels();
document.fonts.ready.then(snapToPixels);
document.querySelectorAll('.theme-switch').forEach(function (button) {
  button.addEventListener('click', function () {
    var light = document.documentElement.getAttribute('data-theme') !== 'light';
    if (light) document.documentElement.setAttribute('data-theme', 'light');
    else document.documentElement.removeAttribute('data-theme');
    fetch('/theme', {method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                     body: 'dark=' + (light ? '0' : '1')});
  });
});
// Read fresh on every opening: the device reads the sensors only when asked
document.querySelectorAll('.diag-toggle').forEach(function (button) {
  var diag = document.getElementById('diag');
  button.addEventListener('click', function () {
    var open = diag.hidden;
    diag.hidden = !open;
    button.setAttribute('aria-expanded', open);
    if (!open) return;
    diag.innerHTML = '<p>Reading the sensors...</p>';
    fetch('/diag').then(function (response) { return response.text(); })
      .then(function (html) { diag.innerHTML = html; })
      .catch(function () { diag.innerHTML = '<p>Could not read the sensor data.</p>'; });
  });
});
window.addEventListener('resize', snapToPixels);
</script>
</body></html>
)rawliteral";

static const char TITLE_BLOCK[] PROGMEM = R"rawliteral(
<div class="title">
<div><h1>Ambien<span class="version">v%FW_VERSION%</span></h1><p>%SUBTITLE%</p></div>
<div class="device"><div class="device-info"><b>%DEVICE_NAME%</b><span class="power"><span class="battery %BATTERY_LEVEL%"><i></i><i></i><i></i><i></i><svg class="bolt" width="22" height="9" viewBox="0 0 22 9" shape-rendering="crispEdges" fill="currentColor"><path d="M10 1h1v1h-1zM9 2h2v1h-2zM13 2h2v1h-2zM8 3h3v1h-3zM12 3h3v1h-3zM7 4h7v1h-7zM6 5h3v1h-3zM10 5h3v1h-3zM6 6h2v1h-2zM10 6h2v1h-2zM10 7h1v1h-1z"/></svg></span>%BATTERY_VOLTAGE%</span></div><button type="button" class="theme-switch" aria-label="Light / dark mode"><svg width="30" height="30" viewBox="0 0 15 15" shape-rendering="crispEdges" fill="currentColor"><path class="moon" d="M5 0h3v1h-3zM3 1h4v1h-4zM2 2h4v1h-4zM1 3h4v1h-4zM1 4h4v1h-4zM0 5h5v1h-5zM0 6h5v1h-5zM0 7h5v1h-5zM14 7h1v1h-1zM0 8h6v1h-6zM13 8h2v1h-2zM0 9h7v1h-7zM12 9h3v1h-3zM1 10h13v1h-13zM1 11h13v1h-13zM2 12h11v1h-11zM3 13h9v1h-9zM5 14h5v1h-5z"/><path class="sun" d="M7 0h1v1h-1zM7 1h1v1h-1zM2 2h1v1h-1zM12 2h1v1h-1zM3 3h1v1h-1zM11 3h1v1h-1zM6 4h3v1h-3zM5 5h5v1h-5zM4 6h7v1h-7zM0 7h2v1h-2zM4 7h7v1h-7zM13 7h2v1h-2zM4 8h7v1h-7zM5 9h5v1h-5zM6 10h3v1h-3zM3 11h1v1h-1zM11 11h1v1h-1zM2 12h1v1h-1zM12 12h1v1h-1zM7 13h1v1h-1zM7 14h1v1h-1z"/></svg></button></div>
</div>
)rawliteral";

// tools/render_portal.py fills this template too: add a sample value there for every new placeholder
static const char SETTINGS_PAGE[] PROGMEM = R"rawliteral(
<div class="wrapper">
%TITLE%
<div class="container">
<form method="post" action="/">
<h2>Network</h2>
<p>WiFi connection.</p>
<div class="row"><label for="ssid">WiFi SSID</label><input type="text" id="ssid" name="ssid" value="%SSID%"></div>
<div class="row"><label for="pass">WiFi password</label><input type="password" id="pass" name="pass" value="%PASS%"></div>

<h2>Sensor</h2>
<p>Wake interval and sensor options.</p>
<div class="row"><label for="wake_interval">Wake interval</label>
<div class="select"><select id="wake_interval" name="wake_interval">
<option value="1"%WAKE_1%>1 minute</option>
<option value="10"%WAKE_10%>10 minutes</option>
<option value="30"%WAKE_30%>30 minutes</option>
<option value="60"%WAKE_60%>60 minutes</option>
</select></div></div>
<div class="row"><label for="temp_scale">Temperature scale</label>
<div class="select"><select id="temp_scale" name="temp_scale">
<option value="celsius"%SCALE_C%>Celsius</option>
<option value="fahrenheit"%SCALE_F%>Fahrenheit</option>
</select></div></div>
<div class="row"><label for="co2_sensor">CO2 sensor</label>
<div class="select"><select id="co2_sensor" name="co2_sensor">
<option value="enabled"%CO2_ON%>Enabled</option>
<option value="disabled"%CO2_OFF%>Disabled (temperature and humidity only)</option>
</select></div>%CALIBRATE_LINK%</div>

<h2>MQTT</h2>
<p>Send every measurement to an MQTT broker, optionally with Home Assistant discovery.</p>
<div class="row"><label class="check"><input type="checkbox" id="mqtt_enabled" name="mqtt_enabled" value="1"%MQTT_CHECKED%>Enable MQTT</label></div>
<div id="mqtt_fields" style="%MQTT_STYLE%">
<div class="row"><label for="mqtt_broker">Broker address</label><input type="text" id="mqtt_broker" name="mqtt_broker" value="%MQTT_BROKER%" placeholder="192.168.1.10"></div>
<div class="row"><label for="mqtt_port">Port</label><input type="number" id="mqtt_port" name="mqtt_port" value="%MQTT_PORT%"></div>
<div class="row"><label for="mqtt_user">User</label><input type="text" id="mqtt_user" name="mqtt_user" value="%MQTT_USER%" placeholder="(no login)"></div>
<div class="row"><label for="mqtt_pass">Password</label><input type="password" id="mqtt_pass" name="mqtt_pass" value="%MQTT_PASS%"></div>
<div class="row"><label for="mqtt_topic">Topic</label><input type="text" id="mqtt_topic" name="mqtt_topic" value="%MQTT_TOPIC%" placeholder="%MQTT_TOPIC_DEFAULT%"></div>
<div class="row"><label class="check"><input type="checkbox" name="hass_register" value="1"%HASS_CHECKED%>Register in Home Assistant</label>
<p class="hint">On the next wakeup the Home Assistant MQTT discovery config is published once, then this unchecks itself.</p></div>
</div>

<button type="submit"><span>Save and reboot</span></button></form>
<script>
document.getElementById('mqtt_enabled').addEventListener('change', function () {
  document.getElementById('mqtt_fields').style.display = this.checked ? '' : 'none';
});
</script>
)rawliteral";

WebPortal::WebPortal() : server(80), restartFlag(false), themeChangedFlag(false), routesSet(false),
                         batteryVoltage(0) {}

void WebPortal::setBatteryVoltage(float volts)
{
    batteryVoltage = volts;
}

bool WebPortal::begin()
{
    Serial.println("[WEB] Starting web portal...");

    // begin() may be called again after stop()
    if (!routesSet) {
        setupRoutes();
        routesSet = true;
    }
    server.begin();

    dnsServer.start(53, "*", WiFi.softAPIP());

    Serial.println("[WEB] Web portal started on port 80");
    return true;
}

void WebPortal::stop()
{
    dnsServer.stop();
    server.stop();
    Serial.println("[WEB] Web portal stopped");
}

void WebPortal::setupRoutes()
{
    server.on("/", HTTP_GET, [this]()
              { handleRoot(); });
    server.on("/", HTTP_POST, [this]()
              { handleFormSubmit(); });

    server.on("/calibrate", HTTP_GET, [this]()
    {
            handleCalibrate();
    });

    server.on("/diag", HTTP_GET, [this]()
              { server.send(200, "text/html", diagnosticsHtml()); });

    server.on("/theme", HTTP_POST, [this]()
              { handleTheme(); });

    for (const WebFont& font : WEB_FONTS) {
        server.on(font.path, HTTP_GET, [this, &font]()
                  {
            server.sendHeader("Cache-Control", "public, max-age=31536000");
            server.send_P(200, "font/woff2", (const char*)font.data, font.size); });
    }

    // Captive portal detection
    server.on("/generate_204", HTTP_GET, [this]()
              { handleRoot(); });
    server.on("/fwlink", HTTP_GET, [this]()
              { handleRoot(); });

    server.onNotFound([this]()
                      {
        Serial.printf("[WEB] Request not found: %s %s\n", 
                      server.method() == HTTP_GET ? "GET" : "POST",
                      server.uri().c_str());
        for (int i = 0; i < server.args(); i++) {
            Serial.printf("  Arg %d: %s = %s\n", i, server.argName(i).c_str(), server.arg(i).c_str());
        }
        handleRoot(); });
}

void WebPortal::handleClient()
{
    dnsServer.processNextRequest();
    server.handleClient();
}

bool WebPortal::shouldRestart()
{
    return restartFlag;
}

bool WebPortal::themeChanged()
{
    bool changed = themeChangedFlag;
    themeChangedFlag = false;
    return changed;
}

void WebPortal::handleRoot()
{
    String html = getHTMLHeader();
    html += getSettingsPage();
    html += getHTMLFooter();

    server.send(200, "text/html", html);
}

void WebPortal::handleFormSubmit()
{
    Serial.println("[WEB] Processing form submission...");

    if (server.hasArg("ssid"))
    {
        json["ssid"] = server.arg("ssid");
        if (server.arg("ssid").length() > 0)
        {
            json["offline_mode"] = false;
        }
    }
    if (server.hasArg("pass"))
    {
        String pass = server.arg("pass");
        if (pass != "placeholder")
        {
            json["pass"] = pass;
        }
    }

    // Unchecked checkboxes are not submitted at all
    json["mqtt_enabled"] = server.hasArg("mqtt_enabled");
    json["hass_register"] = server.hasArg("mqtt_enabled") && server.hasArg("hass_register");
    if (server.hasArg("mqtt_broker"))
    {
        json["mqtt_broker"] = server.arg("mqtt_broker");
    }
    if (server.hasArg("mqtt_port"))
    {
        json["mqtt_port"] = server.arg("mqtt_port").toInt();
    }
    if (server.hasArg("mqtt_user"))
    {
        json["mqtt_user"] = server.arg("mqtt_user");
    }
    if (server.hasArg("mqtt_pass"))
    {
        String mqttPass = server.arg("mqtt_pass");
        if (mqttPass != "placeholder")
        {
            json["mqtt_pass"] = mqttPass;
        }
    }
    if (server.hasArg("mqtt_topic"))
    {
        String topic = server.arg("mqtt_topic");
        topic.trim();
        json["mqtt_topic"] = topic;
    }
    // Per-value topics of older firmware (now one JSON state message)
    json.remove("temp_topic");
    json.remove("humidity_topic");
    json.remove("battery_topic");

    if (server.hasArg("wake_interval"))
    {
        json["wake_interval"] = server.arg("wake_interval").toInt();
    }
    if (server.hasArg("temp_scale"))
    {
        json["temp_scale"] = server.arg("temp_scale");
    }
    if (server.hasArg("co2_sensor"))
    {
        json["co2_sensor"] = server.arg("co2_sensor");
    }

    saveConfig();

    String response = getHTMLHeader();
    response += getPageStart("Settings");
    response += "<h2>Saved</h2>";
    response += "<p>The device restarts in about 5 seconds.</p>";
    response += "<script>setTimeout(function(){window.location.href='/';},5000);</script>";
    response += getHTMLFooter();

    server.send(200, "text/html", response);

    restartFlag = true;
}

void WebPortal::handleCalibrate()
{
    Serial.println("[WEB] Calibration request received");

    const char* backLink = "<p class=\"links\"><a href=\"/\">Back to the settings</a></p>";
    String html = getHTMLHeader();
    html += getPageStart("CO2 sensor calibration");

    if (!co2SensorEnabled()) {
        html += "<p class=\"status\">The CO2 sensor is disabled in the settings.</p>";
        html += backLink;
        html += getHTMLFooter();
        server.send(200, "text/html", html);
        Serial.println("[CAL] CO2 sensor disabled, nothing to calibrate");
        return;
    }

    html += "<h2>Calibration</h2>";

    if (server.hasArg("cancel")) {
        cancelCalibration();
        html += "<p class=\"status\">Calibration cancelled.</p>";
        Serial.println("[CAL] Calibration cancelled");
    } else {
        armCalibration();
        html += "<p class=\"status\">Calibration armed for the next measurement.</p>";
        html += "<p>Leave the device in air at " + String(CALIBRATION_PPM) + " ppm (outdoor air), exit the "
                "server with the [SLEEP] button and let it take the calibration measurement.</p>";
        html += "<p class=\"links\"><a href=\"/calibrate?cancel=1\">Cancel the calibration</a></p>";
        Serial.println("[CAL] Calibration armed for the next measurement");
    }

    html += backLink;
    html += getHTMLFooter();
    server.send(200, "text/html", html);
}

void WebPortal::handleTheme()
{
    bool dark = server.arg("dark") != "0";
    if (dark != darkMode()) {
        setDarkMode(dark);
        themeChangedFlag = true;
    }
    server.send(204, "text/plain", "");
}

String WebPortal::getHTMLHeader()
{
    String header = String(FPSTR(HTML_HEADER));
    header.replace("%THEME%", darkMode() ? "" : " data-theme=\"light\"");
    return header;
}

// For pages other than the settings page; HTML_FOOTER closes the container and the wrapper
String WebPortal::getPageStart(const char* subtitle)
{
    return "<div class=\"wrapper\">" + getTitleBlock(subtitle) + "<div class=\"container\">";
}

String WebPortal::getTitleBlock(const char* subtitle)
{
    // Rough Li-ion scale
    int bars = batteryVoltage >= 4.0f ? 4 : batteryVoltage >= 3.85f ? 3 : batteryVoltage >= 3.7f ? 2 : batteryVoltage >= 3.55f ? 1 : 0;
    String block = String(FPSTR(TITLE_BLOCK));
    block.replace("%SUBTITLE%", subtitle);
    block.replace("%FW_VERSION%", FW_VERSION);
    block.replace("%DEVICE_NAME%", "Ambien-" + deviceSuffix());
    bool charging = batteryCharging(batteryVoltage);
    block.replace("%BATTERY_LEVEL%", charging ? String("charging") : "l" + String(bars));
    block.replace("%BATTERY_VOLTAGE%", charging ? String("Charging") : String(batteryVoltage, 2) + " V");
    return block;
}

String WebPortal::getHTMLFooter()
{
    String footer = String(FPSTR(HTML_FOOTER));
    footer.replace("%FW_VERSION%", FW_VERSION);
    return footer;
}

// "" instead of "null" for a missing key
static String configString(const char *key)
{
    return json[key].isNull() ? String("") : json[key].as<String>();
}

String WebPortal::getSettingsPage()
{
    String page = String(FPSTR(SETTINGS_PAGE));
    page.replace("%TITLE%", getTitleBlock("CO2 + Temperature + Humidity"));
    int wake = json["wake_interval"].isNull() ? 30 : json["wake_interval"].as<int>();
    bool fahrenheit = configString("temp_scale") == "fahrenheit";
    bool mqtt = mqttEnabled();

    page.replace("%SSID%", htmlEscape(configString("ssid")));
    page.replace("%PASS%", configString("pass").length() ? "placeholder" : "");
    page.replace("%WAKE_1%", wake == 1 ? " selected" : "");
    page.replace("%WAKE_10%", wake == 10 ? " selected" : "");
    page.replace("%WAKE_30%", wake == 30 ? " selected" : "");
    page.replace("%WAKE_60%", wake == 60 ? " selected" : "");
    page.replace("%SCALE_C%", fahrenheit ? "" : " selected");
    page.replace("%SCALE_F%", fahrenheit ? " selected" : "");
    page.replace("%CO2_ON%", co2SensorEnabled() ? " selected" : "");
    page.replace("%CO2_OFF%", co2SensorEnabled() ? "" : " selected");
    page.replace("%MQTT_CHECKED%", mqtt ? " checked" : "");
    page.replace("%MQTT_STYLE%", mqtt ? "" : "display:none");
    page.replace("%MQTT_BROKER%", htmlEscape(configString("mqtt_broker")));
    page.replace("%MQTT_PORT%", json["mqtt_port"].isNull() ? String("1883") : String(json["mqtt_port"].as<int>()));
    page.replace("%MQTT_USER%", htmlEscape(configString("mqtt_user")));
    page.replace("%MQTT_PASS%", configString("mqtt_pass").length() ? "placeholder" : "");
    page.replace("%MQTT_TOPIC%", htmlEscape(configString("mqtt_topic")));
    page.replace("%MQTT_TOPIC_DEFAULT%", "ambien/" + mqtt_deviceId());
    page.replace("%HASS_CHECKED%", hassRegisterPending() ? " checked" : "");
    page.replace("%CALIBRATE_LINK%", co2SensorEnabled()
        ? "<p class=\"hint\"><a href=\"/calibrate\">Calibrate CO2 sensor</a></p>"
        : "");
    return page;
}
