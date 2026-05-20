#include <WiFi.h>
#include <esp_task_wdt.h>
#include "AutomaticBlindController.h"
#include "BH1750LightMeter.h"
#include "ConfigSettings.h"
#include "Display.h"
#include "Network.h"
#include "Somfy.h"
#include "Web.h"

ConfigSettings settings;
Web webServer;
Network net;
SomfyShadeController somfy;
BH1750LightMeter lightMeter;
AutomaticBlindController automaticBlindController;
DisplayControlPanel displayPanel;

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("Starting Automatic Blinds controller");

    settings.begin();
    somfy.begin();
    net.setup();
    webServer.begin();

    displayPanel.begin(&somfy, somfy.getShade(), &automaticBlindController, &lightMeter, &settings, &net);
    lightMeter.begin();
    automaticBlindController.begin(somfy.getShade());

    esp_task_wdt_init(7, true);
    esp_task_wdt_add(nullptr);
}

void loop() {
    lightMeter.update();
    if (lightMeter.newReadingAvailable()) {
        const float luxReading = lightMeter.getLux();
        Serial.printf("Lux: %.2f\n", luxReading);
        automaticBlindController.loop(luxReading);
    }

    net.loop();
    somfy.loop();
    webServer.loop();
    displayPanel.loop();
    esp_task_wdt_reset();
}
