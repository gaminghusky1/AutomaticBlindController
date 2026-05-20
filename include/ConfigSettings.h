#pragma once

#include <Arduino.h>

#define FW_VERSION "single-shade-0.1.0"

enum class conn_types_t : uint8_t {
    unset = 0,
    wifi = 1,
    ap = 2
};

struct WifiSettings {
    char ssid[65] = "";
    char passphrase[65] = "";

    bool load();
    bool save() const;
    bool configured() const;
    void set(const char* newSsid, const char* newPassphrase);
};

class ConfigSettings {
public:
    char serverId[10] = "";
    char hostname[32] = "AutoBlinds";
    char ntpServer[65] = "pool.ntp.org";
    char posixZone[64] = "";
    conn_types_t connType = conn_types_t::unset;
    WifiSettings WIFI;

    bool begin();
    bool load();
    bool save() const;
    bool applyTime() const;
    void setHostname(const char* newHostname);
    void setNtp(const char* newServer, const char* newZone);
};
