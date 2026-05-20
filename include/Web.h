#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>

class Web {
public:
    void begin();
    void loop();
    void end();

private:
    void sendCORSHeaders();
    void sendJson(int statusCode, const JsonDocument& document);
    void sendError(int statusCode, const char* message);
    bool parseJsonBody(JsonDocument& document);
    void handleRoot();
    void handleStatus();
    void handleCommand();
    void handlePair();
    void handleConfirmPair();
    void handleLearnRemote();
    void handleClearRemote();
    void handleShadeSettings();
    void handleWiFi();
    void handleWiFiScan();
    void handleSettings();
    void handleAutomation();
    void handleReboot();
    void handleNotFound();
};
