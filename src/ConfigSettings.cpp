#include "ConfigSettings.h"

#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

Preferences pref;

namespace {
void copyString(char* destination, size_t size, const char* value) {
    if (!value) value = "";
    strlcpy(destination, value, size);
}
}

bool WifiSettings::configured() const {
    return ssid[0] != '\0';
}

void WifiSettings::set(const char* newSsid, const char* newPassphrase) {
    copyString(ssid, sizeof(ssid), newSsid);
    copyString(passphrase, sizeof(passphrase), newPassphrase);
}

bool WifiSettings::load() {
    pref.begin("WiFi", true);
    pref.getString("ssid", ssid, sizeof(ssid));
    pref.getString("pass", passphrase, sizeof(passphrase));
    pref.end();
    if (ssid[0] == '\0') {
        pref.begin("WIFI", true);
        pref.getString("ssid", ssid, sizeof(ssid));
        pref.getString("passphrase", passphrase, sizeof(passphrase));
        pref.end();
        if (ssid[0] != '\0') save();
    }
    return true;
}

bool WifiSettings::save() const {
    pref.begin("WiFi");
    pref.putString("ssid", ssid);
    pref.putString("pass", passphrase);
    pref.end();
    return true;
}

bool ConfigSettings::begin() {
    const uint64_t chipId = ESP.getEfuseMac();
    snprintf(serverId, sizeof(serverId), "%06X", static_cast<uint32_t>(chipId & 0xFFFFFF));
    load();
    if (hostname[0] == '\0') {
        snprintf(hostname, sizeof(hostname), "AutoBlinds-%s", serverId);
        save();
    }
    applyTime();
    return true;
}

bool ConfigSettings::load() {
    pref.begin("Settings", true);
    pref.getString("hostname", hostname, sizeof(hostname));
    pref.getString("ntpServer", ntpServer, sizeof(ntpServer));
    pref.getString("posixZone", posixZone, sizeof(posixZone));
    pref.end();
    WIFI.load();
    return true;
}

bool ConfigSettings::save() const {
    pref.begin("Settings");
    pref.putString("hostname", hostname);
    pref.putString("ntpServer", ntpServer);
    pref.putString("posixZone", posixZone);
    pref.end();
    WIFI.save();
    return true;
}

bool ConfigSettings::applyTime() const {
    if (ntpServer[0] == '\0') return false;
    if (posixZone[0] != '\0') {
        configTzTime(posixZone, ntpServer);
    } else {
        configTime(0, 0, ntpServer);
    }
    return true;
}

void ConfigSettings::setHostname(const char* newHostname) {
    copyString(hostname, sizeof(hostname), newHostname);
}

void ConfigSettings::setNtp(const char* newServer, const char* newZone) {
    copyString(ntpServer, sizeof(ntpServer), newServer);
    copyString(posixZone, sizeof(posixZone), newZone);
}
