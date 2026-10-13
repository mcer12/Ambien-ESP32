#include "mqtt_fns.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "utils.h"

// Discovery config is ~1 KB; PubSubClient's default buffer (256 bytes) would silently drop it
#define MQTT_BUFFER_SIZE 1536

String mqtt_deviceId() {
    String suffix = deviceSuffix();
    suffix.toLowerCase();
    return "ambien_" + suffix;
}

String mqtt_stateTopic() {
    String base = json["mqtt_topic"].as<String>();
    base.trim();
    while (base.endsWith("/")) base.remove(base.length() - 1);
    if (base.length() == 0 || base == "null") base = "ambien/" + mqtt_deviceId();
    return base + "/state";
}

static bool publishJson(PubSubClient& client, const String& topic, JsonDocument& doc, bool retained) {
    char payload[MQTT_BUFFER_SIZE - 128];  // room for the topic and MQTT header in the client buffer
    size_t length = serializeJson(doc, payload, sizeof(payload));
    if (length == 0 || length >= sizeof(payload) - 1) {
        Serial.printf("[MQTT] Payload for %s too large\n", topic.c_str());
        return false;
    }
    bool ok = client.publish(topic.c_str(), (const uint8_t*)payload, length, retained);
    Serial.printf("[MQTT] %s %s (%u bytes): %s\n", ok ? "Published" : "FAILED", topic.c_str(), (unsigned)length, payload);
    return ok;
}

bool mqtt_publishMeasurement(const AmbienReadings& readings) {
    if (!mqttEnabled()) return false;

    String broker = json["mqtt_broker"].as<String>();
    broker.trim();
    if (broker.length() == 0 || broker == "null") {
        Serial.println("[MQTT] Enabled but no broker set, skipping");
        return false;
    }
    int port = json["mqtt_port"].isNull() ? 1883 : json["mqtt_port"].as<int>();
    if (port <= 0) port = 1883;
    String user = json["mqtt_user"].as<String>();
    String pass = json["mqtt_pass"].as<String>();
    bool auth = user.length() > 0 && user != "null";

    WiFiClient net;
    PubSubClient client(net);
    client.setServer(broker.c_str(), port);
    client.setBufferSize(MQTT_BUFFER_SIZE);
    client.setSocketTimeout(3);

    String id = mqtt_deviceId();
    unsigned long t0 = millis();
    bool connected = auth ? client.connect(id.c_str(), user.c_str(), pass == "null" ? "" : pass.c_str())
                          : client.connect(id.c_str());
    if (!connected) {
        Serial.printf("[MQTT] Connect to %s:%d failed, state %d (%lu ms)\n", broker.c_str(), port, client.state(), millis() - t0);
        return false;
    }
    Serial.printf("[MQTT] Connected to %s:%d as %s (%lu ms)\n", broker.c_str(), port, id.c_str(), millis() - t0);

    String stateTopic = mqtt_stateTopic();
    JsonDocument doc;

    if (hassRegisterPending()) {
        // Device-based discovery, retained: the device sleeps, so it can't answer Home Assistant's birth message
        String name = "Ambien " + deviceSuffix();
        AmbienDevice device = {id.c_str(), name.c_str(), FW_VERSION, stateTopic.c_str(),
                               ambien_mqtt::EXPIRE_AFTER_SECONDS, readings.hasCo2};
        ambien_mqtt::buildDiscovery(doc, device);
        String discoveryTopic = String(ambien_mqtt::DISCOVERY_PREFIX) + "/device/" + id + "/config";
        if (publishJson(client, discoveryTopic, doc, true)) clearHassRegister();
    }

    // Not retained (Home Assistant guideline with expire_after: a retained old state would come back as fresh)
    ambien_mqtt::buildState(doc, readings);
    bool sent = publishJson(client, stateTopic, doc, false);

    client.loop();
    client.disconnect();
    net.stop();
    return sent;
}
