#ifndef MQTT_FNS_H
#define MQTT_FNS_H

#include <Arduino.h>
#include <AmbienMqtt.h>

String mqtt_deviceId();
String mqtt_stateTopic();

// Also publishes the Home Assistant discovery config when a registration is pending
bool mqtt_publishMeasurement(const AmbienReadings& readings);

#endif
