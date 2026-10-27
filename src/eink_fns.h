#ifndef EINK_FNS_H
#define EINK_FNS_H

#include <Arduino.h>
#include <GxEPD2_BW.h>

#define EPD_CS 5
#define EPD_DC 17
#define EPD_RST 16
#define EPD_BUSY 4

// HINK-E042A13 (SSD1683), no partial refresh!
extern GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display;

void eink_init();
// showCO2 false: no CO2 value, special temperature and humidity screen; empty ssid: offline mode
void eink_printSensorsData(bool showCO2, float batteryVoltage, String ssid, int wakeIntervalMinutes, String btn1Label, String btn2Label);
void eink_displayWebServerInfo(String title, String details, String instructions, float batteryVoltage, unsigned long timeoutMs, String btn1Label, String btn2Label);
// A sleep of a minute or more is announced, so the screen doesn't look stuck
void eink_displayFirstReadingNotice(uint32_t firstReadingS, bool calibrating, String btn1Label, String btn2Label);
void eink_displayCalibrated(uint32_t firstReadingS, String btn1Label, String btn2Label);
void eink_displayUnableToConnect(String ssid, int wakeIntervalMinutes, String btn1Label, String btn2Label);
void eink_displaySetupNeeded(String btn1Label, String btn2Label);

#endif
