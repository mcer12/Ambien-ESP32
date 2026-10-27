#include "scd4x_fns.h"
#include <Wire.h>

SCD4x scd4x;

uint16_t scd4x_co2 = 0;
float scd4x_temp = 0.0f;
float scd4x_humidity = 0.0f;

static const uint16_t SENSOR_ALTITUDE_M = 194;

static bool measuring = false;

void scd4x_begin()
{
    scd4x.begin(false, false, true, false);
}

void scd4x_idle()
{
    scd4x.begin(false, false, false, false);
    measuring = false;
    Serial.println("[SCD4x] Idle");
}

bool scd4x_start()
{
    if (!scd4x.begin(false, false, true, false))
    {
        scd4x.stopPeriodicMeasurement(0);
        delay(1000);
        scd4x.begin(false, false, true, false);
    }

    if (!scd4x.setSensorAltitude(SENSOR_ALTITUDE_M, 1))
    {
        Serial.println("[SCD4x] Failed to set altitude");
    }

    if (scd4x.startLowPowerPeriodicMeasurement())
    {
        measuring = true;
        Serial.println("[SCD4x] Low power periodic measurement started");
        return true;
    }
    Serial.println("[SCD4x] Failed to start low power periodic measurement");
    return false;
}

bool scd4x_stop(uint16_t waitMs)
{
    if (scd4x.stopPeriodicMeasurement(waitMs))
    {
        measuring = false;
        Serial.println("[SCD4x] Measurement stopped");
        return true;
    }
    Serial.println("[SCD4x] Failed to stop measurement");
    return false;
}

bool scd4x_isMeasuring()
{
    return measuring;
}

bool scd4x_calibrate(uint16_t ppm, float* correction)
{
    if (!scd4x_stop(500))
    {
        Serial.println("[SCD4x] Calibration aborted: failed to stop the measurement");
        return false;
    }
    if (!scd4x.performForcedRecalibration(ppm, correction))
    {
        Serial.println("[SCD4x] Forced recalibration failed");
        return false;
    }
    if (!scd4x.persistSettings(800))
    {
        Serial.println("[SCD4x] Failed to persist the settings");
    }
    Serial.printf("[SCD4x] Recalibrated to %u ppm, correction %.1f ppm\n", ppm, *correction);
    return true;
}

void scd4x_readData()
{
    if (scd4x.readMeasurement())
    {
        scd4x_co2 = scd4x.getCO2();
        scd4x_temp = scd4x.getTemperature();
        scd4x_humidity = scd4x.getHumidity();

        Serial.print("SCD4x CO2: ");
        Serial.print(scd4x_co2);
        Serial.println(" ppm");
        Serial.print("SCD4x T: ");
        Serial.print(scd4x_temp, 1);
        Serial.println(" °C");
        Serial.print("SCD4x H: ");
        Serial.print(scd4x_humidity, 1);
        Serial.println(" %RH");
    }
    else
    {
        Serial.println("SCD4x: Waiting for fresh data...");
    }
}
