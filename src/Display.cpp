#include "Display.h"

#include <cstring>
#include <cstdlib>
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <lvgl.h>

#include "AutomaticBlindController.h"
#include "BH1750LightMeter.h"
#include "ConfigSettings.h"
#include "Network.h"
#include "Somfy.h"

namespace {
// Display pins.
constexpr int LCD_CS = 27;
constexpr int LCD_RST = 17;
constexpr int LCD_DC = 16;
constexpr int LCD_MOSI = 23;
constexpr int LCD_SCK = 18;
constexpr int LCD_MISO = 19;
constexpr int LCD_LED = 32;

// Capacitive touch pins.
constexpr int CTP_SDA = 21;
constexpr int CTP_SCL = 22;
constexpr int CTP_RST = 25;
constexpr int CTP_INT = 33;

constexpr int DEFAULT_CC1101_CSN = 5;

constexpr uint8_t BACKLIGHT_PWM_CHANNEL = 7;
constexpr uint32_t BACKLIGHT_PWM_FREQ = 5000;
constexpr uint8_t BACKLIGHT_PWM_BITS = 8;
constexpr uint8_t BACKLIGHT_BRIGHTNESS = 220;
constexpr uint8_t BACKLIGHT_DIM_BRIGHTNESS = 45;

constexpr uint32_t STATUS_REFRESH_MS = 1000;
constexpr uint32_t WIFI_SCAN_TIMEOUT_MS = 15000;
constexpr uint32_t BACKLIGHT_DIM_AFTER_MS = 60000;
constexpr uint32_t BACKLIGHT_OFF_AFTER_MS = BACKLIGHT_DIM_AFTER_MS + 10000;

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 320;
constexpr int TOP_BAR_H = 58;
constexpr int DRAW_BUFFER_LINES = 10;

constexpr int POLL_RATE_MIN_DS = 5;
constexpr int POLL_RATE_MAX_DS = 600;
constexpr int WAIT_TIME_MIN_S = 0;
constexpr int WAIT_TIME_MAX_S = 600;
constexpr int LUX_THRESHOLD_MIN = 0;
constexpr int LUX_THRESHOLD_MAX = 65535;
constexpr int LUX_THRESHOLD_MIN_GAP = 50;

constexpr uint32_t COLOR_BG_HEX = 0x000000;
constexpr uint32_t COLOR_TEXT_HEX = 0xFFFFFF;
constexpr uint32_t COLOR_MUTED_HEX = 0xB8C7CC;
constexpr uint32_t COLOR_BUTTON_HEX = 0x009DCC;
constexpr uint32_t COLOR_BUTTON_PRESSED_HEX = 0x007FA8;
constexpr uint32_t COLOR_BORDER_HEX = 0x00E5FF;
constexpr uint32_t COLOR_PANEL_HEX = 0x050B0D;

class LGFX : public lgfx::LGFX_Device {
private:
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Touch_FT5x06 _touch;

public:
    LGFX() {
        {
            auto cfg = _bus.config();

#if defined(VSPI_HOST)
            cfg.spi_host = VSPI_HOST;
#else
            cfg.spi_host = SPI2_HOST;
#endif
            cfg.spi_mode = 0;
            cfg.freq_write = 30000000;
            cfg.freq_read = 6000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = 0;

            cfg.pin_sclk = LCD_SCK;
            cfg.pin_mosi = LCD_MOSI;
            cfg.pin_miso = LCD_MISO;
            cfg.pin_dc = LCD_DC;

            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        {
            auto cfg = _panel.config();

            cfg.pin_cs = LCD_CS;
            cfg.pin_rst = LCD_RST;
            cfg.pin_busy = -1;

            cfg.memory_width = 320;
            cfg.memory_height = 480;
            cfg.panel_width = 320;
            cfg.panel_height = 480;

            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 1;

            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;

            cfg.readable = false;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;

            _panel.config(cfg);
        }

        {
            auto cfg = _touch.config();

            cfg.i2c_port = 0;
            cfg.i2c_addr = 0x38;
            cfg.pin_sda = CTP_SDA;
            cfg.pin_scl = CTP_SCL;
            cfg.pin_int = CTP_INT;
            cfg.pin_rst = CTP_RST;
            cfg.freq = 100000;

            cfg.x_min = 0;
            cfg.x_max = 320;
            cfg.y_min = 0;
            cfg.y_max = 480;
            cfg.offset_rotation = 0;

            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }
};

enum class DisplayPage : uint8_t {
    Home,
    Automation,
    Pairing,
    WiFi
};

enum class DisplayAction : uint8_t {
    ShadeUp,
    ShadeMy,
    ShadeDown,
    ToggleAutomation,
    PageHome,
    PageAutomation,
    PagePairing,
    PageWiFi,
    PairProg,
    PairToggle,
    RemoteLearn,
    RemoteClear,
    WiFiScan,
    WiFiNetwork0,
    WiFiNetwork1,
    WiFiNetwork2,
    WiFiNetwork3,
    WiFiConnect,
    WiFiCancel,
    PowerOff,
    AutoEditPollRate,
    AutoEditUpLux,
    AutoEditDownLux,
    AutoEditDownWait,
    AutoEditUpWait,
    PairEditUpTime,
    PairEditDownTime,
    AutoInputSave,
    AutoInputCancel
};

enum class AutoField : uint8_t {
    None,
    PollRate,
    UpLux,
    DownLux,
    DownWait,
    UpWait,
    ShadeUpTime,
    ShadeDownTime
};

LGFX tft;
SomfyShadeController* somfyController = nullptr;
SomfyShade* somfyShade = nullptr;
AutomaticBlindController* automationController = nullptr;
BH1750LightMeter* panelLightMeter = nullptr;
ConfigSettings* panelSettings = nullptr;
Network* panelNetwork = nullptr;

DisplayPage currentPage = DisplayPage::Home;
bool displayReady = false;
bool lvglReady = false;
uint32_t lastStatusDraw = 0;
uint32_t lastLvTick = 0;
uint32_t lastDisplayActivityAt = 0;
bool lastAutomationEnabled = false;
uint8_t currentBacklightBrightness = BACKLIGHT_BRIGHTNESS;
bool ignoreTouchUntilRelease = false;

lv_disp_draw_buf_t drawBuffer;
lv_color_t drawBufferPixels[SCREEN_W * DRAW_BUFFER_LINES];
lv_disp_drv_t displayDriver;
lv_indev_drv_t touchDriver;

lv_obj_t* luxLabel = nullptr;
lv_obj_t* wifiLabel = nullptr;
lv_obj_t* positionLabel = nullptr;
lv_obj_t* autoCountdownLabel = nullptr;
lv_obj_t* autoCheckbox = nullptr;
lv_obj_t* pairingStatusLabel = nullptr;
lv_obj_t* wifiConnectedLabel = nullptr;
lv_obj_t* wifiStatusLabel = nullptr;
lv_obj_t* passwordArea = nullptr;
lv_obj_t* pollRateLabel = nullptr;
lv_obj_t* thresholdLabel = nullptr;
lv_obj_t* downWaitLabel = nullptr;
lv_obj_t* upWaitLabel = nullptr;
lv_obj_t* pollRateSlider = nullptr;
lv_obj_t* thresholdSlider = nullptr;
lv_obj_t* downWaitSlider = nullptr;
lv_obj_t* upWaitSlider = nullptr;
lv_obj_t* automationStatusLabel = nullptr;
lv_obj_t* autoInputArea = nullptr;

lv_style_t screenStyle;
lv_style_t topBarStyle;
lv_style_t buttonStyle;
lv_style_t buttonPressedStyle;
lv_style_t circleButtonStyle;
lv_style_t labelStyle;
lv_style_t mutedLabelStyle;
lv_style_t checkboxStyle;
lv_style_t checkboxIndicatorStyle;
lv_style_t modalStyle;
bool stylesInitialized = false;

char scannedNetworks[4][33] = {};
bool scannedNetworkSecured[4] = {};
uint8_t scannedNetworkCount = 0;
char selectedSsid[65] = "";
bool wifiScanInProgress = false;
uint32_t wifiScanStartedAt = 0;
char wifiScanStatusText[48] = "Tap Scan to search for networks.";
char automationStatusText[72] = "Tap a value to edit it.";
AutoField editingAutoField = AutoField::None;

uint8_t cc1101CsnPin() {
    return somfyController ? somfyController->transceiver.config.CSNPin : DEFAULT_CC1101_CSN;
}

void releaseSharedSpiDevices() {
    digitalWrite(cc1101CsnPin(), HIGH);
    digitalWrite(LCD_CS, HIGH);
}

void displayFlush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* colorP) {
    releaseSharedSpiDevices();
    tft.startWrite();
    tft.pushImage(area->x1,
                  area->y1,
                  area->x2 - area->x1 + 1,
                  area->y2 - area->y1 + 1,
                  reinterpret_cast<lgfx::swap565_t*>(&colorP->full));
    tft.endWrite();
    releaseSharedSpiDevices();
    lv_disp_flush_ready(driver);
}

void setBacklightBrightness(const uint8_t brightness) {
    currentBacklightBrightness = brightness;
    ledcWrite(BACKLIGHT_PWM_CHANNEL, brightness);
}

void restoreBacklight() {
    setBacklightBrightness(BACKLIGHT_BRIGHTNESS);
    lastDisplayActivityAt = millis();
}

void turnBacklightOff() {
    setBacklightBrightness(0);
    lastDisplayActivityAt = millis();
    ignoreTouchUntilRelease = true;
}

void updateBacklightTimeout() {
    if (currentBacklightBrightness == 0) return;

    const uint32_t idleMs = millis() - lastDisplayActivityAt;
    if (idleMs >= BACKLIGHT_OFF_AFTER_MS) {
        setBacklightBrightness(0);
    } else if (idleMs >= BACKLIGHT_DIM_AFTER_MS && currentBacklightBrightness != BACKLIGHT_DIM_BRIGHTNESS) {
        setBacklightBrightness(BACKLIGHT_DIM_BRIGHTNESS);
    }
}

void touchRead(lv_indev_drv_t*, lv_indev_data_t* data) {
    uint16_t x = 0;
    uint16_t y = 0;

    if (tft.getTouch(&x, &y)) {
        if (ignoreTouchUntilRelease) {
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        if (currentBacklightBrightness < BACKLIGHT_BRIGHTNESS) {
            restoreBacklight();
            ignoreTouchUntilRelease = true;
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        lastDisplayActivityAt = millis();
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    } else {
        ignoreTouchUntilRelease = false;
        data->state = LV_INDEV_STATE_REL;
    }
}

lv_color_t color(const uint32_t hex) {
    return lv_color_hex(hex);
}

void initStyles() {
    if (stylesInitialized) return;

    lv_style_init(&screenStyle);
    lv_style_set_bg_color(&screenStyle, color(COLOR_BG_HEX));
    lv_style_set_bg_opa(&screenStyle, LV_OPA_COVER);
    lv_style_set_pad_all(&screenStyle, 0);
    lv_style_set_border_width(&screenStyle, 0);

    lv_style_init(&topBarStyle);
    lv_style_set_bg_color(&topBarStyle, color(COLOR_BG_HEX));
    lv_style_set_bg_opa(&topBarStyle, LV_OPA_COVER);
    lv_style_set_border_side(&topBarStyle, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&topBarStyle, 1);
    lv_style_set_border_color(&topBarStyle, color(COLOR_BORDER_HEX));
    lv_style_set_pad_all(&topBarStyle, 0);

    lv_style_init(&labelStyle);
    lv_style_set_text_color(&labelStyle, color(COLOR_TEXT_HEX));
    lv_style_set_text_font(&labelStyle, &lv_font_montserrat_16);

    lv_style_init(&mutedLabelStyle);
    lv_style_set_text_color(&mutedLabelStyle, color(COLOR_MUTED_HEX));
    lv_style_set_text_font(&mutedLabelStyle, &lv_font_montserrat_14);

    lv_style_init(&buttonStyle);
    lv_style_set_bg_color(&buttonStyle, color(COLOR_BUTTON_HEX));
    lv_style_set_bg_opa(&buttonStyle, LV_OPA_COVER);
    lv_style_set_border_color(&buttonStyle, color(COLOR_BORDER_HEX));
    lv_style_set_border_width(&buttonStyle, 1);
    lv_style_set_radius(&buttonStyle, 8);
    lv_style_set_text_color(&buttonStyle, color(COLOR_TEXT_HEX));
    lv_style_set_shadow_width(&buttonStyle, 0);
    lv_style_set_pad_all(&buttonStyle, 0);

    lv_style_init(&buttonPressedStyle);
    lv_style_set_bg_color(&buttonPressedStyle, color(COLOR_BUTTON_PRESSED_HEX));
    lv_style_set_translate_y(&buttonPressedStyle, 1);

    lv_style_init(&circleButtonStyle);
    lv_style_set_bg_color(&circleButtonStyle, color(COLOR_BUTTON_HEX));
    lv_style_set_bg_opa(&circleButtonStyle, LV_OPA_COVER);
    lv_style_set_border_color(&circleButtonStyle, color(COLOR_BORDER_HEX));
    lv_style_set_border_width(&circleButtonStyle, 1);
    lv_style_set_radius(&circleButtonStyle, LV_RADIUS_CIRCLE);
    lv_style_set_text_color(&circleButtonStyle, color(COLOR_TEXT_HEX));
    lv_style_set_shadow_width(&circleButtonStyle, 0);
    lv_style_set_pad_all(&circleButtonStyle, 0);

    lv_style_init(&checkboxStyle);
    lv_style_set_text_color(&checkboxStyle, color(COLOR_TEXT_HEX));
    lv_style_set_text_font(&checkboxStyle, &lv_font_montserrat_16);
    lv_style_set_pad_column(&checkboxStyle, 4);

    lv_style_init(&checkboxIndicatorStyle);
    lv_style_set_bg_color(&checkboxIndicatorStyle, color(COLOR_BG_HEX));
    lv_style_set_border_color(&checkboxIndicatorStyle, color(COLOR_BORDER_HEX));
    lv_style_set_border_width(&checkboxIndicatorStyle, 2);
    lv_style_set_radius(&checkboxIndicatorStyle, 4);

    lv_style_init(&modalStyle);
    lv_style_set_bg_color(&modalStyle, color(COLOR_PANEL_HEX));
    lv_style_set_bg_opa(&modalStyle, LV_OPA_COVER);
    lv_style_set_border_color(&modalStyle, color(COLOR_BORDER_HEX));
    lv_style_set_border_width(&modalStyle, 1);
    lv_style_set_radius(&modalStyle, 8);
    lv_style_set_pad_all(&modalStyle, 8);

    stylesInitialized = true;
}

void applyButtonStyles(lv_obj_t* button) {
    lv_obj_remove_style_all(button);
    lv_obj_add_style(button, &buttonStyle, LV_PART_MAIN);
    lv_obj_add_style(button, &buttonPressedStyle, LV_PART_MAIN | LV_STATE_PRESSED);
}

void applyCircleButtonStyles(lv_obj_t* button) {
    lv_obj_remove_style_all(button);
    lv_obj_add_style(button, &circleButtonStyle, LV_PART_MAIN);
    lv_obj_add_style(button, &buttonPressedStyle, LV_PART_MAIN | LV_STATE_PRESSED);
}

void onActionEvent(lv_event_t* event);
void onPollRateSliderEvent(lv_event_t* event);
void onThresholdSliderEvent(lv_event_t* event);
void onDownWaitSliderEvent(lv_event_t* event);
void onUpWaitSliderEvent(lv_event_t* event);
void onAutoInputKeyboardEvent(lv_event_t* event);

lv_obj_t* createLabel(lv_obj_t* parent, const char* text, const int x, const int y, const int w, const int h, const bool muted = false) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_add_style(label, muted ? &mutedLabelStyle : &labelStyle, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    return label;
}

lv_obj_t* createButton(lv_obj_t* parent,
                       const int x,
                       const int y,
                       const int w,
                       const int h,
                       const char* text,
                       const DisplayAction action) {
    lv_obj_t* button = lv_btn_create(parent);
    applyButtonStyles(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_add_event_cb(button, onActionEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(action)));

    lv_obj_t* label = lv_label_create(button);
    lv_obj_add_style(label, &labelStyle, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, w - 10);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

lv_obj_t* createCircleButton(lv_obj_t* parent,
                             const int x,
                             const int y,
                             const int diameter,
                             const char* text,
                             const DisplayAction action) {
    lv_obj_t* button = lv_btn_create(parent);
    applyCircleButtonStyles(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, diameter, diameter);
    lv_obj_add_event_cb(button, onActionEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(action)));

    lv_obj_t* label = lv_label_create(button);
    lv_obj_add_style(label, &labelStyle, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

lv_obj_t* createSlider(lv_obj_t* parent,
                       const int x,
                       const int y,
                       const int w,
                       const int minValue,
                       const int maxValue,
                       lv_event_cb_t callback) {
    lv_obj_t* slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, w, 16);
    lv_slider_set_range(slider, minValue, maxValue);
    lv_obj_set_style_bg_color(slider, color(0x1B2A2F), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, color(COLOR_BUTTON_HEX), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, color(COLOR_BORDER_HEX), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_PRESS_LOST, nullptr);
    return slider;
}

const char* pageTitle() {
    switch (currentPage) {
        case DisplayPage::Automation: return "Automation";
        case DisplayPage::Pairing: return "Pairing";
        case DisplayPage::WiFi: return "WiFi";
        case DisplayPage::Home:
        default:
            return "Shade";
    }
}

void formatTopBarText(char* luxText,
                      size_t luxSize,
                      char* wifiText,
                      size_t wifiSize,
                      char* posText,
                      size_t posSize,
                      char* countdownText,
                      size_t countdownSize) {
    const float lux = panelLightMeter ? panelLightMeter->getCurrentLux() : 0.0f;
    snprintf(luxText, luxSize, "Lux: %.0f", lux);

    String wifiName = "No WiFi";
    if (panelNetwork && panelNetwork->connected()) {
        wifiName = WiFi.SSID();
    } else if (panelNetwork && panelNetwork->softAPOpened) {
        wifiName = "Setup AP";
    } else if (panelNetwork && panelNetwork->connecting()) {
        wifiName = "Connecting";
    }
    snprintf(wifiText, wifiSize, "WiFi: %s", wifiName.c_str());

    snprintf(posText, posSize, "%%" LV_SYMBOL_DOWN ": %d",
             somfyController ? somfyController->shade.transformPosition(somfyController->shade.currentPos) : 0);

    countdownText[0] = '\0';
    if (automationController) {
        const int8_t statusCode = automationController->getStatusCode();
        const unsigned long seconds = (automationController->getRemainingWaitTime() + 999UL) / 1000UL;
        if (statusCode == 1) {
            snprintf(countdownText, countdownSize, LV_SYMBOL_UP " in:%lus", seconds);
        } else if (statusCode == 2) {
            snprintf(countdownText, countdownSize, LV_SYMBOL_DOWN " in:%lus", seconds);
        }
    }
}

void updateTopBarInfo() {
    if (!luxLabel || !wifiLabel || !positionLabel) return;

    char luxText[20];
    char wifiText[80];
    char posText[24];
    char countdownText[24];
    formatTopBarText(luxText,
                     sizeof(luxText),
                     wifiText,
                     sizeof(wifiText),
                     posText,
                     sizeof(posText),
                     countdownText,
                     sizeof(countdownText));

    lv_label_set_text(luxLabel, luxText);
    lv_label_set_text(wifiLabel, wifiText);
    lv_label_set_text(positionLabel, posText);
    if (autoCountdownLabel) {
        lv_label_set_text(autoCountdownLabel, countdownText);
    }
}

void updateAutomationCheckbox() {
    if (!autoCheckbox) return;
    const bool enabled = automationController && automationController->isEnabled();
    if (enabled) {
        lv_obj_add_state(autoCheckbox, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(autoCheckbox, LV_STATE_CHECKED);
    }
    lastAutomationEnabled = enabled;
}

void updatePairingStatus() {
    if (!pairingStatusLabel || !somfyShade) return;

    char text[144];
    if (somfyShade->linkedRemoteAddress != 0) {
        snprintf(text,
                 sizeof(text),
                 "%s\nLinked remote: %06lX",
                 somfyShade->paired ? "Shade is marked paired" : "Shade is not marked paired",
                 static_cast<unsigned long>(somfyShade->linkedRemoteAddress));
    } else if (somfyController && somfyController->isLearningRemote()) {
        snprintf(text,
                 sizeof(text),
                 "%s\nLearning remote: press physical remote",
                 somfyShade->paired ? "Shade is marked paired" : "Shade is not marked paired");
    } else {
        snprintf(text,
                 sizeof(text),
                 "%s\nNo linked remote",
                 somfyShade->paired ? "Shade is marked paired" : "Shade is not marked paired");
    }
    lv_label_set_text(pairingStatusLabel, text);
}

void updateWiFiStatus() {
    if (!wifiConnectedLabel) return;
    String status = "not connected";
    if (panelNetwork && panelNetwork->connected()) {
        status = WiFi.SSID();
    } else if (panelNetwork && panelNetwork->softAPOpened) {
        status = String("setup AP ") + WiFi.softAPSSID();
    } else if (panelNetwork && panelNetwork->connecting()) {
        status = "connecting";
    }
    lv_label_set_text_fmt(wifiConnectedLabel, "WiFi: %s", status.c_str());
}

int pollDelayToSliderValue() {
    if (!panelLightMeter) return 20;
    const unsigned long delayMs = panelLightMeter->getMeasurementDelay();
    return constrain(static_cast<int>((delayMs + 50UL) / 100UL), POLL_RATE_MIN_DS, POLL_RATE_MAX_DS);
}

void updatePollRateLabel() {
    if (!pollRateLabel || !pollRateSlider) return;
    const int valueDs = lv_slider_get_value(pollRateSlider);
    lv_label_set_text_fmt(pollRateLabel, "Light poll rate: %.1f s", valueDs / 10.0f);
}

void updateThresholdLabel() {
    if (!thresholdLabel || !thresholdSlider) return;
    lv_label_set_text_fmt(thresholdLabel,
                          "Lux thresholds: up <= %d, down >= %d",
                          static_cast<int>(lv_slider_get_left_value(thresholdSlider)),
                          static_cast<int>(lv_slider_get_value(thresholdSlider)));
}

void updateDownWaitLabel() {
    if (!downWaitLabel || !downWaitSlider) return;
    lv_label_set_text_fmt(downWaitLabel, "Wait before down: %d s", static_cast<int>(lv_slider_get_value(downWaitSlider)));
}

void updateUpWaitLabel() {
    if (!upWaitLabel || !upWaitSlider) return;
    lv_label_set_text_fmt(upWaitLabel, "Wait before up: %d s", static_cast<int>(lv_slider_get_value(upWaitSlider)));
}

bool parseNumber(const char* text, double& value) {
    if (!text || text[0] == '\0') return false;
    char* end = nullptr;
    value = strtod(text, &end);
    if (end == text) return false;
    while (*end == ' ' || *end == '\t') end++;
    return *end == '\0';
}

double clampDouble(const double value, const double low, const double high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

const char* autoFieldTitle(const AutoField field) {
    switch (field) {
        case AutoField::PollRate: return "Light poll rate (s)";
        case AutoField::UpLux: return "Up threshold (lux)";
        case AutoField::DownLux: return "Down threshold (lux)";
        case AutoField::DownWait: return "Wait before down (s)";
        case AutoField::UpWait: return "Wait before up (s)";
        case AutoField::ShadeUpTime: return "Shade up time (s)";
        case AutoField::ShadeDownTime: return "Shade down time (s)";
        case AutoField::None:
        default:
            return "";
    }
}

void formatAutoFieldValue(const AutoField field, char* buffer, const size_t size) {
    switch (field) {
        case AutoField::PollRate:
            snprintf(buffer, size, "%.1f", panelLightMeter ? panelLightMeter->getMeasurementDelay() / 1000.0f : 2.0f);
            break;
        case AutoField::UpLux:
            snprintf(buffer, size, "%.0f", automationController ? automationController->getUpLuxThreshold() : 3000.0f);
            break;
        case AutoField::DownLux:
            snprintf(buffer, size, "%.0f", automationController ? automationController->getDownLuxThreshold() : 25000.0f);
            break;
        case AutoField::DownWait:
            snprintf(buffer, size, "%lu", automationController ? automationController->getDownWaitTime() / 1000UL : 30UL);
            break;
        case AutoField::UpWait:
            snprintf(buffer, size, "%lu", automationController ? automationController->getUpWaitTime() / 1000UL : 30UL);
            break;
        case AutoField::ShadeUpTime:
            snprintf(buffer, size, "%.1f", somfyShade ? somfyShade->upTime / 1000.0f : 10.0f);
            break;
        case AutoField::ShadeDownTime:
            snprintf(buffer, size, "%.1f", somfyShade ? somfyShade->downTime / 1000.0f : 10.0f);
            break;
        case AutoField::None:
        default:
            buffer[0] = '\0';
            break;
    }
}

void openAutoInputModal(const AutoField field);
void saveAutoInput();

void buildScreen();
void finishWiFiScan(int count);

void setPage(const DisplayPage page) {
    if (currentPage == page) return;
    currentPage = page;
    selectedSsid[0] = '\0';
    buildScreen();
}

void connectSelectedWiFi() {
    if (!panelSettings || !panelNetwork || selectedSsid[0] == '\0' || !passwordArea) return;

    const char* password = lv_textarea_get_text(passwordArea);
    panelSettings->WIFI.set(selectedSsid, password);
    panelSettings->WIFI.save();
    panelNetwork->connectWiFi();

    selectedSsid[0] = '\0';
    buildScreen();
}

void scanWiFiNetworks() {
    scannedNetworkCount = 0;
    memset(scannedNetworkSecured, 0, sizeof(scannedNetworkSecured));
    selectedSsid[0] = '\0';
    wifiScanInProgress = true;
    wifiScanStartedAt = millis();
    strlcpy(wifiScanStatusText, "Scanning...", sizeof(wifiScanStatusText));

    if (wifiStatusLabel) {
        lv_label_set_text(wifiStatusLabel, wifiScanStatusText);
        lv_timer_handler();
    }

    WiFi.scanDelete();
    if (panelNetwork) {
        panelNetwork->stopWiFiAttempt();
    }
    WiFi.mode(WIFI_AP_STA);

    const int result = WiFi.scanNetworks(true, true, false, 500);
    Serial.printf("Display: async WiFi scan start result: %d\n", result);
    if (result == WIFI_SCAN_RUNNING) {
        return;
    }

    if (result >= 0) {
        finishWiFiScan(result);
        return;
    }

    wifiScanInProgress = false;
    snprintf(wifiScanStatusText, sizeof(wifiScanStatusText), "WiFi scan failed (%d).", result);
    WiFi.scanDelete();
    buildScreen();
}

void finishWiFiScan(const int count) {
    for (int i = 0; i < count && scannedNetworkCount < 4; i++) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;
        bool duplicate = false;
        for (uint8_t existing = 0; existing < scannedNetworkCount; existing++) {
            if (ssid == scannedNetworks[existing]) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        strlcpy(scannedNetworks[scannedNetworkCount], ssid.c_str(), sizeof(scannedNetworks[scannedNetworkCount]));
        scannedNetworkSecured[scannedNetworkCount] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        scannedNetworkCount++;
    }

    snprintf(wifiScanStatusText,
             sizeof(wifiScanStatusText),
             scannedNetworkCount == 0 ? "No networks found." : "Select a network.");
    WiFi.scanDelete();
    wifiScanInProgress = false;
    buildScreen();
}

void handleWiFiScanProgress() {
    if (!wifiScanInProgress) return;

    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) {
        if (millis() - wifiScanStartedAt > WIFI_SCAN_TIMEOUT_MS) {
            WiFi.scanDelete();
            wifiScanInProgress = false;
            strlcpy(wifiScanStatusText, "WiFi scan timed out.", sizeof(wifiScanStatusText));
            if (currentPage == DisplayPage::WiFi) buildScreen();
        }
        return;
    }

    if (count == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
        wifiScanInProgress = false;
        strlcpy(wifiScanStatusText, "WiFi scan failed.", sizeof(wifiScanStatusText));
        if (currentPage == DisplayPage::WiFi) buildScreen();
        return;
    }

    finishWiFiScan(count);
}

void selectWiFiNetwork(const uint8_t index) {
    if (index >= scannedNetworkCount) return;

    if (!scannedNetworkSecured[index]) {
        if (panelSettings && panelNetwork) {
            panelSettings->WIFI.set(scannedNetworks[index], "");
            panelSettings->WIFI.save();
            panelNetwork->connectWiFi();
            strlcpy(wifiScanStatusText, "Connecting to open network...", sizeof(wifiScanStatusText));
        }
        selectedSsid[0] = '\0';
        buildScreen();
        return;
    }

    strlcpy(selectedSsid, scannedNetworks[index], sizeof(selectedSsid));
    buildScreen();
}

void startAutoFieldEdit(const DisplayAction action) {
    switch (action) {
        case DisplayAction::AutoEditPollRate:
            openAutoInputModal(AutoField::PollRate);
            break;
        case DisplayAction::AutoEditUpLux:
            openAutoInputModal(AutoField::UpLux);
            break;
        case DisplayAction::AutoEditDownLux:
            openAutoInputModal(AutoField::DownLux);
            break;
        case DisplayAction::AutoEditDownWait:
            openAutoInputModal(AutoField::DownWait);
            break;
        case DisplayAction::AutoEditUpWait:
            openAutoInputModal(AutoField::UpWait);
            break;
        case DisplayAction::PairEditUpTime:
            openAutoInputModal(AutoField::ShadeUpTime);
            break;
        case DisplayAction::PairEditDownTime:
            openAutoInputModal(AutoField::ShadeDownTime);
            break;
        default:
            break;
    }
}

void handleAction(const DisplayAction action) {
    switch (action) {
        case DisplayAction::ShadeUp:
            Serial.println("Display: UP button pressed");
            if (somfyShade) somfyShade->sendCommand(somfy_commands::Up);
            break;
        case DisplayAction::ShadeMy:
            Serial.println("Display: MY button pressed");
            if (somfyShade) somfyShade->sendCommand(somfy_commands::My);
            break;
        case DisplayAction::ShadeDown:
            Serial.println("Display: DOWN button pressed");
            if (somfyShade) somfyShade->sendCommand(somfy_commands::Down);
            break;
        case DisplayAction::ToggleAutomation:
            if (automationController) {
                automationController->setEnabled(!automationController->isEnabled());
                Serial.printf("Display: automation %s\n", automationController->isEnabled() ? "enabled" : "disabled");
                updateAutomationCheckbox();
            }
            break;
        case DisplayAction::PageHome:
            setPage(DisplayPage::Home);
            break;
        case DisplayAction::PageAutomation:
            setPage(DisplayPage::Automation);
            break;
        case DisplayAction::PagePairing:
            setPage(DisplayPage::Pairing);
            break;
        case DisplayAction::PageWiFi:
            setPage(DisplayPage::WiFi);
            break;
        case DisplayAction::PairProg:
            if (somfyShade) somfyShade->sendCommand(somfy_commands::Prog);
            break;
        case DisplayAction::PairToggle:
            if (somfyShade) {
                somfyShade->setPaired(!somfyShade->paired);
                buildScreen();
            }
            break;
        case DisplayAction::RemoteLearn:
            if (somfyController) {
                somfyController->beginRemoteLearn();
                buildScreen();
            }
            break;
        case DisplayAction::RemoteClear:
            if (somfyController) {
                somfyController->clearLinkedRemote();
                buildScreen();
            }
            break;
        case DisplayAction::WiFiScan:
            scanWiFiNetworks();
            break;
        case DisplayAction::WiFiNetwork0:
        case DisplayAction::WiFiNetwork1:
        case DisplayAction::WiFiNetwork2:
        case DisplayAction::WiFiNetwork3:
            selectWiFiNetwork(static_cast<uint8_t>(action) - static_cast<uint8_t>(DisplayAction::WiFiNetwork0));
            break;
        case DisplayAction::WiFiConnect:
            connectSelectedWiFi();
            break;
        case DisplayAction::WiFiCancel:
            selectedSsid[0] = '\0';
            buildScreen();
            break;
        case DisplayAction::PowerOff:
            turnBacklightOff();
            break;
        case DisplayAction::AutoEditPollRate:
        case DisplayAction::AutoEditUpLux:
        case DisplayAction::AutoEditDownLux:
        case DisplayAction::AutoEditDownWait:
        case DisplayAction::AutoEditUpWait:
        case DisplayAction::PairEditUpTime:
        case DisplayAction::PairEditDownTime:
            startAutoFieldEdit(action);
            break;
        case DisplayAction::AutoInputSave:
            saveAutoInput();
            break;
        case DisplayAction::AutoInputCancel:
            editingAutoField = AutoField::None;
            buildScreen();
            break;
    }
}

void onActionEvent(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_VALUE_CHANGED) return;

    const uintptr_t actionValue = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    handleAction(static_cast<DisplayAction>(actionValue));
}

void onKeyboardEvent(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        connectSelectedWiFi();
    } else if (code == LV_EVENT_CANCEL) {
        selectedSsid[0] = '\0';
        buildScreen();
    }
}

void onAutoInputKeyboardEvent(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        saveAutoInput();
    } else if (code == LV_EVENT_CANCEL) {
        editingAutoField = AutoField::None;
        buildScreen();
    }
}

void onPollRateSliderEvent(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    updatePollRateLabel();
    if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && panelLightMeter && pollRateSlider) {
        panelLightMeter->setMeasurementDelay(static_cast<unsigned long>(lv_slider_get_value(pollRateSlider)) * 100UL);
    }
}

void onThresholdSliderEvent(lv_event_t* event) {
    if (!thresholdSlider) return;
    const lv_event_code_t code = lv_event_get_code(event);

    int lower = lv_slider_get_left_value(thresholdSlider);
    int upper = lv_slider_get_value(thresholdSlider);
    if (upper - lower < LUX_THRESHOLD_MIN_GAP) {
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            upper = min(LUX_THRESHOLD_MAX, lower + LUX_THRESHOLD_MIN_GAP);
            lower = max(LUX_THRESHOLD_MIN, upper - LUX_THRESHOLD_MIN_GAP);
            lv_slider_set_left_value(thresholdSlider, lower, LV_ANIM_OFF);
            lv_slider_set_value(thresholdSlider, upper, LV_ANIM_OFF);
        }
    }

    updateThresholdLabel();
    if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && automationController) {
        lower = lv_slider_get_left_value(thresholdSlider);
        upper = lv_slider_get_value(thresholdSlider);
        if (static_cast<float>(lower) >= automationController->getDownLuxThreshold()) {
            automationController->setDownLuxThreshold(static_cast<float>(upper));
            automationController->setUpLuxThreshold(static_cast<float>(lower));
        } else {
            automationController->setUpLuxThreshold(static_cast<float>(lower));
            automationController->setDownLuxThreshold(static_cast<float>(upper));
        }
        updateThresholdLabel();
    }
}

void onDownWaitSliderEvent(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    updateDownWaitLabel();
    if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && automationController && downWaitSlider) {
        automationController->setDownWaitTime(static_cast<unsigned long>(lv_slider_get_value(downWaitSlider)) * 1000UL);
    }
}

void onUpWaitSliderEvent(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    updateUpWaitLabel();
    if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && automationController && upWaitSlider) {
        automationController->setUpWaitTime(static_cast<unsigned long>(lv_slider_get_value(upWaitSlider)) * 1000UL);
    }
}

void openAutoInputModal(const AutoField field) {
    editingAutoField = field;

    lv_obj_t* root = lv_scr_act();
    lv_obj_t* modal = lv_obj_create(root);
    lv_obj_remove_style_all(modal);
    lv_obj_add_style(modal, &modalStyle, LV_PART_MAIN);
    lv_obj_set_pos(modal, 8, TOP_BAR_H);
    lv_obj_set_size(modal, 464, SCREEN_H - TOP_BAR_H);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(modal);

    createLabel(modal, autoFieldTitle(field), 12, 8, 430, 24);

    char valueText[24];
    formatAutoFieldValue(field, valueText, sizeof(valueText));
    autoInputArea = lv_textarea_create(modal);
    lv_obj_set_pos(autoInputArea, 12, 36);
    lv_obj_set_size(autoInputArea, 428, 40);
    lv_textarea_set_one_line(autoInputArea, true);
    lv_textarea_set_accepted_chars(autoInputArea, "0123456789.");
    lv_textarea_set_max_length(autoInputArea, 8);
    lv_textarea_set_text(autoInputArea, valueText);
    lv_obj_set_style_text_color(autoInputArea, color(COLOR_TEXT_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_color(autoInputArea, color(COLOR_BG_HEX), LV_PART_MAIN);
    lv_obj_set_style_border_color(autoInputArea, color(COLOR_BORDER_HEX), LV_PART_MAIN);

    lv_obj_t* keyboardHost = lv_obj_create(modal);
    lv_obj_remove_style_all(keyboardHost);
    lv_obj_set_pos(keyboardHost, 0, 84);
    lv_obj_set_size(keyboardHost, 446, 168);
    lv_obj_clear_flag(keyboardHost, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* keyboard = lv_keyboard_create(keyboardHost);
    lv_obj_set_pos(keyboard, 0, 0);
    lv_obj_set_size(keyboard, 448, 168);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_keyboard_set_textarea(keyboard, autoInputArea);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_event_cb(keyboard, onAutoInputKeyboardEvent, LV_EVENT_ALL, nullptr);
    lv_obj_set_style_bg_color(keyboard, color(COLOR_PANEL_HEX), LV_PART_MAIN);
    lv_obj_set_style_text_color(keyboard, color(COLOR_TEXT_HEX), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, color(COLOR_BUTTON_HEX), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, color(COLOR_BUTTON_PRESSED_HEX), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_state(autoInputArea, LV_STATE_FOCUSED);
}

void saveAutoInput() {
    if (!autoInputArea || editingAutoField == AutoField::None) return;

    double value = 0.0;
    if (!parseNumber(lv_textarea_get_text(autoInputArea), value)) {
        strlcpy(automationStatusText, "Invalid number; setting not changed.", sizeof(automationStatusText));
        editingAutoField = AutoField::None;
        buildScreen();
        return;
    }

    bool clamped = false;
    switch (editingAutoField) {
        case AutoField::PollRate: {
            const double adjusted = clampDouble(value, 0.5, 60.0);
            clamped = adjusted != value;
            if (panelLightMeter) {
                panelLightMeter->setMeasurementDelay(static_cast<unsigned long>(adjusted * 1000.0));
            }
            break;
        }
        case AutoField::UpLux: {
            const double currentDown = automationController ? automationController->getDownLuxThreshold() : 25000.0;
            const double maxUp = max(static_cast<double>(LUX_THRESHOLD_MIN), currentDown - LUX_THRESHOLD_MIN_GAP);
            const double adjusted = clampDouble(value, LUX_THRESHOLD_MIN, maxUp);
            clamped = adjusted != value;
            if (automationController) {
                automationController->setUpLuxThreshold(static_cast<float>(adjusted));
            }
            break;
        }
        case AutoField::DownLux: {
            const double currentUp = automationController ? automationController->getUpLuxThreshold() : 3000.0;
            const double minDown = min(static_cast<double>(LUX_THRESHOLD_MAX), currentUp + LUX_THRESHOLD_MIN_GAP);
            const double adjusted = clampDouble(value, minDown, LUX_THRESHOLD_MAX);
            clamped = adjusted != value;
            if (automationController) {
                automationController->setDownLuxThreshold(static_cast<float>(adjusted));
            }
            break;
        }
        case AutoField::DownWait: {
            const double adjusted = clampDouble(value, WAIT_TIME_MIN_S, WAIT_TIME_MAX_S);
            clamped = adjusted != value;
            if (automationController) {
                automationController->setDownWaitTime(static_cast<unsigned long>(adjusted * 1000.0));
            }
            break;
        }
        case AutoField::UpWait: {
            const double adjusted = clampDouble(value, WAIT_TIME_MIN_S, WAIT_TIME_MAX_S);
            clamped = adjusted != value;
            if (automationController) {
                automationController->setUpWaitTime(static_cast<unsigned long>(adjusted * 1000.0));
            }
            break;
        }
        case AutoField::ShadeUpTime: {
            const double adjusted = clampDouble(value, 0.1, 600.0);
            clamped = adjusted != value;
            if (somfyShade) {
                somfyShade->upTime = static_cast<uint32_t>(adjusted * 1000.0);
                somfyShade->save();
            }
            break;
        }
        case AutoField::ShadeDownTime: {
            const double adjusted = clampDouble(value, 0.1, 600.0);
            clamped = adjusted != value;
            if (somfyShade) {
                somfyShade->downTime = static_cast<uint32_t>(adjusted * 1000.0);
                somfyShade->save();
            }
            break;
        }
        case AutoField::None:
            break;
    }

    strlcpy(automationStatusText,
            clamped ? "Value was outside range and was clamped." : "Setting saved.",
            sizeof(automationStatusText));
    editingAutoField = AutoField::None;
    buildScreen();
}

void createTopBar(lv_obj_t* root) {
    lv_obj_t* topBar = lv_obj_create(root);
    lv_obj_remove_style_all(topBar);
    lv_obj_add_style(topBar, &topBarStyle, LV_PART_MAIN);
    lv_obj_set_pos(topBar, 0, 0);
    lv_obj_set_size(topBar, SCREEN_W, TOP_BAR_H);

    if (currentPage != DisplayPage::Home) {
        createButton(topBar, 8, 8, 72, 40, "HOME", DisplayAction::PageHome);
        luxLabel = createLabel(topBar, "", 88, 17, 70, 18);
        wifiLabel = createLabel(topBar, "", 162, 17, 106, 18);
        positionLabel = createLabel(topBar, "", 272, 6, 64, 18);
        autoCountdownLabel = createLabel(topBar, "", 272, 31, 64, 16, true);
    } else {
        luxLabel = createLabel(topBar, "", 8, 17, 78, 18);
        wifiLabel = createLabel(topBar, "", 92, 17, 154, 18);
        positionLabel = createLabel(topBar, "", 250, 6, 74, 18);
        autoCountdownLabel = createLabel(topBar, "", 250, 31, 74, 16, true);
    }

    autoCheckbox = lv_checkbox_create(topBar);
    lv_obj_add_style(autoCheckbox, &checkboxStyle, LV_PART_MAIN);
    lv_obj_add_style(autoCheckbox, &checkboxIndicatorStyle, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(autoCheckbox, color(COLOR_BUTTON_HEX), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(autoCheckbox, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_pos(autoCheckbox, 340, 13);
    lv_obj_set_size(autoCheckbox, 82, 30);
    lv_checkbox_set_text(autoCheckbox, "Auto");
    lv_obj_add_event_cb(autoCheckbox, onActionEvent, LV_EVENT_VALUE_CHANGED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(DisplayAction::ToggleAutomation)));
    createButton(topBar, 432, 8, 40, 40, LV_SYMBOL_POWER, DisplayAction::PowerOff);

    updateTopBarInfo();
    updateAutomationCheckbox();
}

void buildHomePage(lv_obj_t* root) {
    createCircleButton(root, 22, 70, 72, LV_SYMBOL_UP, DisplayAction::ShadeUp);
    createCircleButton(root, 22, 154, 72, "my", DisplayAction::ShadeMy);
    createCircleButton(root, 22, 238, 72, LV_SYMBOL_DOWN, DisplayAction::ShadeDown);

    lv_obj_t* settingsLabel = createLabel(root, "Settings", 124, 66, 220, 34);
    lv_obj_set_style_text_font(settingsLabel, &lv_font_montserrat_24, LV_PART_MAIN);
    createButton(root, 124, 116, 340, 50, "AUTOMATION", DisplayAction::PageAutomation);
    createButton(root, 124, 180, 340, 50, "PAIRING", DisplayAction::PagePairing);
    createButton(root, 124, 244, 340, 50, "WI-FI", DisplayAction::PageWiFi);
}

void buildAutomationPage(lv_obj_t* root) {
    createLabel(root, "Automation", 24, 66, 220, 28);

    char valueText[24];
    createLabel(root, "Light poll rate", 24, 100, 205, 24, true);
    formatAutoFieldValue(AutoField::PollRate, valueText, sizeof(valueText));
    strlcat(valueText, " s", sizeof(valueText));
    createButton(root, 292, 94, 150, 34, valueText, DisplayAction::AutoEditPollRate);

    createLabel(root, "Up threshold", 24, 138, 205, 24, true);
    formatAutoFieldValue(AutoField::UpLux, valueText, sizeof(valueText));
    strlcat(valueText, " lux", sizeof(valueText));
    createButton(root, 292, 132, 150, 34, valueText, DisplayAction::AutoEditUpLux);

    createLabel(root, "Down threshold", 24, 176, 205, 24, true);
    formatAutoFieldValue(AutoField::DownLux, valueText, sizeof(valueText));
    strlcat(valueText, " lux", sizeof(valueText));
    createButton(root, 292, 170, 150, 34, valueText, DisplayAction::AutoEditDownLux);

    createLabel(root, "Wait before down", 24, 214, 205, 24, true);
    formatAutoFieldValue(AutoField::DownWait, valueText, sizeof(valueText));
    strlcat(valueText, " s", sizeof(valueText));
    createButton(root, 292, 208, 150, 34, valueText, DisplayAction::AutoEditDownWait);

    createLabel(root, "Wait before up", 24, 252, 205, 24, true);
    formatAutoFieldValue(AutoField::UpWait, valueText, sizeof(valueText));
    strlcat(valueText, " s", sizeof(valueText));
    createButton(root, 292, 246, 150, 34, valueText, DisplayAction::AutoEditUpWait);

    automationStatusLabel = createLabel(root, automationStatusText, 24, 288, 430, 22, true);
}

void buildPairingPage(lv_obj_t* root) {
    createLabel(root, "Pairing", 24, 64, 220, 26);

    char valueText[24];
    createLabel(root, "Shade up time", 24, 94, 205, 22, true);
    formatAutoFieldValue(AutoField::ShadeUpTime, valueText, sizeof(valueText));
    strlcat(valueText, " s", sizeof(valueText));
    createButton(root, 292, 88, 150, 34, valueText, DisplayAction::PairEditUpTime);

    createLabel(root, "Shade down time", 24, 132, 205, 22, true);
    formatAutoFieldValue(AutoField::ShadeDownTime, valueText, sizeof(valueText));
    strlcat(valueText, " s", sizeof(valueText));
    createButton(root, 292, 126, 150, 34, valueText, DisplayAction::PairEditDownTime);

    createButton(root, 24, 172, 200, 38, "Send PROG", DisplayAction::PairProg);
    createButton(root,
                 248,
                 172,
                 200,
                 38,
                 somfyShade && somfyShade->paired ? "Mark Unpaired" : "Mark Paired",
                 DisplayAction::PairToggle);
    createButton(root, 24, 222, 200, 38, "Link Remote", DisplayAction::RemoteLearn);
    createButton(root, 248, 222, 200, 38, "Clear Remote", DisplayAction::RemoteClear);

    pairingStatusLabel = createLabel(root, "", 24, 270, 430, 42, true);
    updatePairingStatus();
}

void buildWiFiKeyboard(lv_obj_t* root) {
    lv_obj_t* modal = lv_obj_create(root);
    lv_obj_remove_style_all(modal);
    lv_obj_add_style(modal, &modalStyle, LV_PART_MAIN);
    lv_obj_set_style_pad_all(modal, 0, LV_PART_MAIN);
    lv_obj_set_pos(modal, 8, TOP_BAR_H);
    lv_obj_set_size(modal, 464, SCREEN_H - TOP_BAR_H);
    lv_obj_move_foreground(modal);

    createLabel(modal, selectedSsid, 12, 8, 430, 22);

    passwordArea = lv_textarea_create(modal);
    lv_obj_set_pos(passwordArea, 12, 36);
    lv_obj_set_size(passwordArea, 220, 40);
    lv_textarea_set_one_line(passwordArea, true);
    lv_textarea_set_password_mode(passwordArea, true);
    lv_textarea_set_placeholder_text(passwordArea, "Password");
    lv_obj_set_style_text_color(passwordArea, color(COLOR_TEXT_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_color(passwordArea, color(COLOR_BG_HEX), LV_PART_MAIN);
    lv_obj_set_style_border_color(passwordArea, color(COLOR_BORDER_HEX), LV_PART_MAIN);

    createButton(modal, 244, 36, 88, 40, "Cancel", DisplayAction::WiFiCancel);
    createButton(modal, 344, 36, 108, 40, "Connect", DisplayAction::WiFiConnect);

    lv_obj_t* keyboard = lv_keyboard_create(modal);
    lv_obj_set_pos(keyboard, 0, 0);
    lv_obj_set_size(keyboard, 448, 180);
    lv_keyboard_set_textarea(keyboard, passwordArea);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(keyboard, onKeyboardEvent, LV_EVENT_ALL, nullptr);
    lv_obj_set_style_bg_color(keyboard, color(COLOR_PANEL_HEX), LV_PART_MAIN);
    lv_obj_set_style_text_color(keyboard, color(COLOR_TEXT_HEX), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, color(COLOR_BUTTON_HEX), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, color(COLOR_BUTTON_PRESSED_HEX), LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_add_state(passwordArea, LV_STATE_FOCUSED);
}

void buildWiFiPage(lv_obj_t* root) {
    createLabel(root, "WiFi", 24, 72, 220, 30);
    wifiConnectedLabel = createLabel(root, "", 24, 96, 260, 22, true);
    updateWiFiStatus();
    createButton(root, 360, 68, 102, 38, "Scan", DisplayAction::WiFiScan);

    if (selectedSsid[0] != '\0') {
        buildWiFiKeyboard(root);
        return;
    }

    wifiStatusLabel = createLabel(root,
                                  wifiScanStatusText,
                                  24,
                                  124,
                                  300,
                                  24,
                                  true);
    for (uint8_t i = 0; i < scannedNetworkCount; i++) {
        createButton(root,
                     24,
                     156 + (i * 40),
                     298,
                     34,
                     scannedNetworks[i],
                     static_cast<DisplayAction>(static_cast<uint8_t>(DisplayAction::WiFiNetwork0) + i));
    }
}

void buildScreen() {
    if (!displayReady || !lvglReady) return;

    lv_obj_t* root = lv_scr_act();
    lv_obj_clean(root);
    lv_obj_remove_style_all(root);
    lv_obj_add_style(root, &screenStyle, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    luxLabel = nullptr;
    wifiLabel = nullptr;
    positionLabel = nullptr;
    autoCountdownLabel = nullptr;
    autoCheckbox = nullptr;
    pairingStatusLabel = nullptr;
    wifiConnectedLabel = nullptr;
    wifiStatusLabel = nullptr;
    passwordArea = nullptr;
    pollRateLabel = nullptr;
    thresholdLabel = nullptr;
    downWaitLabel = nullptr;
    upWaitLabel = nullptr;
    pollRateSlider = nullptr;
    thresholdSlider = nullptr;
    downWaitSlider = nullptr;
    upWaitSlider = nullptr;
    automationStatusLabel = nullptr;
    autoInputArea = nullptr;

    createTopBar(root);

    switch (currentPage) {
        case DisplayPage::Home:
            buildHomePage(root);
            break;
        case DisplayPage::Automation:
            buildAutomationPage(root);
            break;
        case DisplayPage::Pairing:
            buildPairingPage(root);
            break;
        case DisplayPage::WiFi:
            buildWiFiPage(root);
            break;
    }

    (void)pageTitle();
    lastStatusDraw = millis();
}

void beginBacklight() {
    pinMode(LCD_LED, OUTPUT);
    digitalWrite(LCD_LED, HIGH);
    ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
    ledcAttachPin(LCD_LED, BACKLIGHT_PWM_CHANNEL);
    restoreBacklight();
}

void prepareSharedBusPins() {
    pinMode(LCD_CS, OUTPUT);
    digitalWrite(LCD_CS, HIGH);

    uint8_t cc1101Csn = DEFAULT_CC1101_CSN;
    if (somfyController) {
        cc1101Csn = somfyController->transceiver.config.CSNPin;
    }
    pinMode(cc1101Csn, OUTPUT);
    digitalWrite(cc1101Csn, HIGH);

    pinMode(CTP_RST, OUTPUT);
    digitalWrite(CTP_RST, HIGH);
    delay(20);
}

void initLvgl() {
    lv_init();
    initStyles();

    lv_disp_draw_buf_init(&drawBuffer, drawBufferPixels, nullptr, SCREEN_W * DRAW_BUFFER_LINES);

    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = SCREEN_W;
    displayDriver.ver_res = SCREEN_H;
    displayDriver.flush_cb = displayFlush;
    displayDriver.draw_buf = &drawBuffer;
    lv_disp_drv_register(&displayDriver);

    lv_indev_drv_init(&touchDriver);
    touchDriver.type = LV_INDEV_TYPE_POINTER;
    touchDriver.read_cb = touchRead;
    lv_indev_drv_register(&touchDriver);

    lvglReady = true;
    lastLvTick = millis();
}

void refreshStatusIfNeeded() {
    if (millis() - lastStatusDraw < STATUS_REFRESH_MS) return;

    updateTopBarInfo();

    const bool automationEnabled = automationController && automationController->isEnabled();
    if (automationEnabled != lastAutomationEnabled) {
        updateAutomationCheckbox();
    }

    if (currentPage == DisplayPage::Pairing) {
        updatePairingStatus();
    } else if (currentPage == DisplayPage::WiFi) {
        updateWiFiStatus();
    }

    lastStatusDraw = millis();
}
}

void DisplayControlPanel::begin(SomfyShadeController* somfyShadeController,
                                SomfyShade* shade,
                                AutomaticBlindController* automation,
                                BH1750LightMeter* lightMeter,
                                ConfigSettings* settings,
                                Network* network) {
    somfyController = somfyShadeController;
    somfyShade = shade;
    automationController = automation;
    panelLightMeter = lightMeter;
    panelSettings = settings;
    panelNetwork = network;

    prepareSharedBusPins();
    beginBacklight();
    tft.init();
    tft.setRotation(0);
    releaseSharedSpiDevices();

    displayReady = true;
    initLvgl();
    buildScreen();
    Serial.println("LVGL display control panel started");
}

void DisplayControlPanel::loop() {
    if (!displayReady || !lvglReady) return;

    const uint32_t now = millis();
    lv_tick_inc(now - lastLvTick);
    lastLvTick = now;

    handleWiFiScanProgress();
    refreshStatusIfNeeded();
    updateBacklightTimeout();
    lv_timer_handler();
}
