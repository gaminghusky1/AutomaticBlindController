#include "Network.h"

#include <ESPmDNS.h>
#include <WiFi.h>

extern ConfigSettings settings;

namespace {
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t RECONNECT_INTERVAL_MS = 30000;
constexpr uint32_t SOFT_AP_RETRY_INTERVAL_MS = 5000;
}

bool Network::setup() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(settings.hostname);

    if (settings.WIFI.configured()) {
        openSoftAP();
        connectWiFi();
    } else {
        openSoftAP();
    }
    return true;
}

void Network::loop() {
    if (connected()) {
        if (softAPOpened) {
            WiFi.softAPdisconnect(true);
            softAPOpened = false;
        }
        strength = WiFi.RSSI();
        if (!mdnsStarted) startMDNS();
        return;
    }

    ensureSoftAP();

    if (connectionInProgress && millis() - connectStartedAt > CONNECT_TIMEOUT_MS) {
        connectionInProgress = false;
        WiFi.disconnect(false, false);
        Serial.println("WiFi connection timed out; setup AP remains available");
    }

    if (!connectionInProgress && settings.WIFI.configured() && millis() - lastReconnectAttempt > RECONNECT_INTERVAL_MS) {
        connectWiFi();
    }
}

void Network::end() {
    MDNS.end();
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    mdnsStarted = false;
    softAPOpened = false;
}

bool Network::connected() const {
    return WiFi.status() == WL_CONNECTED;
}

bool Network::connecting() const {
    return connectionInProgress;
}

bool Network::openSoftAP() {
    if (softAPOpened) return true;

    const String apName = String(settings.hostname) + "-" + settings.serverId;
    lastSoftAPAttempt = millis();
    WiFi.mode(WIFI_AP_STA);
    softAPOpened = WiFi.softAP(apName.c_str());
    if (softAPOpened) {
        settings.connType = conn_types_t::ap;
        Serial.printf("Setup AP started: %s IP:%s\n", apName.c_str(), WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("Failed to start setup AP");
    }
    return softAPOpened;
}

bool Network::connectWiFi() {
    if (!settings.WIFI.configured()) return false;

    lastReconnectAttempt = millis();
    connectionInProgress = true;
    connectStartedAt = millis();
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(settings.hostname);
    WiFi.begin(settings.WIFI.ssid, settings.WIFI.passphrase);
    ssid = settings.WIFI.ssid;
    Serial.printf("Connecting to WiFi SSID:%s\n", settings.WIFI.ssid);
    return true;
}

void Network::stopWiFiAttempt() {
    if (connectionInProgress || WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(false, false);
        connectionInProgress = false;
    }
    lastReconnectAttempt = millis();
    WiFi.mode(WIFI_AP_STA);
    if (!softAPOpened) openSoftAP();
}

void Network::ensureSoftAP() {
    if (softAPOpened) return;
    if (lastSoftAPAttempt != 0 && millis() - lastSoftAPAttempt < SOFT_AP_RETRY_INTERVAL_MS) return;
    openSoftAP();
}

void Network::startMDNS() {
    if (MDNS.begin(settings.hostname)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
        settings.connType = conn_types_t::wifi;
        Serial.printf("Connected to WiFi: %s (%ddBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        Serial.printf("mDNS started: http://%s.local\n", settings.hostname);
    } else {
        Serial.println("mDNS failed to start");
    }
}
