#ifndef SCD4X_FNS_H
#define SCD4X_FNS_H

#include <Arduino.h>
#include <SparkFun_SCD4x_Arduino_Library.h>

extern SCD4x scd4x;

extern uint16_t scd4x_co2;
extern float scd4x_temp;
extern float scd4x_humidity;

void scd4x_begin();
void scd4x_init();
void scd4x_readData();

#endif
