#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_adc_cal.h>
#include <driver/adc.h>
#include "utils.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "scd4x_fns.h"
#include "sht30_fns.h"
#include "eink_fns.h"
#include "mqtt_fns.h"

#define BUTTON_1 35
#define BUTTON_2 32
#define ADC 34
#define CALIBRATION_PPM 430
#define AP_NAME_PREFIX "Ambien_"

enum DeviceState {
    STATE_TIMER_WAKEUP,
    STATE_BUTTON1_AP,
    STATE_BUTTON2_LOCAL,
    STATE_SW_RESET
};

DeviceState currentState;
unsigned long webserverStartTime = 0;
const unsigned long WEBSERVER_TIMEOUT = 5 * 60 * 1000;
const unsigned long WEBSERVER_TIMEOUT_FORCED = 10 * 60 * 1000;
const unsigned long BUTTON_CHECK_DELAY = 2000;
const uint32_t FIRST_READING_DELAY_S = 5;
float voltage;

// Kept across deep sleep: the last WiFi connection attempt failed, so button 2 means "try again"
RTC_DATA_ATTR bool wifiConnectFailed = false;

WiFiManager wifiManager;
WebPortal webPortal;

enum ServerExit {
    SERVER_EXIT_SLEEP,   // button 2 / timeout -> go to deep sleep
    SERVER_EXIT_SWITCH   // button 1 -> switch AP <-> Local
};

void handleTimerWakeup();
void measureAndSleep(bool online);
void handleButton1AP(unsigned long timeout);
void handleButton2Local(unsigned long timeout);
void handleServerMode(bool apMode, unsigned long timeout);
ServerExit runAPServer(unsigned long timeout);
ServerExit runLocalServer(unsigned long timeout);
ServerExit serverLoop(unsigned long timeout);
String getAPName();
void displayAPServerInfo(unsigned long timeout, String btn1Label, String btn2Label);
bool hasWiFiSettings();
void fallbackAP(String reason);
void sleepUntilSetup(String reason);
void sleepUnableToConnect();
void sleepBeforeFirstReading();
void handleSoftwareReset();
void goToDeepSleep(uint32_t sleepSeconds = 0);
void goToDeepSleepIndefinitely();
int checkButtonPress();
float readBatteryVoltage();

void setup()
{
    Serial.begin(115200);
    
    pinMode(BUTTON_1, INPUT_PULLDOWN);
    pinMode(BUTTON_2, INPUT_PULLDOWN);

    if (!LittleFS.begin(true)) {
        Serial.println("[FS] Failed to mount filesystem");
    }

    readConfig();
    voltage = readBatteryVoltage();

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    esp_reset_reason_t reset_reason = esp_reset_reason();
    
    Serial.print("[MAIN] Wakeup cause: ");
    Serial.println(wakeup_reason);
    Serial.print("[MAIN] Reset reason: ");
    Serial.println(reset_reason);

    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("[MAIN] Timer wakeup - sensor reading mode");
            currentState = STATE_TIMER_WAKEUP;
            break;
            
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
            // GPIO 32 / 35 are above bit 31: print the whole 64-bit mask
            Serial.printf("[MAIN] Button wakeup - pin mask: 0x%llX\n", (unsigned long long)wakeup_pin_mask);
            
            if (wakeup_pin_mask & (1ULL << BUTTON_1)) {
                Serial.println("[MAIN] Button 1 - Force AP mode");
                currentState = STATE_BUTTON1_AP;
            } else if (wakeup_pin_mask & (1ULL << BUTTON_2) && wifiConnectFailed) {
                Serial.println("[MAIN] Button 2 - Try again (last WiFi connection failed)");
                currentState = STATE_TIMER_WAKEUP;
            } else if (wakeup_pin_mask & (1ULL << BUTTON_2)) {
                Serial.println("[MAIN] Button 2 - Local webserver mode");
                currentState = STATE_BUTTON2_LOCAL;
            } else {
                Serial.println("[MAIN] Unknown button - defaulting to timer wakeup");
                currentState = STATE_TIMER_WAKEUP;
            }
            break;
        }
        
        default:
            if (reset_reason == ESP_RST_SW) {
                Serial.println("[MAIN] Software reset detected - config saved");
                currentState = STATE_SW_RESET;
            } else {
                Serial.println("[MAIN] First boot or power-on reset - defaulting to timer wakeup");
                currentState = STATE_TIMER_WAKEUP;
            }
            break;
    }
}

void loop()
{
    switch(currentState) {
        case STATE_TIMER_WAKEUP:
            handleTimerWakeup();
            break;
            
        case STATE_BUTTON1_AP:
        case STATE_BUTTON2_LOCAL:
            // Without WiFi settings (First run? screen / offline mode): button 1 starts the setup AP, button 2
            // switches to offline mode or, already offline, refreshes the measurement
            if (!hasWiFiSettings() && currentState == STATE_BUTTON1_AP) {
                fallbackAP("No settings saved");
            } else if (!hasWiFiSettings()) {
                if (!offlineMode()) {
                    Serial.println("[MAIN] Offline mode selected");
                    setOfflineMode(true);
                }
                measureAndSleep(false);
            } else if (currentState == STATE_BUTTON1_AP) {
                handleButton1AP(WEBSERVER_TIMEOUT_FORCED);
            } else {
                handleButton2Local(WEBSERVER_TIMEOUT_FORCED);
            }
            break;
            
        case STATE_SW_RESET:
            handleSoftwareReset();
            break;
    }
}

void handleTimerWakeup() {
    Serial.println("[MAIN] Executing timer wakeup routine");
    
    if (!hasWiFiSettings() && offlineMode()) {
        Serial.println("[MAIN] Offline mode - measuring without WiFi");
        measureAndSleep(false);
        return;
    }

    if (!hasWiFiSettings()) {
        Serial.println("[MAIN] No WiFi credentials configured - starting fallback AP mode");
        fallbackAP("No settings saved");
        return;
    }

    Serial.println("[MAIN] Attempting to connect to WiFi...");
    wifiManager.begin();

    if (!wifiManager.isConnected()) {
        Serial.println("[MAIN] Failed to connect to WiFi - retrying at the next wakeup");
        sleepUnableToConnect();
        return;
    }
    wifiConnectFailed = false;

    measureAndSleep(true);
}

// Offline mode has no local server, so its second button just takes a new measurement
void measureAndSleep(bool online) {
    Wire.begin();
    if (co2SensorEnabled()) scd4x_begin();
    sht30_init();
    eink_init();

    if (co2SensorEnabled()) scd4x_readData();
    sht30_readData();

    if (online) {
        // Publish first, then switch the radio off before the ~3 s display refresh (battery, less heat)
        AmbienReadings readings = {sht30_temp, sht30_humidity, co2SensorEnabled() && scd4x_co2 > 0, scd4x_co2, voltage};
        mqtt_publishMeasurement(readings);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    int wake_interval = json["wake_interval"].isNull() ? 30 : json["wake_interval"].as<int>();
    eink_printSensorsData(co2SensorEnabled(), voltage, online ? json["ssid"].as<String>() : String(""),
                          wake_interval, "AP MODE", online ? "LOCAL SERVER" : "REFRESH");

    Serial.println("[MAIN] Sensor reading complete, going to deep sleep");

    goToDeepSleep();
}

void handleButton1AP(unsigned long timeout) {
    handleServerMode(true, timeout);
}

void handleButton2Local(unsigned long timeout) {
    handleServerMode(false, timeout);
}

// Button 1 switches AP <-> Local in place; button 2 or the timeout sleeps briefly, then measures
void handleServerMode(bool apMode, unsigned long timeout) {
    Wire.begin();
    if (co2SensorEnabled()) scd4x_init();
    sht30_init();
    eink_init();

    while (true) {
        ServerExit exitReason = apMode ? runAPServer(timeout) : runLocalServer(timeout);

        if (exitReason == SERVER_EXIT_SWITCH) {
            Serial.println(apMode ? "[MAIN] Switching to local server mode" : "[MAIN] Switching to AP mode");
            webPortal.stop();
            apMode = !apMode;
            continue;
        }

        sleepBeforeFirstReading();
    }
}

ServerExit runAPServer(unsigned long timeout) {
    Serial.println("[MAIN] Starting AP mode with captive portal");

    wifiManager.startAP();
    webPortal.setBatteryVoltage(voltage);
    webPortal.begin();

    displayAPServerInfo(timeout, "SWITCH TO LOCAL", "SLEEP");

    return serverLoop(timeout);
}

ServerExit runLocalServer(unsigned long timeout) {
    Serial.println("[MAIN] Starting local webserver mode");

    wifiManager.begin();

    if (!wifiManager.isConnected()) {
        Serial.println("[MAIN] Failed to connect to WiFi, going to deep sleep");
        sleepUnableToConnect();
    }

    webPortal.setBatteryVoltage(voltage);
    webPortal.begin();

    float batteryVoltage = readBatteryVoltage();
    String details = "WiFi: " + WiFi.SSID() + " (signal " + String(WiFi.RSSI()) + " dBm)\n"
                   + "Address: http://" + WiFi.localIP().toString();
    String instructions = "Open the address in a browser on a phone or computer\n"
                          "connected to the same WiFi network.";
    eink_displayWebServerInfo("Local Server", details, instructions, batteryVoltage, timeout, "SWITCH TO AP", "SLEEP");

    return serverLoop(timeout);
}

ServerExit serverLoop(unsigned long timeout) {
    webserverStartTime = millis();

    while (true) {
        webPortal.handleClient();

        if (webPortal.shouldRestart()) {
            Serial.println("[MAIN] Restarting...");
            delay(1000);
            ESP.restart();
        }

        if (millis() - webserverStartTime >= BUTTON_CHECK_DELAY) {
            int button = checkButtonPress();
            if (button == 1) {
                Serial.println("[MAIN] Button 1 pressed, switching mode");
                return SERVER_EXIT_SWITCH;
            }
            if (button == 2) {
                Serial.println("[MAIN] Button 2 pressed, going to deep sleep");
                return SERVER_EXIT_SLEEP;
            }
        }

        if (timeout > 0 && millis() - webserverStartTime >= timeout) {
            Serial.println("[MAIN] Webserver timeout, going to deep sleep");
            return SERVER_EXIT_SLEEP;
        }

        delay(10);
    }
}

String getAPName() {
    return String(AP_NAME_PREFIX) + deviceSuffix();
}

void displayAPServerInfo(unsigned long timeout, String btn1Label, String btn2Label) {
    String details = "SSID: " + getAPName() + "\n"
                   + "Address: http://" + String(AP_IP);
    String instructions = "Connect a phone or computer to this WiFi network.\n"
                          "If the setup page doesn't open, use the IP address.";
    eink_displayWebServerInfo("Access Point", details, instructions, voltage, timeout, btn1Label, btn2Label);
}

bool hasWiFiSettings() {
    const char* ssid = json["ssid"].as<const char*>();
    return ssid != NULL && ssid[0] != '\0';
}

// Setup AP for a device without WiFi settings
void fallbackAP(String reason) {
    Serial.println("[MAIN] Starting fallback AP mode: " + reason);

    Wire.begin();
    if (co2SensorEnabled()) scd4x_init();
    sht30_init();
    eink_init();

    wifiManager.startAP();
    webPortal.setBatteryVoltage(voltage);
    webPortal.begin();

    // No WiFi credentials to switch to: button 1 sleeps, button 2 switches to offline mode
    displayAPServerInfo(WEBSERVER_TIMEOUT_FORCED, "SLEEP", "OFFLINE MODE");

    webserverStartTime = millis();

    while(true) {
        webPortal.handleClient();

        if (webPortal.shouldRestart()) {
            Serial.println("[MAIN] Restarting...");
            delay(1000);
            ESP.restart();
        }

        int button = millis() - webserverStartTime >= BUTTON_CHECK_DELAY ? checkButtonPress() : 0;
        if (button == 1) {
            Serial.println("[MAIN] Button 1 pressed, closing the setup AP");
            sleepUntilSetup(reason);
        }
        if (button == 2) {
            // Offline mode: stop the AP and measure after a short sleep (radio off, electronics cooler)
            Serial.println("[MAIN] Button 2 pressed, offline mode");
            if (!offlineMode()) setOfflineMode(true);
            webPortal.stop();
            goToDeepSleep(FIRST_READING_DELAY_S);
        }

        if (millis() - webserverStartTime >= WEBSERVER_TIMEOUT_FORCED) {
            Serial.println("[MAIN] Fallback AP timeout, going to indefinite deep sleep");
            sleepUntilSetup(reason);
        }

        delay(10);
    }
}

// No timer wakeup: sleeps until a button press (unless already in offline mode)
void sleepUntilSetup(String reason) {
    webPortal.stop();
    if (offlineMode()) {
        Serial.println("[MAIN] Setup AP closed, back to offline mode");
        goToDeepSleep(FIRST_READING_DELAY_S);
    }
    Serial.println("[MAIN] Setup needed: " + reason);
    eink_displaySetupNeeded("AP MODE", "OFFLINE MODE");
    goToDeepSleepIndefinitely();
}

// Keeps the normal wake timer so the device retries at the next interval
void sleepUnableToConnect() {
    wifiConnectFailed = true;
    eink_init();
    int wake_interval = json["wake_interval"].isNull() ? 30 : json["wake_interval"].as<int>();
    eink_displayUnableToConnect(json["ssid"].as<String>(), wake_interval, "AP MODE", "TRY AGAIN");
    goToDeepSleep();
}

void sleepBeforeFirstReading() {
    eink_displayFirstReadingNotice("AP MODE", "LOCAL SERVER");
    goToDeepSleep(FIRST_READING_DELAY_S);
}

// Returns 1 or 2 for the pressed button (after release + debounce), 0 if none
int checkButtonPress() {
    int button = 0;
    if (digitalRead(BUTTON_1) == HIGH) button = 1;
    else if (digitalRead(BUTTON_2) == HIGH) button = 2;

    if (button == 0) {
        return 0;
    }

    Serial.printf("[MAIN] Button %d press detected, waiting for release...\n", button);

    unsigned long pressTime = millis();

    while (digitalRead(BUTTON_1) == HIGH || digitalRead(BUTTON_2) == HIGH) {
        delay(10);
        if (millis() - pressTime > 10000) {
            Serial.println("[MAIN] Button held too long, proceeding...");
            break;
        }
    }

    // debounce
    unsigned long releaseTime = millis();
    while (millis() - releaseTime < 1000) {
        delay(10);
    }

    return button;
}

void handleSoftwareReset() {
    // Software reset happens after the portal saves a new config.
    Serial.println("[MAIN] Handling software reset");
    
    if (!hasWiFiSettings()) {
        Serial.println("[MAIN] No WiFi credentials configured - starting fallback AP mode");
        fallbackAP("No settings saved");
        return;
    }

    Serial.println("[MAIN] Attempting to connect to WiFi...");
    wifiManager.begin();

    if (!wifiManager.isConnected()) {
        Serial.println("[MAIN] Failed to connect to WiFi - retrying at the next wakeup");
        sleepUnableToConnect();
        return;
    }
    wifiConnectFailed = false;

    Serial.println("[MAIN] WiFi connected successfully - showing first reading notice");
    eink_init();
    sleepBeforeFirstReading();
}

// Deep sleep with timer + button wakeup; sleepSeconds 0 = the configured wake interval
void goToDeepSleep(uint32_t sleepSeconds) {
    Serial.println("[MAIN] Configuring deep sleep");

    if (sleepSeconds == 0) {
        int wake_interval = json["wake_interval"].isNull() ? 30 : json["wake_interval"].as<int>();
        Serial.print("[MAIN] Wake interval: ");
        Serial.print(wake_interval);
        Serial.println(" minutes");
        sleepSeconds = wake_interval * 60;
    } else {
        Serial.printf("[MAIN] Sleeping for %u s\n", sleepSeconds);
    }

    uint64_t sleep_time_us = sleepSeconds * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleep_time_us);
    
    uint64_t button_mask = (1ULL << BUTTON_1) | (1ULL << BUTTON_2);
    esp_sleep_enable_ext1_wakeup(button_mask, ESP_EXT1_WAKEUP_ANY_HIGH);
    
    esp_deep_sleep_start();

    delay(100);
}

void goToDeepSleepIndefinitely() {
    Serial.println("[MAIN] Configuring indefinite deep sleep (button wakeup only)");
    
    uint64_t button_mask = (1ULL << BUTTON_1) | (1ULL << BUTTON_2);
    esp_sleep_enable_ext1_wakeup(button_mask, ESP_EXT1_WAKEUP_ANY_HIGH);
    
    Serial.println("[MAIN] Device will sleep until button press");
    
    esp_deep_sleep_start();

    delay(100);
}

float readBatteryVoltage() {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_12);
    
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    int adcValue = adc1_get_raw(ADC1_CHANNEL_6);

    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adcValue, &adc_chars);
    float voltage = (voltage_mv / 1000.0) * 2.0;  // x2 voltage divider
    Serial.printf("[ADC] Battery voltage: %.2fV\n", voltage);
    
    return voltage;
}
