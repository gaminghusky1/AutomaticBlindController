#include "AutomaticBlindController.h"

#include <Preferences.h>

extern Preferences pref;

void AutomaticBlindController::begin(SomfyShade* somfyShade) {
    shade = somfyShade;
    load();
}

void AutomaticBlindController::loop(const float lux) {
    if (!controllerEnabled || !shade || !shade->paired || !shade->isIdle()) {
        return;
    }

    const int shadePos = shade->transformPosition(shade->currentPos);
    const unsigned long now = millis();

    switch (currentState) {
        case IDLE:
            if (lux <= blindUpLuxThreshold && shadePos > 0) {
                upWaitStartTime = now;
                currentState = WAITING_UP;
            } else if (lux >= blindDownLuxThreshold && shadePos < 100) {
                downWaitStartTime = now;
                currentState = WAITING_DOWN;
            }
            break;

        case WAITING_UP:
            if (lux > blindUpLuxThreshold || shadePos == 0) {
                currentState = IDLE;
                break;
            }
            Serial.printf("Waiting Up; will move in %d seconds. \n", (int) (blindUpWaitTime - (now - upWaitStartTime)) / 1000);
            if (now - upWaitStartTime >= blindUpWaitTime) {
                shade->sendCommand(somfy_commands::Up);
                currentState = IDLE;
            }
            break;

        case WAITING_DOWN:
            if (lux < blindDownLuxThreshold || shadePos == 100) {
                currentState = IDLE;
                break;
            }
            Serial.printf("Waiting Down; will move in %d seconds. \n", (int) (blindDownWaitTime - (now - downWaitStartTime)) / 1000);
            if (now - downWaitStartTime >= blindDownWaitTime) {
                shade->sendCommand(somfy_commands::Down);
                currentState = IDLE;
            }
            break;
    }
}

bool AutomaticBlindController::isEnabled() const {
    return controllerEnabled;
}

void AutomaticBlindController::setEnabled(const bool enabled) {
    controllerEnabled = enabled;
    if (!controllerEnabled) {
        currentState = IDLE;
    }
    save();
}

const char* AutomaticBlindController::getStateName() const {
    switch (currentState) {
        case WAITING_UP: return "waiting_up";
        case WAITING_DOWN: return "waiting_down";
        case IDLE:
        default:
            return "idle";
    }
}

int8_t AutomaticBlindController::getStatusCode() const {
    if (!controllerEnabled || !shade || !shade->paired || !shade->isIdle()) return -1;
    switch (currentState) {
        case WAITING_UP: return 1;
        case WAITING_DOWN: return 2;
        case IDLE:
        default:
            return 0;
    }
}

unsigned long AutomaticBlindController::getRemainingWaitTime() const {
    const unsigned long now = millis();
    if (currentState == WAITING_UP) {
        const unsigned long elapsed = now - upWaitStartTime;
        return elapsed >= blindUpWaitTime ? 0UL : blindUpWaitTime - elapsed;
    }
    if (currentState == WAITING_DOWN) {
        const unsigned long elapsed = now - downWaitStartTime;
        return elapsed >= blindDownWaitTime ? 0UL : blindDownWaitTime - elapsed;
    }
    return 0UL;
}

float AutomaticBlindController::getDownLuxThreshold() const {
    return blindDownLuxThreshold;
}

float AutomaticBlindController::getUpLuxThreshold() const {
    return blindUpLuxThreshold;
}

unsigned long AutomaticBlindController::getDownWaitTime() const {
    return blindDownWaitTime;
}

unsigned long AutomaticBlindController::getUpWaitTime() const {
    return blindUpWaitTime;
}

bool AutomaticBlindController::setDownLuxThreshold(const float newThreshold) {
    if (newThreshold > LUX_THRESHOLD_MAX_LIMIT || newThreshold < LUX_THRESHOLD_MIN_LIMIT || newThreshold <= blindUpLuxThreshold) {
        return false;
    }
    blindDownLuxThreshold = newThreshold;
    save();
    return true;
}

bool AutomaticBlindController::setUpLuxThreshold(const float newThreshold) {
    if (newThreshold > LUX_THRESHOLD_MAX_LIMIT || newThreshold < LUX_THRESHOLD_MIN_LIMIT || newThreshold >= blindDownLuxThreshold) {
        return false;
    }
    blindUpLuxThreshold = newThreshold;
    save();
    return true;
}

void AutomaticBlindController::setDownWaitTime(const unsigned long newWaitTime) {
    blindDownWaitTime = newWaitTime;
    save();
}

void AutomaticBlindController::setUpWaitTime(const unsigned long newWaitTime) {
    blindUpWaitTime = newWaitTime;
    save();
}

bool AutomaticBlindController::load() {
    pref.begin("Automation", true);
    controllerEnabled = pref.getBool("enabled", controllerEnabled);
    blindDownLuxThreshold = pref.getFloat("downLux", blindDownLuxThreshold);
    blindUpLuxThreshold = pref.getFloat("upLux", blindUpLuxThreshold);
    blindDownWaitTime = pref.getULong("downWait", blindDownWaitTime);
    blindUpWaitTime = pref.getULong("upWait", blindUpWaitTime);
    pref.end();
    return true;
}

bool AutomaticBlindController::save() const {
    pref.begin("Automation");
    pref.putBool("enabled", controllerEnabled);
    pref.putFloat("downLux", blindDownLuxThreshold);
    pref.putFloat("upLux", blindUpLuxThreshold);
    pref.putULong("downWait", blindDownWaitTime);
    pref.putULong("upWait", blindUpWaitTime);
    pref.end();
    return true;
}
