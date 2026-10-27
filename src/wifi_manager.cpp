#include "wifi_manager.h"

#ifndef AP_NAME_PREFIX
#define AP_NAME_PREFIX "Ambien_"
#endif

WiFiManager::WiFiManager() : connected(false) {}

bool WiFiManager::begin() {
    // Shown by the router (DHCP)
    String hostname = "ambien-" + deviceSuffix();
    hostname.toLowerCase();
    WiFi.setHostname(hostname.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    String ssid = json["ssid"].as<String>();
    String pass = json["pass"].as<String>();

    if (ssid.length() > 0) {
        Serial.println("[WiFi] Attempting to connect to WiFi...");
        if (connectToWiFi()) {
            return true;
        }
    }

    Serial.println("[WiFi] Starting AP mode...");
    return startAP();
}

bool WiFiManager::connectToWiFi() {
    String ssid = json["ssid"].as<String>();
    String pass = json["pass"].as<String>();

    if (ssid.length() == 0) {
        return false;
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.print("[WiFi] Connecting to: ");
    Serial.println(ssid);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        Serial.print("[WiFi] Connected! IP: ");
        Serial.print(WiFi.localIP());
        Serial.print(", hostname: ");
        Serial.println(WiFi.getHostname());
        return true;
    }

    Serial.println("[WiFi] Connection failed");
    return false;
}

bool WiFiManager::startAP() {
    WiFi.mode(WIFI_AP);
    
    String apName = String(AP_NAME_PREFIX) + deviceSuffix();

    IPAddress apIP;
    apIP.fromString(AP_IP);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.softAPConfig(apIP, gateway, subnet);
    
    if (WiFi.softAP(apName.c_str())) {
        Serial.print("[WiFi] AP started: ");
        Serial.println(apName);
        Serial.print("[WiFi] AP IP: ");
        Serial.println(WiFi.softAPIP());
        connected = false;
        return true;
    }

    Serial.println("[WiFi] AP start failed");
    return false;
}

bool WiFiManager::isConnected() {
    return connected && (WiFi.status() == WL_CONNECTED);
}

String WiFiManager::getLocalIP() {
    if (connected) {
        return WiFi.localIP().toString();
    }
    return WiFi.softAPIP().toString();
}

void WiFiManager::disconnect() {
    WiFi.disconnect();
    connected = false;
}
