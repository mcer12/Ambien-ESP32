#include "diagnostics.h"
#include <WiFi.h>
#include <Wire.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_flash.h>
#include <esp_sleep.h>
#include <esp_adc_cal.h>
#include "utils.h"
#include "scd4x_fns.h"
#include "sht30_fns.h"

// SCD4x commands missing in the sparkfun lib
static const uint16_t SCD4X_GET_AMBIENT_PRESSURE = 0xE000;
static const uint16_t SCD4X_GET_SENSOR_VARIANT = 0x202F;
static const uint16_t SCD4X_GET_ASC_TARGET = 0x233F;
static const uint16_t SCD4X_GET_ASC_INITIAL_PERIOD = 0x2340;
static const uint16_t SCD4X_GET_ASC_STANDARD_PERIOD = 0x234B;

static const uint8_t SHT30_ADDRESS = 0x44;

static void section(String& html, const char* title) {
    html += "<h3>";
    html += title;
    html += "</h3><table>";
}

static void row(String& html, const char* label, const String& value) {
    html += "<tr><td>";
    html += label;
    html += "</td><td>";
    html += htmlEscape(value);
    html += "</td></tr>";
}

static void endSection(String& html) {
    html += "</table>";
}

static String hex(uint32_t value, int digits) {
    char text[12];
    snprintf(text, sizeof(text), "0x%0*lX", digits, (unsigned long)value);
    return String(text);
}

static String onOff(bool on) {
    return on ? "On" : "Off";
}

static void addScd4x(String& html) {
    section(html, "CO2 sensor (SCD4x)");
    if (!co2SensorEnabled()) {
        row(html, "Status", "Disabled in the settings");
        endSection(html);
        return;
    }

    uint16_t word;
    // Normally idle during a session; a calibration countdown leaves it measuring
    bool wasMeasuring = scd4x_isMeasuring();
    if (wasMeasuring) {
        // Only readable while measuring
        row(html, "Ambient pressure", scd4x.readRegister(SCD4X_GET_AMBIENT_PRESSURE, &word)
            ? String(word * 100UL) + " Pa" : String("Read failed"));
        if (!scd4x_stop(500)) {
            row(html, "Settings", "Read failed: the sensor did not stop measuring");
            endSection(html);
            return;
        }
    }

    char serial[13];
    row(html, "Serial number", scd4x.getSerialNumber(serial) ? String(serial) : String("Read failed"));

    if (scd4x.readRegister(SCD4X_GET_SENSOR_VARIANT, &word)) {
        uint8_t variant = word >> 12;
        row(html, "Variant", variant == 0 ? String("SCD40") : variant == 1 ? String("SCD41") : variant == 5
            ? String("SCD43") : "Unknown (" + hex(word, 4) + ")");
    } else {
        row(html, "Variant", "Read failed");
    }

    float offset;
    row(html, "Temperature offset", scd4x.getTemperatureOffset(&offset) ? String(offset, 1) + " °C" : String("Read failed"));
    uint16_t altitude;
    row(html, "Altitude", scd4x.getSensorAltitude(&altitude) ? String(altitude) + " m" : String("Read failed"));
    uint16_t asc;
    row(html, "Self-calibration (ASC)", scd4x.getAutomaticSelfCalibrationEnabled(&asc) ? onOff(asc == 1) : String("Read failed"));
    // Newer sensor firmware only: older SCD40s don't acknowledge these
    row(html, "ASC target", scd4x.readRegister(SCD4X_GET_ASC_TARGET, &word) ? String(word) + " ppm" : String("Not supported"));
    row(html, "ASC initial period", scd4x.readRegister(SCD4X_GET_ASC_INITIAL_PERIOD, &word) ? String(word) + " h" : String("Not supported"));
    row(html, "ASC standard period", scd4x.readRegister(SCD4X_GET_ASC_STANDARD_PERIOD, &word) ? String(word) + " h" : String("Not supported"));
    row(html, "Forced recalibration", calibrationPending() ? String("Armed for the next measurement")
        : String("Not armed"));
    row(html, "Last correction", json["cal_correction"].isNull() ? String("Never calibrated")
        : String(json["cal_correction"].as<float>(), 1) + " ppm to " + String(CALIBRATION_PPM) + " ppm");

    if (wasMeasuring) {
        row(html, "Measurement", scd4x_start() ? String("Restarted, low power (every 30 s)") : String("Failed to restart"));
    } else {
        row(html, "Measurement", "Idle, starts when the server closes");
    }
    endSection(html);
}

static uint8_t sensirionCrc8(const uint8_t* data) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = crc & 0x80 ? (crc << 1) ^ 0x31 : crc << 1;
    }
    return crc;
}

static bool readSht30Serial(uint32_t& serial) {
    Wire.beginTransmission(SHT30_ADDRESS);
    Wire.write(0x37);
    Wire.write(0x80);
    if (Wire.endTransmission() != 0) return false;
    delay(1);
    uint8_t data[6];
    if (Wire.requestFrom(SHT30_ADDRESS, (uint8_t)6) != 6) return false;
    for (uint8_t& b : data) b = Wire.read();
    if (sensirionCrc8(data) != data[2] || sensirionCrc8(data + 3) != data[5]) return false;
    serial = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | data[3] << 8 | data[4];
    return true;
}

static void addSht30(String& html) {
    section(html, "Temperature and humidity sensor (SHT30)");

    uint32_t serial;
    row(html, "Serial number", readSht30Serial(serial) ? hex(serial, 8) : String("Read failed"));

    uint16_t status;
    if (sht30.readStatusRegister(status) == 0) {
        row(html, "Status register", hex(status, 4));
        row(html, "Heater", onOff(status & (1 << 13)));
        row(html, "Alert pending", status & (1 << 15) ? "Yes" : "No");
        row(html, "Reset since last clear", status & (1 << 4) ? "Yes" : "No");
    } else {
        row(html, "Status register", "Read failed");
    }
    endSection(html);
}

static const char* resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "Power-on";
        case ESP_RST_EXT: return "External pin";
        case ESP_RST_SW: return "Software (esp_restart)";
        case ESP_RST_PANIC: return "Exception / panic";
        case ESP_RST_INT_WDT: return "Interrupt watchdog";
        case ESP_RST_TASK_WDT: return "Task watchdog";
        case ESP_RST_WDT: return "Other watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep wakeup";
        case ESP_RST_BROWNOUT: return "Brownout";
        case ESP_RST_SDIO: return "SDIO";
        default: return "Unknown";
    }
}

static const char* wakeupCauseName(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "None (not from deep sleep)";
        case ESP_SLEEP_WAKEUP_EXT1: return "Button (EXT1)";
        case ESP_SLEEP_WAKEUP_TIMER: return "Timer";
        default: return "Other";
    }
}

static const char* flashModeName(FlashMode_t mode) {
    switch (mode) {
        case FM_QIO: return "QIO";
        case FM_QOUT: return "QOUT";
        case FM_DIO: return "DIO";
        case FM_DOUT: return "DOUT";
        case FM_FAST_READ: return "Fast read";
        case FM_SLOW_READ: return "Slow read";
        default: return "Unknown";
    }
}

static String megabytes(uint32_t bytes) {
    return String(bytes / (1024.0f * 1024.0f), bytes % (1024 * 1024) ? 2 : 0) + " MB";
}

static String kilobytes(uint32_t bytes) {
    return String(bytes / 1024) + " KB";
}

static void addEsp32(String& html) {
    section(html, "ESP32");
    uint16_t revision = ESP.getChipRevision();  // MXX: major version M, minor XX
    row(html, "Chip", String(ESP.getChipModel()) + " rev " + String(revision / 100) + "." + String(revision % 100));
    row(html, "Cores", String(ESP.getChipCores()));
    row(html, "CPU frequency", String(ESP.getCpuFreqMHz()) + " MHz");
    uint64_t mac = ESP.getEfuseMac();
    row(html, "MAC address", macToStr((const uint8_t*)&mac));
    uint32_t physicalFlash = 0;
    esp_flash_get_physical_size(NULL, &physicalFlash);
    row(html, "Flash", megabytes(ESP.getFlashChipSize()) + " (chip " + megabytes(physicalFlash) + "), "
        + String(ESP.getFlashChipSpeed() / 1000000) + " MHz " + flashModeName(ESP.getFlashChipMode()));
    row(html, "PSRAM", ESP.getPsramSize() ? megabytes(ESP.getPsramSize()) : String("None"));
    row(html, "Firmware", String("v" FW_VERSION ", built " __DATE__ " " __TIME__));
    row(html, "Firmware size", kilobytes(ESP.getSketchSize()) + " of " + kilobytes(ESP.getSketchSize() + ESP.getFreeSketchSpace()));
    row(html, "Arduino core / ESP-IDF", String(ESP_ARDUINO_VERSION_STR " / ") + ESP.getSdkVersion());
    row(html, "Free heap", kilobytes(ESP.getFreeHeap()) + " of " + kilobytes(ESP.getHeapSize())
        + " (lowest " + kilobytes(ESP.getMinFreeHeap()) + ")");
    row(html, "Uptime", String(millis() / 1000) + " s");
    row(html, "Reset reason", resetReasonName(esp_reset_reason()));
    row(html, "Wakeup cause", wakeupCauseName(esp_sleep_get_wakeup_cause()));
    esp_adc_cal_characteristics_t adc;
    esp_adc_cal_value_t source = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc);
    row(html, "ADC calibration", source == ESP_ADC_CAL_VAL_EFUSE_TP ? String("Two-point (eFuse)")
        : source == ESP_ADC_CAL_VAL_EFUSE_VREF ? "eFuse Vref " + String(adc.vref) + " mV"
        : "Default Vref " + String(adc.vref) + " mV");
    endSection(html);
}

static uint32_t efuseField(const esp_efuse_desc_t* field[]) {
    uint32_t value = 0;
    esp_efuse_read_field_blob(field, &value, esp_efuse_get_field_size(field));
    return value;
}

static void addEfuse(String& html) {
    section(html, "eFuse");
    row(html, "Chip package", String(efuseField(ESP_EFUSE_CHIP_PACKAGE) | efuseField(ESP_EFUSE_CHIP_PACKAGE_4BIT) << 3));
    row(html, "Rated CPU frequency", efuseField(ESP_EFUSE_CHIP_CPU_FREQ_RATED)
        ? (efuseField(ESP_EFUSE_CHIP_CPU_FREQ_LOW) ? "160 MHz" : "240 MHz") : "Not set (240 MHz)");
    row(html, "Cores disabled", efuseField(ESP_EFUSE_DISABLE_APP_CPU) ? "App CPU" : "None");
    row(html, "Bluetooth", efuseField(ESP_EFUSE_DISABLE_BT) ? "Disabled" : "Enabled");
    // Without XPD_SDIO_FORCE the GPIO12 strapping pin selects the flash voltage at boot
    row(html, "Flash voltage", !efuseField(ESP_EFUSE_XPD_SDIO_FORCE) ? String("Set by GPIO12 at boot")
        : !efuseField(ESP_EFUSE_XPD_SDIO_REG) ? String("Regulator off (3.3 V from VDD3P3_RTC)")
        : efuseField(ESP_EFUSE_XPD_SDIO_TIEH) ? String("3.3 V") : String("1.8 V"));
    static const char* const CODING_SCHEMES[] = {"None", "3/4", "Repeat", "None"};
    row(html, "Coding scheme", CODING_SCHEMES[efuseField(ESP_EFUSE_CODING_SCHEME) & 3]);
    // Encryption is on while an odd number of FLASH_CRYPT_CNT bits is burnt
    row(html, "Flash encryption", __builtin_popcount(efuseField(ESP_EFUSE_FLASH_CRYPT_CNT)) % 2 ? "On" : "Off");
    row(html, "Secure boot", efuseField(ESP_EFUSE_ABS_DONE_1) ? "V2" : efuseField(ESP_EFUSE_ABS_DONE_0) ? "V1" : "Off");
    row(html, "JTAG", efuseField(ESP_EFUSE_JTAG_DISABLE) ? "Disabled" : "Enabled");
    row(html, "UART download mode", efuseField(ESP_EFUSE_UART_DOWNLOAD_DIS) ? "Disabled" : "Enabled");
    row(html, "Custom MAC", efuseField(ESP_EFUSE_MAC_VERSION) == 1 ? "Programmed" : "None");
    row(html, "Write / read protection", hex(efuseField(ESP_EFUSE_WR_DIS), 4) + " / " + hex(efuseField(ESP_EFUSE_RD_DIS), 1));
    endSection(html);
}

static void addWiFi(String& html) {
    section(html, "WiFi");
    wifi_mode_t mode = WiFi.getMode();
    if (mode & WIFI_MODE_STA && WiFi.isConnected()) {
        row(html, "Network", WiFi.SSID());
        row(html, "Access point", WiFi.BSSIDstr() + ", channel " + String(WiFi.channel()));
        row(html, "Signal", String(WiFi.RSSI()) + " dBm");
        row(html, "Address", WiFi.localIP().toString() + " / " + WiFi.subnetMask().toString());
        row(html, "Gateway / DNS", WiFi.gatewayIP().toString() + " / " + WiFi.dnsIP().toString());
        row(html, "Hostname", WiFi.getHostname());
        row(html, "Station MAC", WiFi.macAddress());
    }
    if (mode & WIFI_MODE_AP) {
        row(html, "Setup network", WiFi.softAPSSID() + ", " + WiFi.softAPIP().toString());
        row(html, "Connected devices", String(WiFi.softAPgetStationNum()));
        row(html, "Access point MAC", WiFi.softAPmacAddress());
    }
    endSection(html);
}

String diagnosticsHtml() {
    String html;
    addScd4x(html);
    addSht30(html);
    addEsp32(html);
    addEfuse(html);
    addWiFi(html);
    return html;
}
