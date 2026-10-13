#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

extern JsonDocument json;

bool readConfig();
bool saveConfig();

// On unless "co2_sensor" is "disabled"
bool co2SensorEnabled();

bool offlineMode();
void setOfflineMode(bool on);  // saves the config

bool mqttEnabled();
// "hass_register": true, or "yes" from older firmware
bool hassRegisterPending();
void clearHassRegister();  // saves the config

#define FW_VERSION "1.0"

// Last 4 hex digits of the MAC ("A590"), used in the AP name and the MQTT device id
String deviceSuffix();

String macToStr(const uint8_t* mac);

#endif // UTILS_H
