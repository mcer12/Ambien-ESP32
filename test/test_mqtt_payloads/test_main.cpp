// Unit tests for lib/AmbienMqtt (MQTT state message and Home Assistant discovery config).
// On the ESP32: pio test -e esp32dev --upload-port COM6. On a PC with gcc: pio test -e native.

#include <unity.h>
#include <string.h>
#include <math.h>
#include <AmbienMqtt.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

static AmbienReadings readings(bool hasCo2) {
    AmbienReadings r = {23.46f, 45.4f, hasCo2, 812, 4.054f};
    return r;
}

static AmbienDevice device(bool hasCo2) {
    AmbienDevice d = {"ambien_a590", "Ambien A590", "1.0.0", "ambien/ambien_a590/state",
                       ambien_mqtt::EXPIRE_AFTER_SECONDS, hasCo2};
    return d;
}

void setUp() {}
void tearDown() {}

void test_state_values_and_rounding() {
    JsonDocument doc;
    ambien_mqtt::buildState(doc, readings(true));
    // integer checks: the ESP32 Unity build has no float assertions
    TEST_ASSERT_EQUAL(235, (int)lroundf(doc["temperature"].as<float>() * 10));
    TEST_ASSERT_EQUAL(45, (int)lroundf(doc["humidity"].as<float>()));
    TEST_ASSERT_EQUAL(812, doc["co2"].as<int>());
    TEST_ASSERT_EQUAL(405, (int)lroundf(doc["battery"].as<float>() * 100));

    char out[128];
    serializeJson(doc, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("{\"temperature\":23.5,\"humidity\":45,\"co2\":812,\"battery\":4.05}", out);
}

void test_state_without_co2() {
    JsonDocument doc;
    ambien_mqtt::buildState(doc, readings(false));
    TEST_ASSERT_TRUE(doc["co2"].isNull());
    TEST_ASSERT_FALSE(doc["temperature"].isNull());
}

void test_expire_after() {
    TEST_ASSERT_EQUAL_UINT32(7200, ambien_mqtt::EXPIRE_AFTER_SECONDS);  // 2 hours, whatever the wake interval
}

void test_discovery_device_and_origin() {
    JsonDocument doc;
    ambien_mqtt::buildDiscovery(doc, device(true));
    TEST_ASSERT_EQUAL_STRING("ambien_a590", doc["dev"]["ids"][0]);
    TEST_ASSERT_EQUAL_STRING("Ambien A590", doc["dev"]["name"]);
    TEST_ASSERT_EQUAL_STRING("1.0.0", doc["dev"]["sw"]);
    TEST_ASSERT_EQUAL_STRING("NiceThings", doc["dev"]["mf"]);
    TEST_ASSERT_EQUAL_STRING("Ambien-ESP32", doc["o"]["name"]);  // origin "name" is required
    TEST_ASSERT_EQUAL_STRING("ambien/ambien_a590/state", doc["stat_t"]);
}

void test_discovery_components() {
    JsonDocument doc;
    ambien_mqtt::buildDiscovery(doc, device(true));
    JsonObject cmps = doc["cmps"];
    TEST_ASSERT_EQUAL(4, cmps.size());

    // every component: platform, unique id = <device id>_<key>, template reads the state field <key>
    const char* keys[] = {"temperature", "humidity", "co2", "battery"};
    JsonDocument state;
    ambien_mqtt::buildState(state, readings(true));
    for (const char* key : keys) {
        JsonObject c = cmps[key];
        TEST_ASSERT_FALSE_MESSAGE(c.isNull(), key);
        TEST_ASSERT_EQUAL_STRING("sensor", c["p"]);
        char expected[64];
        snprintf(expected, sizeof(expected), "ambien_a590_%s", key);
        TEST_ASSERT_EQUAL_STRING(expected, c["uniq_id"]);
        snprintf(expected, sizeof(expected), "{{ value_json.%s }}", key);
        TEST_ASSERT_EQUAL_STRING(expected, c["val_tpl"]);
        TEST_ASSERT_FALSE_MESSAGE(state[key].isNull(), key);
        TEST_ASSERT_EQUAL_UINT32(7200, c["exp_aft"].as<uint32_t>());
        TEST_ASSERT_EQUAL_STRING("measurement", c["stat_cla"]);
    }
    TEST_ASSERT_EQUAL_STRING("carbon_dioxide", cmps["co2"]["dev_cla"]);
    TEST_ASSERT_EQUAL_STRING("ppm", cmps["co2"]["unit_of_meas"]);
    TEST_ASSERT_EQUAL_STRING("°C", cmps["temperature"]["unit_of_meas"]);
    TEST_ASSERT_EQUAL_STRING("diagnostic", cmps["battery"]["ent_cat"]);
    TEST_ASSERT_TRUE(cmps["temperature"]["ent_cat"].isNull());
}

void test_discovery_without_co2() {
    JsonDocument doc;
    ambien_mqtt::buildDiscovery(doc, device(false));
    TEST_ASSERT_EQUAL(3, doc["cmps"].as<JsonObject>().size());
    TEST_ASSERT_TRUE(doc["cmps"]["co2"].isNull());
}

void test_discovery_fits_mqtt_buffer() {
    JsonDocument doc;
    ambien_mqtt::buildDiscovery(doc, device(true));
    // src/mqtt_fns.cpp serializes into MQTT_BUFFER_SIZE - 128 = 1408 bytes
    TEST_ASSERT_LESS_THAN(1408, measureJson(doc));
}

static int runTests() {
    UNITY_BEGIN();
    RUN_TEST(test_state_values_and_rounding);
    RUN_TEST(test_state_without_co2);
    RUN_TEST(test_expire_after);
    RUN_TEST(test_discovery_device_and_origin);
    RUN_TEST(test_discovery_components);
    RUN_TEST(test_discovery_without_co2);
    RUN_TEST(test_discovery_fits_mqtt_buffer);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() {
    delay(2000);  // let the serial monitor attach
    runTests();
}
void loop() {}
#else
int main() {
    return runTests();
}
#endif
