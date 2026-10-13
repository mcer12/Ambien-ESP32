#include "scd4x_fns.h"
#include <Wire.h>

SCD4x scd4x;

uint16_t scd4x_co2 = 0;
float scd4x_temp = 0.0f;
float scd4x_humidity = 0.0f;

void scd4x_begin()
{
    scd4x.begin(false, true, true, false);
}

void scd4x_init()
{
    scd4x.begin(false, true, true, false);

    if (scd4x.stopPeriodicMeasurement(500) == true)
    {
        Serial.println("Periodic measurement stopped");
    }

    Serial.print("SCD4x Altitude: ");
    Serial.println(scd4x.getSensorAltitude());

    delay(1);

    Serial.print("SCD4x Temperature offset: ");
    Serial.println(scd4x.getTemperatureOffset());

    delay(1);

    if (scd4x.setSensorAltitude(194, 1) == true)
    {
        Serial.println("Failed to set altitude");
    }

    if (scd4x.startLowPowerPeriodicMeasurement() == true)
    {
        Serial.println("Low power periodic measurement enabled!");
    }
    else
    {
        Serial.println("Failed to start low power measurement");
    }

    Serial.println("SCD4x sensor initialized!");
}

void scd4x_readData()
{
    if (scd4x.readMeasurement())
    {
        scd4x_co2 = scd4x.getCO2();
        scd4x_temp = scd4x.getTemperature();
        scd4x_humidity = scd4x.getHumidity();
        float tempOffset = scd4x.getTemperatureOffset();
        uint16_t altitude = scd4x.getSensorAltitude();

        Serial.print("SCD4x CO2: ");
        Serial.print(scd4x_co2);
        Serial.println(" ppm");
        Serial.print("SCD4x T: ");
        Serial.print(scd4x_temp, 1);
        Serial.println(" °C");
        Serial.print("SCD4x H: ");
        Serial.print(scd4x_humidity, 1);
        Serial.println(" %RH");
        Serial.print("SCD4x T Offset: ");
        Serial.print(tempOffset, 1);
        Serial.println(" ");
        Serial.print("SCD4x Altitude: ");
        Serial.print(altitude);
        Serial.println(" ");
    }
    else
    {
        Serial.println("SCD4x: Waiting for fresh data...");
    }
}
