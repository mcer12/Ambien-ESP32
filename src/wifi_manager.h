#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "utils.h"

#ifndef AP_IP
#define AP_IP "192.168.0.1"
#endif

class WiFiManager {
public:
    WiFiManager();
    bool begin();
    bool connectToWiFi();
    bool startAP();
    bool isConnected();
    String getLocalIP();
    void disconnect();
    
private:
    bool connected;
};

#endif // WIFI_MANAGER_H
