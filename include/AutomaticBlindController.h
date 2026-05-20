#pragma once

#include <Arduino.h>
#include "Somfy.h"

class AutomaticBlindController {
public:
    void begin(SomfyShade* somfyShade);
    void loop(float lux);

    bool isEnabled() const;
    void setEnabled(bool enabled);
    const char* getStateName() const;
    int8_t getStatusCode() const;
    unsigned long getRemainingWaitTime() const;

    float getDownLuxThreshold() const;
    float getUpLuxThreshold() const;

    unsigned long getDownWaitTime() const;
    unsigned long getUpWaitTime() const;

    bool setDownLuxThreshold(float newThreshold);
    bool setUpLuxThreshold(float newThreshold);

    void setDownWaitTime(unsigned long newWaitTime);
    void setUpWaitTime(unsigned long newWaitTime);

private:
    enum AutomaticBlindControllerState {
        IDLE,
        WAITING_UP,
        WAITING_DOWN
    };

    bool load();
    bool save() const;

    bool controllerEnabled = true;
    AutomaticBlindControllerState currentState = IDLE;
    SomfyShade* shade = nullptr;

    unsigned long upWaitStartTime = 0UL;
    unsigned long downWaitStartTime = 0UL;

    float blindDownLuxThreshold = 25000.0f;
    float blindUpLuxThreshold = 3000.0f;

    unsigned long blindDownWaitTime = /*5UL */ 30UL * 1000UL;
    unsigned long blindUpWaitTime = /*10UL */ 30UL * 1000UL;

    static constexpr float LUX_THRESHOLD_MAX_LIMIT = 65535.0f;
    static constexpr float LUX_THRESHOLD_MIN_LIMIT = 0.0f;
};
