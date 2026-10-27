#ifndef SCD4X_FNS_H
#define SCD4X_FNS_H

#include <Arduino.h>
#include <SparkFun_SCD4x_Arduino_Library.h>

// Shared by the readings and the calibration: a different warm-up for each bakes in a permanent offset
static const uint32_t CO2_WARMUP_S = 180;

// Outdoor air: Sensirion's reference concentration for a forced recalibration
static const uint16_t CALIBRATION_PPM = 430;

extern SCD4x scd4x;

extern uint16_t scd4x_co2;
extern float scd4x_temp;
extern float scd4x_humidity;

void scd4x_begin();
// Stops a measurement left running before this boot; the sensor idles through server sessions
void scd4x_idle();
bool scd4x_start();
// waitMs 0 skips the 500 ms the sensor needs before the next command (fine right before a deep sleep)
bool scd4x_stop(uint16_t waitMs = 0);
// Only knows about measurements started in this boot
bool scd4x_isMeasuring();
void scd4x_readData();
// Stops the measurement and recalibrates to ppm after CO2_WARMUP_S in the mode the readings are taken in
bool scd4x_calibrate(uint16_t ppm, float* correction);

#endif
