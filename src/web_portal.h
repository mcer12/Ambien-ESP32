#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "utils.h"

class WebPortal {
public:
    WebPortal();
    bool begin();
    void stop();
    void handleClient();
    bool shouldRestart();
    void setBatteryVoltage(float volts);
    
private:
    WebServer server;
    DNSServer dnsServer;
    bool restartFlag;
    bool routesSet;
    float batteryVoltage;
    
    void setupRoutes();
    void handleRoot();
    void handleFormSubmit();
    void handleCalibrate();
    
    String getHTMLHeader();
    String getSettingsPage();
    String getPageStart(const char* subtitle);
    String getTitleBlock(const char* subtitle);
    String getHTMLFooter();
};

#endif // WEB_PORTAL_H
