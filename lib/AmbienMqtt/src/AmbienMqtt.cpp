#include "AmbienMqtt.h"
#include <math.h>
#include <stdio.h>

namespace ambien_mqtt {

static float roundTo(float value, int decimals) {
    float factor = decimals == 2 ? 100.0f : decimals == 1 ? 10.0f : 1.0f;
    return roundf(value * factor) / factor;
}

void buildState(JsonDocument& doc, const AmbienReadings& readings) {
    doc.clear();
    doc["temperature"] = roundTo(readings.temperature, 1);
    doc["humidity"] = roundTo(readings.humidity, 0);
    if (readings.hasCo2) doc["co2"] = readings.co2;
    doc["battery"] = roundTo(readings.batteryVoltage, 2);
}

// key is both the component id and the field name in the state message
static void addSensor(JsonObject components, const AmbienDevice& device, const char* key, const char* name,
                      const char* deviceClass, const char* unit, int precision, bool diagnostic) {
    JsonObject c = components[key].to<JsonObject>();
    c["p"] = "sensor";
    c["name"] = name;
    c["dev_cla"] = deviceClass;
    c["unit_of_meas"] = unit;
    c["stat_cla"] = "measurement";
    c["sug_dsp_prc"] = precision;
    if (diagnostic) c["ent_cat"] = "diagnostic";
    char tpl[48];
    snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", key);
    c["val_tpl"] = tpl;
    char uid[64];
    snprintf(uid, sizeof(uid), "%s_%s", device.id, key);
    c["uniq_id"] = uid;
    c["exp_aft"] = device.expireAfter;
}

void buildDiscovery(JsonDocument& doc, const AmbienDevice& device) {
    doc.clear();

    JsonObject dev = doc["dev"].to<JsonObject>();
    dev["ids"].to<JsonArray>().add(device.id);
    dev["name"] = device.name;
    dev["mf"] = "NiceThings";
    dev["mdl"] = "Ambien-ESP32";
    dev["sw"] = device.firmware;

    JsonObject origin = doc["o"].to<JsonObject>();
    origin["name"] = "Ambien-ESP32";
    origin["sw"] = device.firmware;
    origin["url"] = "https://github.com/mcer12/Ambien-ESP32";

    doc["stat_t"] = device.stateTopic;

    JsonObject components = doc["cmps"].to<JsonObject>();
    addSensor(components, device, "temperature", "Temperature", "temperature", "°C", 1, false);
    addSensor(components, device, "humidity", "Humidity", "humidity", "%", 0, false);
    if (device.hasCo2) addSensor(components, device, "co2", "CO2", "carbon_dioxide", "ppm", 0, false);
    addSensor(components, device, "battery", "Battery voltage", "voltage", "V", 2, true);
}

}  // namespace ambien_mqtt
