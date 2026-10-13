#include "utils.h"

JsonDocument json;

bool readConfig() {
    if (!LittleFS.exists("/config.json")) {
        Serial.println("[CONFIG] No config file found, using defaults");
        json["ssid"] = "";
        json["pass"] = "";
        json["wake_interval"] = 30;
        return false;
    }

    File file = LittleFS.open("/config.json", "r");
    if (!file) {
        Serial.println("[CONFIG] Failed to open config file");
        return false;
    }

    DeserializationError error = deserializeJson(json, file);
    file.close();

    if (error) {
        Serial.print("[CONFIG] Failed to parse config: ");
        Serial.println(error.c_str());
        return false;
    }

    Serial.println("[CONFIG] Configuration loaded successfully");
    return true;
}

bool saveConfig() {
    File file = LittleFS.open("/config.json", "w");
    if (!file) {
        Serial.println("[CONFIG] Failed to open config file for writing");
        return false;
    }

    if (serializeJson(json, file) == 0) {
        Serial.println("[CONFIG] Failed to write config");
        file.close();
        return false;
    }

    file.close();
    Serial.println("[CONFIG] Configuration saved successfully");
    return true;
}

bool co2SensorEnabled() {
    return json["co2_sensor"].isNull() || String(json["co2_sensor"].as<const char*>()) != "disabled";
}

bool offlineMode() {
    return json["offline_mode"].as<bool>();
}

void setOfflineMode(bool on) {
    json["offline_mode"] = on;
    saveConfig();
}

bool mqttEnabled() {
    return json["mqtt_enabled"].as<bool>();
}

bool hassRegisterPending() {
    if (json["hass_register"].is<bool>()) return json["hass_register"].as<bool>();
    return String(json["hass_register"].as<const char*>()) == "yes";
}

void clearHassRegister() {
    json["hass_register"] = false;
    saveConfig();
}

String deviceSuffix() {
    // getEfuseMac() is little-endian: the last two MAC bytes are bytes 4 and 5
    uint64_t mac = ESP.getEfuseMac();
    uint8_t byte4 = (mac >> 32) & 0xFF, byte5 = (mac >> 40) & 0xFF;
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", byte4, byte5);
    return String(suffix);
}

String macToStr(const uint8_t* mac) {
    String result;
    for (int i = 0; i < 6; ++i) {
        if (mac[i] < 16) result += "0";
        result += String(mac[i], HEX);
        if (i < 5) result += ":";
    }
    result.toUpperCase();
    return result;
}
