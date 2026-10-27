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

// On unless "dark_mode" is false
bool darkMode();
void setDarkMode(bool on);  // saves the config

// Armed in the portal, performed at the next measurement
bool calibrationPending();
void armCalibration();                     // saves the config
void cancelCalibration();                  // saves the config
void finishCalibration(float correction);  // clears the flag, stores the correction, saves the config

// Above 4.3 V the battery is on the charger: a resting Li-ion cell stays at or below 4.2 V
bool batteryCharging(float voltage);

bool mqttEnabled();
// "hass_register": true, or "yes" from older firmware
bool hassRegisterPending();
void clearHassRegister();  // saves the config

#define FW_VERSION "1.0"

// Last 4 hex digits of the MAC ("A590"), used in the AP name and the MQTT device id
String deviceSuffix();

String macToStr(const uint8_t* mac);

String htmlEscape(String text);

#endif // UTILS_H
