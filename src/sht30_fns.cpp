#include "sht30_fns.h"
#include <Wire.h>

SensirionI2cSht3x sht30;

float sht30_temp = 0.0f;
float sht30_humidity = 0.0f;

static char errorMessage[64];
static int16_t error;

void sht30_init() {
    sht30.begin(Wire, SHT30_I2C_ADDR_44);

    error = sht30.stopMeasurement();
    if (error) {
        Serial.print("SHT30 Error trying to execute stopMeasurement(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
    }
    delay(1);

    Serial.println("SHT30 sensor initialized!");
}

void sht30_readData() {
    error = sht30.measureSingleShot(REPEATABILITY_HIGH, false, sht30_temp, sht30_humidity);
    if (error) {
        Serial.print("SHT30 Error trying to execute measureSingleShot(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        return;
    }

    Serial.print("SHT30 T: ");
    Serial.print(sht30_temp);
    Serial.println(" °C");
    Serial.print("SHT30 H: ");
    Serial.print(sht30_humidity);
    Serial.println(" %");
}
