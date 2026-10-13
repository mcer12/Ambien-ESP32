#ifndef SHT30_FNS_H
#define SHT30_FNS_H

#include <Arduino.h>
#include <SensirionI2cSht3x.h>

extern SensirionI2cSht3x sht30;

extern float sht30_temp;
extern float sht30_humidity;

void sht30_init();
void sht30_readData();

#endif
