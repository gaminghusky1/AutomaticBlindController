#pragma once

#include <Arduino.h>
#include <BH1750.h>

class BH1750LightMeter {
public:
    void begin(uint8_t sda=SDA, uint8_t scl=SCL); // SDA = 21, SCL = 22
    void update();

    unsigned long getMeasurementDelay() const;

    void setMeasurementDelay(unsigned long newDelayMs);
    void saveSettings() const;
    bool newReadingAvailable() const;

    float getLux();
    float getCurrentLux() const;

private:
    enum LightMeterState {
        WAITING,
        MEASURING
    };

    BH1750 lightMeter;
    LightMeterState state = WAITING;

    unsigned long lastMeasurementMillis = 0;
    unsigned long measurementStartMillis = 0;

    float currentLux = 0.0f;
    bool hasNewReading = false;

    unsigned long measurementDelayMs = 1UL * 60UL * 1000UL;
    uint8_t sdaPin = SDA;
    uint8_t sclPin = SCL;

    static constexpr unsigned long MEASUREMENT_WAIT_MS = 180UL;
    static constexpr unsigned long DEFAULT_MEASUREMENT_DELAY_MS = 2UL * 1000UL;
    static constexpr unsigned long MIN_MEASUREMENT_DELAY_MS = 500UL;
    static constexpr uint32_t I2C_FREQUENCY = 100000UL;

    void beginI2CBus() const;
    bool configureMeasurement();
    float readMeasurement();
};
