#pragma once

#include <Arduino.h>
#include "ConfigSettings.h"

class Network {
public:
    bool softAPOpened = false;
    String ssid;
    int strength = 0;

    bool setup();
    void loop();
    void end();
    bool connected() const;
    bool connecting() const;
    bool openSoftAP();
    bool connectWiFi();
    void stopWiFiAttempt();

private:
    bool connectionInProgress = false;
    bool mdnsStarted = false;
    uint32_t connectStartedAt = 0;
    uint32_t lastReconnectAttempt = 0;
    uint32_t lastSoftAPAttempt = 0;

    void ensureSoftAP();
    void startMDNS();
};
