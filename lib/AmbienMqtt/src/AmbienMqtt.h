#ifndef AMBIEN_MQTT_H
#define AMBIEN_MQTT_H

// No Arduino / WiFi calls here, so it can be unit tested natively (test/test_mqtt_payloads)

#include <ArduinoJson.h>
#include <stdint.h>

struct AmbienReadings {
    float temperature;     // degrees C
    float humidity;        // %
    bool hasCo2;           // CO2 sensor enabled and the reading is valid
    uint16_t co2;          // ppm
    float batteryVoltage;  // V
};

struct AmbienDevice {
    const char* id;          // [a-zA-Z0-9_-] only
    const char* name;
    const char* firmware;
    const char* stateTopic;
    uint32_t expireAfter;    // seconds
    bool hasCo2;
};

namespace ambien_mqtt {

static const char* const DISCOVERY_PREFIX = "homeassistant";

// {"temperature":23.5,"humidity":45,"co2":812,"battery":4.05}
void buildState(JsonDocument& doc, const AmbienReadings& readings);

// For topic <DISCOVERY_PREFIX>/device/<id>/config
void buildDiscovery(JsonDocument& doc, const AmbienDevice& device);

// Fixed regardless of the wake interval
static const uint32_t EXPIRE_AFTER_SECONDS = 2 * 60 * 60;

}  // namespace ambien_mqtt

#endif
