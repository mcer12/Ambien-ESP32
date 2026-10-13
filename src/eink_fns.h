#ifndef EINK_FNS_H
#define EINK_FNS_H

#include <Arduino.h>
#include <GxEPD2_BW.h>

#define EPD_CS 5
#define EPD_DC 17
#define EPD_RST 16
#define EPD_BUSY 4

// HINK-E042A13 (SSD1683): fast full refresh comes out gray and partial refresh darkens it, neither is used
extern GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display;

void eink_init();
// showCO2 false: no CO2 value, temperature and humidity stacked instead of side by side; empty ssid: offline mode
void eink_printSensorsData(bool showCO2, float batteryVoltage, String ssid, int wakeIntervalMinutes, String btn1Label, String btn2Label);
void eink_displayWebServerInfo(String title, String details, String instructions, float batteryVoltage, unsigned long timeoutMs, String btn1Label, String btn2Label);
// Before the short sleep after a server session: the first reading after it can be skewed by radio heat
void eink_displayFirstReadingNotice(String btn1Label, String btn2Label);
void eink_displayUnableToConnect(String ssid, int wakeIntervalMinutes, String btn1Label, String btn2Label);
void eink_displaySetupNeeded(String btn1Label, String btn2Label);

#endif
