#pragma once

#include <Arduino.h>

class AutomaticBlindController;
class BH1750LightMeter;
class ConfigSettings;
class Network;
class SomfyShadeController;
class SomfyShade;

class DisplayControlPanel {
public:
    void begin(SomfyShadeController* somfy,
               SomfyShade* somfyShade,
               AutomaticBlindController* automation,
               BH1750LightMeter* lightMeter,
               ConfigSettings* settings,
               Network* network);
    void loop();
};

