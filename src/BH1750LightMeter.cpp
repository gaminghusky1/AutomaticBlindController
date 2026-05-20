#include "BH1750LightMeter.h"

#include <Preferences.h>
#include <Wire.h>

extern Preferences pref;

void BH1750LightMeter::begin(const uint8_t sda, const uint8_t scl) {
    sdaPin = sda;
    sclPin = scl;
    beginI2CBus();
    pref.begin("LightMeter", true);
    measurementDelayMs = pref.getULong("delayMs", DEFAULT_MEASUREMENT_DELAY_MS);
    pref.end();
    lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE, 0x23, &Wire);
}

void BH1750LightMeter::update() {
    unsigned long currentMillis = millis();

    switch (state) {
        case WAITING:
            if (currentMillis - lastMeasurementMillis >= measurementDelayMs) {
                if (configureMeasurement()) {
                    measurementStartMillis = currentMillis;
                    state = MEASURING;
                } else {
                    lastMeasurementMillis = currentMillis;
                }
            }
            break;

        case MEASURING:
            if (currentMillis - measurementStartMillis > MEASUREMENT_WAIT_MS) {
                const float lux = readMeasurement();
                if (lux >= 0.0f) {
                    currentLux = lux;
                    hasNewReading = true;
                }

                lastMeasurementMillis = currentMillis;
                state = WAITING;
            }
            break;
    }
}

unsigned long BH1750LightMeter::getMeasurementDelay() const {
    return measurementDelayMs;
}

void BH1750LightMeter::setMeasurementDelay(const unsigned long newDelayMs) {
    measurementDelayMs = max(newDelayMs, MIN_MEASUREMENT_DELAY_MS);
    saveSettings();
}

void BH1750LightMeter::saveSettings() const {
    pref.begin("LightMeter");
    pref.putULong("delayMs", measurementDelayMs);
    pref.end();
}

float BH1750LightMeter::getLux() {
    hasNewReading = false;
    return currentLux;
}

float BH1750LightMeter::getCurrentLux() const {
    return currentLux;
}

bool BH1750LightMeter::newReadingAvailable() const {
    return hasNewReading;
}

void BH1750LightMeter::beginI2CBus() const {
    Wire.begin(sdaPin, sclPin, I2C_FREQUENCY);
    Wire.setClock(I2C_FREQUENCY);
    Wire.setTimeOut(50);
}

bool BH1750LightMeter::configureMeasurement() {
    if (lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE)) {
        return true;
    }

    beginI2CBus();
    return lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE);
}

float BH1750LightMeter::readMeasurement() {
    const float lux = lightMeter.readLightLevel();
    if (lux < 0.0f) {
        // Reconfigure the sensor so the next scheduled read starts from a clean state.
        beginI2CBus();
        lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE, 0x23, &Wire);
    }
    return lux;
}
