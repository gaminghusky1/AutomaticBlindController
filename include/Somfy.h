#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class radio_proto : uint8_t {
    RTS = 0
};

enum class somfy_commands : uint8_t {
    My = 0x1,
    Up = 0x2,
    Down = 0x4,
    Prog = 0x8
};

String translateSomfyCommand(somfy_commands command);
somfy_commands translateSomfyCommand(const String& command);

struct somfy_frame_t {
    somfy_commands command = somfy_commands::My;
    uint32_t remoteAddress = 0;
    uint16_t rollingCode = 0;
    uint8_t encryptionKey = 0xA7;
    bool valid = false;

    void encode(uint8_t* frame) const;
};

struct transceiver_config_t {
    const bool enabled = true;
    const uint8_t TXPin = 13;
    const uint8_t RXPin = 14;
    const uint8_t SCKPin = 18;
    const uint8_t MOSIPin = 23;
    const uint8_t MISOPin = 19;
    const uint8_t CSNPin = 5;
    const float frequency = 433.42f;
    const float deviation = 11.43f; // 47.60f;
    const float rxBandwidth = 96.96f; // 99.97f;
    const int8_t txPower = 10;
    bool radioInitialized = false;

    bool apply();
};

class Transceiver {
public:
    transceiver_config_t config;

    bool begin();
    void loop();
    bool receive(somfy_frame_t& frame);
    void enableReceive();
    void disableReceive();
    void beginTransmit();
    void endTransmit();
    void sendFrame(const uint8_t* frame, uint8_t hardwareSyncPulses);

private:
    static void IRAM_ATTR handleReceive();
};

class SomfyShade {
public:
    char name[21] = "Shade";
    bool paired = false;
    float currentPos = 0.0f;
    float target = 0.0f;
    uint32_t upTime = 10000;
    uint32_t downTime = 10000;
    uint8_t repeats = 1;
    uint32_t linkedRemoteAddress = 0;

    bool begin();
    void loop();
    bool save() const;
    void sendCommand(somfy_commands command);
    void sendCommand(somfy_commands command, uint8_t repeat, uint8_t stepSize = 0);
    void moveToTarget(float position);
    bool isIdle() const;
    int8_t transformPosition(float position) const;
    uint32_t getRemoteAddress() const;
    void setRemoteAddress(uint32_t address);
    uint16_t getRollingCode() const;
    uint16_t setRollingCode(uint16_t code);
    void setPaired(bool isPaired);
    void setCurrentPosition(float position);
    void setLinkedRemoteAddress(uint32_t address);
    void clearLinkedRemoteAddress();
    void processExternalFrame(const somfy_frame_t& frame);
    void appendJson(JsonObject object) const;

private:
    uint32_t remoteAddress = 0;
    uint16_t rollingCode = 0;
    uint16_t lastExternalRollingCode = 0;
    int8_t direction = 0;
    float movementStartPosition = 0.0f;
    uint32_t movementStartMillis = 0;
    uint32_t movementDurationMillis = 0;

    uint16_t nextRollingCode();
    void startMovement(float newTarget, int8_t newDirection);
};

class SomfyShadeController {
public:
    Transceiver transceiver;
    SomfyShade shade;

    bool begin();
    void loop();
    void end();
    SomfyShade* getShade();
    void sendFrame(const somfy_frame_t& frame, uint8_t repeat);
    void beginRemoteLearn();
    void clearLinkedRemote();
    bool isLearningRemote() const;

private:
    bool learningRemote = false;
};

extern SomfyShadeController somfy;
