#include "Somfy.h"

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <Preferences.h>
#include <SPI.h>
#include <algorithm>
#include <soc/gpio_reg.h>

extern Preferences pref;

namespace {
constexpr uint16_t SYMBOL_US = 640;
constexpr uint8_t FRAME_BITS = 56;
constexpr uint8_t FRAME_BYTES = 7;

enum class rx_status_t : uint8_t {
    waiting_sync,
    receiving_data
};

constexpr float TOLERANCE_MIN = 0.7f;
constexpr float TOLERANCE_MAX = 1.3f;
constexpr uint32_t TEMPO_WAKEUP_MIN = 9415 * TOLERANCE_MIN;
constexpr uint32_t TEMPO_WAKEUP_MAX = 9415 * TOLERANCE_MAX;
constexpr uint32_t TEMPO_HW_SYNC_MIN = SYMBOL_US * 4 * TOLERANCE_MIN;
constexpr uint32_t TEMPO_HW_SYNC_MAX = SYMBOL_US * 4 * TOLERANCE_MAX;
constexpr uint32_t TEMPO_SW_SYNC_MIN = 4850 * TOLERANCE_MIN;
constexpr uint32_t TEMPO_SW_SYNC_MAX = 4850 * TOLERANCE_MAX;
constexpr uint32_t TEMPO_HALF_SYMBOL_MIN = SYMBOL_US * TOLERANCE_MIN;
constexpr uint32_t TEMPO_HALF_SYMBOL_MAX = SYMBOL_US * TOLERANCE_MAX;
constexpr uint32_t TEMPO_SYMBOL_MIN = SYMBOL_US * 2 * TOLERANCE_MIN;
constexpr uint32_t TEMPO_SYMBOL_MAX = SYMBOL_US * 2 * TOLERANCE_MAX;
constexpr uint32_t BIT_MIN_US = SYMBOL_US * TOLERANCE_MIN;

volatile rx_status_t rxStatus = rx_status_t::waiting_sync;
volatile uint8_t rxHardwareSyncCount = 0;
volatile uint8_t rxBitCount = 0;
volatile uint8_t rxPreviousBit = 0;
volatile bool rxWaitingHalfSymbol = false;
volatile uint8_t rxBitLength = FRAME_BITS;
volatile uint8_t rxPayload[10] = {};
volatile bool rxFrameReady = false;
volatile uint32_t rxLastEdgeMicros = 0;
uint8_t rxPin = 255;
bool rxEnabled = false;

void copyString(char* destination, size_t size, const char* value) {
    if (!value) value = "";
    strlcpy(destination, value, size);
}

float clampPosition(float position) {
    if (position < 0.0f) return 0.0f;
    if (position > 100.0f) return 100.0f;
    return position;
}

uint32_t scaledTravelTime(const uint32_t fullTravelTime, const float startPosition, const float endPosition) {
    const float distance = fabsf(clampPosition(endPosition) - clampPosition(startPosition));
    if (fullTravelTime == 0 || distance <= 0.01f) return 0;

    const float scaledTime = static_cast<float>(fullTravelTime) * (distance / 100.0f);
    return static_cast<uint32_t>(std::max(1.0f, roundf(scaledTime)));
}

uint32_t defaultRemoteAddress() {
    uint32_t address = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFF);
    if (address == 0) address = 0xA5A5A5;
    return address;
}

bool loadLegacyShade(SomfyShade& shade, uint32_t& remoteAddress, uint16_t& rollingCode) {
    pref.begin("Shade1", true);
    remoteAddress = pref.getUInt("remoteAddress", 0);
    if (remoteAddress == 0) {
        pref.end();
        return false;
    }

    pref.getString("name", shade.name, sizeof(shade.name));
    shade.paired = pref.getBool("paired", shade.paired);
    shade.currentPos = pref.getFloat("currentPos", shade.currentPos);
    shade.target = pref.getFloat("target", shade.currentPos);
    shade.upTime = pref.getUInt("upTime", shade.upTime);
    shade.downTime = pref.getUInt("downTime", shade.downTime);
    shade.repeats = pref.getUChar("repeats", shade.repeats);
    pref.end();

    char rollingCodeKey[16];
    snprintf(rollingCodeKey, sizeof(rollingCodeKey), "_%lu", static_cast<unsigned long>(remoteAddress));
    pref.begin("ShadeCodes", true);
    rollingCode = pref.getUShort(rollingCodeKey, rollingCode);
    pref.end();
    return true;
}

bool inRange(uint32_t value, uint32_t minimum, uint32_t maximum) {
    return value >= minimum && value <= maximum;
}

bool decodeObfuscatedFrame(const uint8_t* encoded, somfy_frame_t& frame) {
    uint8_t decoded[FRAME_BYTES];
    memcpy(decoded, encoded, FRAME_BYTES);

    for (int8_t index = FRAME_BYTES - 1; index >= 1; index--) {
        decoded[index] ^= decoded[index - 1];
    }

    const uint8_t storedChecksum = decoded[1] & 0x0F;
    decoded[1] &= 0xF0;

    uint8_t checksum = 0;
    for (uint8_t index = 0; index < FRAME_BYTES; index++) {
        checksum ^= decoded[index] ^ (decoded[index] >> 4);
    }
    checksum &= 0x0F;

    if (checksum != storedChecksum) return false;

    const uint8_t command = (decoded[1] >> 4) & 0x0F;
    if (command != static_cast<uint8_t>(somfy_commands::My) &&
        command != static_cast<uint8_t>(somfy_commands::Up) &&
        command != static_cast<uint8_t>(somfy_commands::Down) &&
        command != static_cast<uint8_t>(somfy_commands::Prog)) {
        return false;
    }

    frame.command = static_cast<somfy_commands>(command);
    frame.rollingCode = (static_cast<uint16_t>(decoded[2]) << 8) | decoded[3];
    frame.remoteAddress = (static_cast<uint32_t>(decoded[4]) << 16) |
                          (static_cast<uint32_t>(decoded[5]) << 8) |
                          decoded[6];
    frame.encryptionKey = decoded[0];
    frame.valid = frame.remoteAddress != 0;
    return frame.valid;
}

void resetRxCapture() {
    memset((void*)rxPayload, 0, sizeof(rxPayload));
    rxHardwareSyncCount = 0;
    rxBitCount = 0;
    rxPreviousBit = 0;
    rxWaitingHalfSymbol = false;
    rxBitLength = FRAME_BITS;
    rxStatus = rx_status_t::waiting_sync;
}
}

String translateSomfyCommand(const somfy_commands command) {
    switch (command) {
        case somfy_commands::Up: return "up";
        case somfy_commands::Down: return "down";
        case somfy_commands::Prog: return "prog";
        case somfy_commands::My:
        default:
            return "my";
    }
}

somfy_commands translateSomfyCommand(const String& command) {
    if (command.equalsIgnoreCase("up") || command.equalsIgnoreCase("open")) return somfy_commands::Up;
    if (command.equalsIgnoreCase("down") || command.equalsIgnoreCase("close")) return somfy_commands::Down;
    if (command.equalsIgnoreCase("prog") || command.equalsIgnoreCase("pair")) return somfy_commands::Prog;
    return somfy_commands::My;
}

void somfy_frame_t::encode(uint8_t* frame) const {
    frame[0] = encryptionKey;
    frame[1] = (static_cast<uint8_t>(command) & 0x0F) << 4;
    frame[2] = rollingCode >> 8;
    frame[3] = rollingCode & 0xFF;
    frame[4] = remoteAddress >> 16;
    frame[5] = remoteAddress >> 8;
    frame[6] = remoteAddress & 0xFF;

    uint8_t checksum = 0;
    for (uint8_t index = 0; index < 7; index++) {
        checksum ^= frame[index] ^ (frame[index] >> 4);
    }
    frame[1] |= checksum & 0x0F;

    for (uint8_t index = 1; index < 7; index++) {
        frame[index] ^= frame[index - 1];
    }
}

void IRAM_ATTR Transceiver::handleReceive() {
    const uint32_t now = micros();
    const uint32_t duration = now - rxLastEdgeMicros;
    rxLastEdgeMicros = now;

    if (rxPin == 255 || rxFrameReady) return;
    if (duration < BIT_MIN_US) return;

    switch (rxStatus) {
        case rx_status_t::waiting_sync:
            if (inRange(duration, TEMPO_HW_SYNC_MIN, TEMPO_HW_SYNC_MAX)) {
                rxHardwareSyncCount++;
            } else if (inRange(duration, TEMPO_SW_SYNC_MIN, TEMPO_SW_SYNC_MAX) && rxHardwareSyncCount >= 4) {
                memset((void*)rxPayload, 0, sizeof(rxPayload));
                rxPreviousBit = 0;
                rxWaitingHalfSymbol = false;
                rxBitCount = 0;
                rxBitLength = rxHardwareSyncCount <= 7 || rxHardwareSyncCount == 14 ? 56 : 80;
                rxStatus = rx_status_t::receiving_data;
            } else {
                rxHardwareSyncCount = 0;
                if (inRange(duration, TEMPO_WAKEUP_MIN, TEMPO_WAKEUP_MAX)) {
                    memset((void*)rxPayload, 0, sizeof(rxPayload));
                    rxPreviousBit = 0;
                    rxWaitingHalfSymbol = false;
                    rxBitCount = 0;
                    rxBitLength = FRAME_BITS;
                }
            }
            break;

        case rx_status_t::receiving_data:
            if (inRange(duration, TEMPO_SYMBOL_MIN, TEMPO_SYMBOL_MAX) && !rxWaitingHalfSymbol) {
                rxPreviousBit = 1 - rxPreviousBit;
                if (rxBitCount < sizeof(rxPayload) * 8) {
                    rxPayload[rxBitCount / 8] |= rxPreviousBit << (7 - (rxBitCount % 8));
                }
                rxBitCount++;
            } else if (inRange(duration, TEMPO_HALF_SYMBOL_MIN, TEMPO_HALF_SYMBOL_MAX)) {
                if (rxWaitingHalfSymbol) {
                    rxWaitingHalfSymbol = false;
                    if (rxBitCount < sizeof(rxPayload) * 8) {
                        rxPayload[rxBitCount / 8] |= rxPreviousBit << (7 - (rxBitCount % 8));
                    }
                    rxBitCount++;
                } else {
                    rxWaitingHalfSymbol = true;
                }
            } else {
                resetRxCapture();
            }
            break;
    }

    if (rxStatus == rx_status_t::receiving_data && rxBitCount >= rxBitLength) {
        rxFrameReady = true;
        rxStatus = rx_status_t::waiting_sync;
    }
}

bool transceiver_config_t::apply() {
    radioInitialized = false;

    Serial.printf("CC1101 GDO0/TX:%u GDO2/RX:%u SCK:%u MISO:%u MOSI:%u CSN:%u\n",
                  TXPin, RXPin, SCKPin, MISOPin, MOSIPin, CSNPin);

    if (TXPin == RXPin) {
        ELECHOUSE_cc1101.setGDO0(TXPin);
    } else {
        ELECHOUSE_cc1101.setGDO(TXPin, RXPin);
    }
    ELECHOUSE_cc1101.setSpiPin(SCKPin, MISOPin, MOSIPin, CSNPin);
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setCCMode(0);
    ELECHOUSE_cc1101.setMHZ(frequency);
    ELECHOUSE_cc1101.setRxBW(rxBandwidth);
    ELECHOUSE_cc1101.setDeviation(deviation);
    ELECHOUSE_cc1101.setPA(txPower);
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setManchester(1);
    ELECHOUSE_cc1101.setPktFormat(3);
    ELECHOUSE_cc1101.setDcFilterOff(0);
    ELECHOUSE_cc1101.setCrc(0);
    ELECHOUSE_cc1101.setCRC_AF(0);
    ELECHOUSE_cc1101.setSyncMode(4);
    ELECHOUSE_cc1101.setAdrChk(0);

    radioInitialized = ELECHOUSE_cc1101.getCC1101();
    pinMode(CSNPin, OUTPUT);
    digitalWrite(CSNPin, HIGH);
    Serial.println(radioInitialized ? "CC1101 initialized" : "CC1101 initialization failed");
    return radioInitialized;
}

bool Transceiver::begin() {
    config.apply();
    enableReceive();
    return true;
}

void Transceiver::loop() {
}

bool Transceiver::receive(somfy_frame_t& frame) {
    if (!rxFrameReady) return false;

    uint8_t payload[10] = {};

    noInterrupts();
    memcpy(payload, (const void*)rxPayload, sizeof(payload));
    rxFrameReady = false;
    resetRxCapture();
    interrupts();

    if (decodeObfuscatedFrame(payload, frame)) {
        Serial.printf("Received RTS command:%s address:%06lX rollingCode:%u\n",
                      translateSomfyCommand(frame.command).c_str(),
                      static_cast<unsigned long>(frame.remoteAddress),
                      frame.rollingCode);
        return true;
    }
    return false;
}

void Transceiver::enableReceive() {
    if (!config.enabled || !config.radioInitialized || config.RXPin == config.TXPin) return;
    if (rxEnabled) return;
    rxPin = config.RXPin;
    pinMode(config.RXPin, INPUT);
    rxFrameReady = false;
    resetRxCapture();
    rxLastEdgeMicros = micros();
    ELECHOUSE_cc1101.SetRx();
    digitalWrite(config.CSNPin, HIGH);
    attachInterrupt(digitalPinToInterrupt(config.RXPin), Transceiver::handleReceive, CHANGE);
    rxEnabled = true;
}

void Transceiver::disableReceive() {
    if (!rxEnabled) return;
    detachInterrupt(digitalPinToInterrupt(config.RXPin));
    rxEnabled = false;
}

void Transceiver::beginTransmit() {
    if (!config.enabled || !config.radioInitialized) return;
    disableReceive();
    pinMode(config.TXPin, OUTPUT);
    digitalWrite(config.TXPin, LOW);
    ELECHOUSE_cc1101.SetTx();
    digitalWrite(config.CSNPin, HIGH);
}

void Transceiver::endTransmit() {
    if (!config.enabled || !config.radioInitialized) return;
    ELECHOUSE_cc1101.setSidle();
    digitalWrite(config.TXPin, LOW);
    digitalWrite(config.CSNPin, HIGH);
    enableReceive();
}

void Transceiver::sendFrame(const uint8_t* frame, const uint8_t hardwareSyncPulses) {
    if (!config.enabled || !config.radioInitialized || config.TXPin > 31) return;

    const uint32_t pinMask = 1UL << config.TXPin;

    if (hardwareSyncPulses == 2) {
        REG_WRITE(GPIO_OUT_W1TS_REG, pinMask);
        delayMicroseconds(10920);
        REG_WRITE(GPIO_OUT_W1TC_REG, pinMask);
        delayMicroseconds(7357);
    }

    for (uint8_t index = 0; index < hardwareSyncPulses; index++) {
        REG_WRITE(GPIO_OUT_W1TS_REG, pinMask);
        delayMicroseconds(4 * SYMBOL_US);
        REG_WRITE(GPIO_OUT_W1TC_REG, pinMask);
        delayMicroseconds(4 * SYMBOL_US);
    }

    REG_WRITE(GPIO_OUT_W1TS_REG, pinMask);
    delayMicroseconds(4850);
    REG_WRITE(GPIO_OUT_W1TC_REG, pinMask);
    delayMicroseconds(SYMBOL_US);

    uint8_t lastBit = 0;
    for (uint8_t bit = 0; bit < FRAME_BITS; bit++) {
        const bool isOne = (frame[bit / 8] >> (7 - (bit % 8))) & 1;
        if (isOne) {
            REG_WRITE(GPIO_OUT_W1TC_REG, pinMask);
            delayMicroseconds(SYMBOL_US);
            REG_WRITE(GPIO_OUT_W1TS_REG, pinMask);
            delayMicroseconds(SYMBOL_US);
            lastBit = 1;
        } else {
            REG_WRITE(GPIO_OUT_W1TS_REG, pinMask);
            delayMicroseconds(SYMBOL_US);
            REG_WRITE(GPIO_OUT_W1TC_REG, pinMask);
            delayMicroseconds(SYMBOL_US);
            lastBit = 0;
        }
    }

    if (lastBit == 0) {
        REG_WRITE(GPIO_OUT_W1TS_REG, pinMask);
    }
    REG_WRITE(GPIO_OUT_W1TC_REG, pinMask);
    delayMicroseconds(13717);
    delayMicroseconds(13717);
}

bool SomfyShade::begin() {
    pref.begin("Shade", true);
    pref.getString("name", name, sizeof(name));
    paired = pref.getBool("paired", paired);
    remoteAddress = pref.getUInt("remoteAddr", 0);
    rollingCode = pref.getUShort("rollingCode", rollingCode);
    linkedRemoteAddress = pref.getUInt("linkedRemote", linkedRemoteAddress);
    lastExternalRollingCode = pref.getUShort("lastExtCode", lastExternalRollingCode);
    currentPos = pref.getFloat("currentPos", currentPos);
    target = pref.getFloat("target", currentPos);
    upTime = pref.getUInt("upTime", upTime);
    downTime = pref.getUInt("downTime", downTime);
    repeats = pref.getUChar("repeats", repeats);
    pref.end();

    if (remoteAddress == 0 && loadLegacyShade(*this, remoteAddress, rollingCode)) {
        Serial.println("Migrated legacy single-shade settings");
        save();
    }
    if (name[0] == '\0') copyString(name, sizeof(name), "Shade");
    if (remoteAddress == 0) {
        remoteAddress = defaultRemoteAddress();
        save();
    }
    if (repeats == 0) repeats = 1;
    currentPos = clampPosition(currentPos);
    target = clampPosition(target);
    direction = 0;

    Serial.printf("Shade address:%06lX rollingCode:%u paired:%s position:%.0f\n",
                  static_cast<unsigned long>(remoteAddress), rollingCode, paired ? "yes" : "no", currentPos);
    return true;
}

bool SomfyShade::save() const {
    pref.begin("Shade");
    pref.putString("name", name);
    pref.putBool("paired", paired);
    pref.putUInt("remoteAddr", remoteAddress & 0xFFFFFF);
    pref.putUShort("rollingCode", rollingCode);
    pref.putUInt("linkedRemote", linkedRemoteAddress & 0xFFFFFF);
    pref.putUShort("lastExtCode", lastExternalRollingCode);
    pref.putFloat("currentPos", currentPos);
    pref.putFloat("target", target);
    pref.putUInt("upTime", upTime);
    pref.putUInt("downTime", downTime);
    pref.putUChar("repeats", repeats);
    pref.end();
    return true;
}

void SomfyShade::loop() {
    if (direction == 0) return;

    const uint32_t elapsed = millis() - movementStartMillis;
    const uint32_t travelTime = movementDurationMillis;
    if (travelTime == 0) {
        currentPos = target;
        direction = 0;
        movementDurationMillis = 0;
        save();
        return;
    }

    const float progress = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(travelTime));
    currentPos = movementStartPosition + ((target - movementStartPosition) * progress);

    if (progress >= 1.0f) {
        currentPos = target;
        direction = 0;
        movementDurationMillis = 0;
        save();
    }
}

void SomfyShade::sendCommand(const somfy_commands command) {
    sendCommand(command, repeats);
}

void SomfyShade::sendCommand(const somfy_commands command, const uint8_t repeat, uint8_t) {
    if (remoteAddress == 0) setRemoteAddress(defaultRemoteAddress());

    somfy_frame_t frame;
    frame.command = command;
    frame.remoteAddress = remoteAddress;
    frame.rollingCode = nextRollingCode();
    frame.encryptionKey = 0xA0 | static_cast<uint8_t>(frame.rollingCode & 0x000F);

    Serial.printf("Somfy command:%s address:%06lX rollingCode:%u repeat:%u\n",
                  translateSomfyCommand(command).c_str(),
                  static_cast<unsigned long>(remoteAddress),
                  frame.rollingCode,
                  repeat);

    somfy.sendFrame(frame, repeat);

    switch (command) {
        case somfy_commands::Up:
            startMovement(0.0f, -1);
            break;
        case somfy_commands::Down:
            startMovement(100.0f, 1);
            break;
        case somfy_commands::My:
            if (!isIdle()) {
                target = currentPos;
                direction = 0;
                save();
            }
            break;
        case somfy_commands::Prog:
            break;
    }
}

void SomfyShade::moveToTarget(const float position) {
    const float clamped = clampPosition(position);
    if (clamped <= 0.5f) {
        sendCommand(somfy_commands::Up);
    } else if (clamped >= 99.5f) {
        sendCommand(somfy_commands::Down);
    } else {
        target = clamped;
        save();
        Serial.println("Intermediate targets are stored but RTS has no direct absolute-position command.");
    }
}

bool SomfyShade::isIdle() const {
    return direction == 0;
}

int8_t SomfyShade::transformPosition(const float position) const {
    return static_cast<int8_t>(lroundf(clampPosition(position)));
}

uint32_t SomfyShade::getRemoteAddress() const {
    return remoteAddress;
}

void SomfyShade::setRemoteAddress(const uint32_t address) {
    remoteAddress = address & 0xFFFFFF;
    save();
}

uint16_t SomfyShade::getRollingCode() const {
    return rollingCode;
}

uint16_t SomfyShade::setRollingCode(const uint16_t code) {
    rollingCode = code;
    save();
    return rollingCode;
}

void SomfyShade::setPaired(const bool isPaired) {
    paired = isPaired;
    save();
}

void SomfyShade::setCurrentPosition(const float position) {
    currentPos = clampPosition(position);
    target = currentPos;
    direction = 0;
    save();
}

void SomfyShade::setLinkedRemoteAddress(const uint32_t address) {
    linkedRemoteAddress = address & 0xFFFFFF;
    lastExternalRollingCode = 0;
    save();
}

void SomfyShade::clearLinkedRemoteAddress() {
    linkedRemoteAddress = 0;
    lastExternalRollingCode = 0;
    save();
}

void SomfyShade::processExternalFrame(const somfy_frame_t& frame) {
    if (!frame.valid || linkedRemoteAddress == 0 || frame.remoteAddress != linkedRemoteAddress) return;
    if (frame.rollingCode == lastExternalRollingCode) return;

    lastExternalRollingCode = frame.rollingCode;
    Serial.printf("Tracking linked remote command:%s rollingCode:%u\n",
                  translateSomfyCommand(frame.command).c_str(),
                  frame.rollingCode);

    switch (frame.command) {
        case somfy_commands::Up:
            startMovement(0.0f, -1);
            break;
        case somfy_commands::Down:
            startMovement(100.0f, 1);
            break;
        case somfy_commands::My:
            if (!isIdle()) {
                target = currentPos;
                direction = 0;
                save();
            }
            break;
        case somfy_commands::Prog:
            break;
    }
    save();
}

void SomfyShade::appendJson(JsonObject object) const {
    object["name"] = name;
    object["paired"] = paired;
    object["remoteAddress"] = remoteAddress;
    object["remoteAddressHex"] = String(remoteAddress, HEX);
    object["linkedRemoteAddress"] = linkedRemoteAddress;
    object["linkedRemoteAddressHex"] = String(linkedRemoteAddress, HEX);
    object["lastExternalRollingCode"] = lastExternalRollingCode;
    object["rollingCode"] = rollingCode;
    object["position"] = transformPosition(currentPos);
    object["target"] = transformPosition(target);
    object["moving"] = !isIdle();
    object["upTime"] = upTime;
    object["downTime"] = downTime;
    object["repeats"] = repeats;
}

uint16_t SomfyShade::nextRollingCode() {
    rollingCode++;
    if (rollingCode == 0) rollingCode = 1;
    save();
    return rollingCode;
}

void SomfyShade::startMovement(const float newTarget, const int8_t newDirection) {
    target = clampPosition(newTarget);
    movementStartPosition = clampPosition(currentPos);
    movementStartMillis = millis();
    movementDurationMillis = scaledTravelTime(newDirection < 0 ? upTime : downTime, movementStartPosition, target);

    if (movementDurationMillis == 0) {
        currentPos = target;
        direction = 0;
        save();
        return;
    }

    direction = newDirection;
    save();
}

bool SomfyShadeController::begin() {
    transceiver.begin();
    shade.begin();
    return true;
}

void SomfyShadeController::loop() {
    somfy_frame_t frame;
    if (transceiver.receive(frame)) {
        if (learningRemote && frame.remoteAddress != shade.getRemoteAddress()) {
            shade.setLinkedRemoteAddress(frame.remoteAddress);
            learningRemote = false;
            Serial.printf("Linked remote learned:%06lX\n", static_cast<unsigned long>(frame.remoteAddress));
        }
        shade.processExternalFrame(frame);
    }
    shade.loop();
    transceiver.loop();
}

void SomfyShadeController::end() {
    transceiver.endTransmit();
}

SomfyShade* SomfyShadeController::getShade() {
    return &shade;
}

void SomfyShadeController::sendFrame(const somfy_frame_t& frame, const uint8_t repeat) {
    uint8_t encodedFrame[7] = {};
    frame.encode(encodedFrame);

    transceiver.beginTransmit();
    transceiver.sendFrame(encodedFrame, 2);
    for (uint8_t index = 0; index < repeat; index++) {
        transceiver.sendFrame(encodedFrame, 7);
        yield();
    }
    transceiver.endTransmit();
}

void SomfyShadeController::beginRemoteLearn() {
    learningRemote = true;
    Serial.println("Remote learn mode active. Press a button on the existing remote.");
}

void SomfyShadeController::clearLinkedRemote() {
    learningRemote = false;
    shade.clearLinkedRemoteAddress();
}

bool SomfyShadeController::isLearningRemote() const {
    return learningRemote;
}
