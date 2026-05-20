#include "Web.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <cstring>
#include <cstdlib>
#include "AutomaticBlindController.h"
#include "BH1750LightMeter.h"
#include "ConfigSettings.h"
#include "Network.h"
#include "Somfy.h"

extern AutomaticBlindController automaticBlindController;
extern BH1750LightMeter lightMeter;
extern ConfigSettings settings;
extern Network net;
extern SomfyShadeController somfy;

namespace {
WebServer server(80);
bool rebootRequested = false;
uint32_t rebootAt = 0;
bool wifiScanStarted = false;
uint32_t wifiScanStartedAt = 0;
constexpr uint32_t WIFI_SCAN_TIMEOUT_MS = 15000;

bool readBool(JsonDocument& document, const char* key, bool& value) {
    JsonVariant variant = document[key];
    if (variant.isNull()) return false;
    value = variant.as<bool>();
    return true;
}

bool readUInt(JsonDocument& document, const char* key, uint32_t& value) {
    JsonVariant variant = document[key];
    if (variant.isNull()) return false;
    if (variant.is<const char*>()) {
        const char* text = variant.as<const char*>();
        value = strtoul(text, nullptr, strncmp(text, "0x", 2) == 0 || strncmp(text, "0X", 2) == 0 ? 16 : 10);
    } else {
        value = variant.as<uint32_t>();
    }
    return true;
}

bool readFloat(JsonDocument& document, const char* key, float& value) {
    JsonVariant variant = document[key];
    if (variant.isNull()) return false;
    value = variant.as<float>();
    return true;
}

bool validCommand(const String& command) {
    return command.equalsIgnoreCase("up") ||
           command.equalsIgnoreCase("open") ||
           command.equalsIgnoreCase("down") ||
           command.equalsIgnoreCase("close") ||
           command.equalsIgnoreCase("my") ||
           command.equalsIgnoreCase("stop") ||
           command.equalsIgnoreCase("prog") ||
           command.equalsIgnoreCase("pair");
}

bool addOrUpdateScannedNetwork(JsonArray networks, const String& ssid, const int32_t rssi, const bool secure) {
    if (ssid.isEmpty()) return false;

    for (JsonObject network : networks) {
        if (ssid == network["ssid"].as<const char*>()) {
            if (rssi > network["rssi"].as<int32_t>()) {
                network["rssi"] = rssi;
                network["secure"] = secure;
            }
            return false;
        }
    }

    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = ssid;
    network["rssi"] = rssi;
    network["secure"] = secure;
    return true;
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Automatic Blinds</title>
  <style>
    :root{--bg:#f5f7fb;--card:#fff;--text:#172033;--muted:#687385;--line:#d9e0ea;--blue:#2563eb;--blue-dark:#1d4ed8;--soft:#eef4ff}
    *{box-sizing:border-box}
    body{font-family:Inter,system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:linear-gradient(180deg,#eef4ff 0,#f5f7fb 280px);color:var(--text)}
    main{max-width:980px;margin:0 auto;padding:28px 18px 42px}
    .hero{display:flex;justify-content:space-between;gap:18px;align-items:flex-end;margin-bottom:18px}
    h1{margin:0;font-size:clamp(2rem,4vw,3rem);letter-spacing:-.04em}
    h2{margin:0 0 14px;font-size:1.05rem}
    section{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:20px;margin:16px 0;box-shadow:0 10px 30px #1f293314}
    .status{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}
    .status-card{background:var(--soft);border:1px solid #dbe7ff;border-radius:14px;padding:12px}
    .status-card span{display:block;color:var(--muted);font-size:.78rem;text-transform:uppercase;letter-spacing:.06em}
    .status-card strong{display:block;margin-top:4px;font-size:1.05rem}
    .button-row{display:flex;flex-wrap:wrap;gap:10px}
    .controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:14px}
    .control-btn{min-height:96px;display:flex;align-items:center;justify-content:center;font-size:1rem}
    .control-btn .icon{font-size:2.35rem;line-height:1}
    .control-btn.my .icon{font-weight:900;font-size:1.65rem;letter-spacing:-.04em}
    .utility-row{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px;margin-top:14px}
    button,input,select{font:inherit;border-radius:10px}
    button{border:1px solid var(--blue);background:var(--blue);color:white;padding:10px 14px;font-weight:650;cursor:pointer}
    button:hover{background:var(--blue-dark);border-color:var(--blue-dark)}
    button.secondary{background:white;color:var(--text);border-color:#b9c3d3}
    button.secondary:hover{background:#f8fafc}
    input,select{width:100%;border:1px solid #c8d1df;padding:10px 12px;background:#fbfdff}
    input:focus,select:focus{outline:2px solid #bfdbfe;border-color:var(--blue)}
    label{display:flex;flex-direction:column;gap:6px;color:var(--muted);font-size:.88rem;font-weight:650}
    label input,label select{color:var(--text);font-weight:400}
    input[type=checkbox]{width:auto;align-self:flex-start;transform:scale(1.15)}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:14px 16px}
    code{background:#edf2f7;padding:2px 6px;border-radius:6px}.muted{color:var(--muted)}
    .section-note{margin:14px 0 0}
  </style>
</head>
<body>
<main>
  <div class="hero">
    <div>
      <h1>Automatic Blinds</h1>
      <p class="muted">Single-shade Somfy RTS controller.</p>
    </div>
    <button class="secondary" onclick="loadStatus()">Refresh</button>
  </div>

  <section>
    <h2>Status</h2>
    <div id="status" class="status">Loading...</div>
  </section>

  <section>
    <h2>Shade Control</h2>
    <div class="controls">
      <button class="control-btn" onclick="sendShadeCommand('up')" aria-label="Raise shade" title="Up / Open"><span class="icon">▲</span></button>
      <button class="control-btn my" onclick="sendShadeCommand('my')" aria-label="My or stop" title="My / Stop"><span class="icon">my</span></button>
      <button class="control-btn" onclick="sendShadeCommand('down')" aria-label="Lower shade" title="Down / Close"><span class="icon">▼</span></button>
    </div>
    <div class="utility-row">
      <button class="secondary" onclick="pair()">Send Prog</button>
      <button id="pairStateButton" class="secondary" onclick="togglePaired()">Mark Paired</button>
      <button class="secondary" onclick="learnRemote()">Learn Existing Remote</button>
      <button class="secondary" onclick="clearRemote()">Clear Existing Remote</button>
    </div>
  </section>

  <section>
    <h2>Shade Settings</h2>
    <div class="grid">
      <label>Name <input id="shadeName"></label>
      <label>Remote Address <input id="remoteAddress"></label>
      <label>Rolling Code <input id="rollingCode" type="number" min="0" max="65535"></label>
      <label>Position 0=open 100=closed <input id="position" type="number" min="0" max="100"></label>
      <label>Up Time seconds <input id="upTime" type="number" min="0.1" step="0.1"></label>
      <label>Down Time seconds <input id="downTime" type="number" min="0.1" step="0.1"></label>
      <label>Repeats <input id="repeats" type="number" min="1" max="10"></label>
    </div>
    <button onclick="saveShade()">Save Shade</button>
    <p class="muted section-note">Linked remote: <code id="linkedRemote">0</code>. Use learn mode, then press up/down/my on your physical remote.</p>
  </section>

  <section>
    <h2>Automation</h2>
    <div class="grid">
      <label>Enabled <input id="autoEnabled" type="checkbox"></label>
      <label>Raise below lux <input id="upLux" type="number" step="1"></label>
      <label>Lower above lux <input id="downLux" type="number" step="1"></label>
      <label>Raise wait seconds <input id="upWait" type="number" min="0" step="1"></label>
      <label>Lower wait seconds <input id="downWait" type="number" min="0" step="1"></label>
      <label>Light poll interval seconds <input id="lightDelay" type="number" min="0.5" step="0.5"></label>
    </div>
    <button onclick="saveAutomation()">Save Automation</button>
  </section>

  <section>
    <h2>WiFi</h2>
    <div class="grid">
      <label>Scanned Network <select id="wifiNetworks" onchange="selectWiFiNetwork()"><option value="">Scan to choose a network</option></select></label>
      <label>SSID <input id="ssid"></label>
      <label>Password <input id="pass" type="password"></label>
      <label>Hostname <input id="hostname"></label>
      <label>NTP Server <input id="ntpServer"></label>
      <label>POSIX Time Zone <input id="posixZone"></label>
    </div>
    <button class="secondary" onclick="scanWiFi()">Scan WiFi</button>
    <button onclick="saveWiFi()">Save WiFi</button>
    <button class="secondary" onclick="saveSettings()">Save Host/Time</button>
    <button class="secondary" onclick="reboot()">Reboot</button>
  </section>

</main>

<script>
async function api(path, method='GET', body=null) {
  const url = `${apiBase}${path}${method === 'GET' ? (path.includes('?') ? '&' : '?') + '_=' + Date.now() : ''}`;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 1800);
  try {
    const res = await fetch(url, {method, cache:'no-store', headers:{'Content-Type':'application/json'}, body: body ? JSON.stringify(body) : null, signal: controller.signal});
    const json = await res.json();
    if (!res.ok) throw new Error(json.error || res.statusText);
    return json;
  } catch (error) {
    if (apiBase) {
      apiBase = '';
      return api(path, method, body);
    }
    throw error;
  } finally {
    clearTimeout(timeout);
  }
}
let refreshTimer = null;
let actionInFlight = false;
let statusInFlight = false;
let apiBase = '';
const dirtyFields = new Set();
function val(id){return document.getElementById(id).value}
function num(id){return Number(val(id))}
function checked(id){return document.getElementById(id).checked}
function secondsFromMs(ms){return Math.round((Number(ms) / 1000) * 10) / 10}
function msFromSeconds(id){return Math.round(num(id) * 1000)}
function isFieldProtected(id) {
  const element = document.getElementById(id);
  return document.activeElement === element || dirtyFields.has(id);
}
function setValue(id, value) {
  if (!isFieldProtected(id)) document.getElementById(id).value = value ?? '';
}
function setChecked(id, value) {
  if (!isFieldProtected(id)) document.getElementById(id).checked = Boolean(value);
}
function clearDirty(ids) { ids.forEach(id => dirtyFields.delete(id)); }
function updateApiBase(s) {
  if (!s.network.connected || !s.network.ip || location.hostname === s.network.ip) return;
  if (location.hostname.endsWith('.local')) apiBase = `http://${s.network.ip}`;
}
function automationStatus(a) {
  const seconds = Math.ceil(Number(a.remainingWaitTime || 0) / 1000);
  if (a.statusCode === 1) return `Up in ${seconds}s`;
  if (a.statusCode === 2) return `Down in ${seconds}s`;
  return 'Idle';
}
async function loadStatus() {
  const s = await api('/api/status');
  updateApiBase(s);
  const networkState = s.network.connected ? 'Connected' : (s.network.connecting ? 'Connecting' : 'Not connected');
  const networkDetail = s.network.connected ? `${s.network.ip} · ${s.network.rssi} dBm` : `Setup AP ${s.network.ip}`;
  const autoText = automationStatus(s.automation);
  document.getElementById('status').innerHTML =
    `<div class="status-card"><span>Network</span><strong>${networkState}</strong><small>${networkDetail}</small></div>`+
    `<div class="status-card"><span>Light</span><strong>${Number(s.light.lux).toFixed(1)} lux</strong><small>Poll ${s.light.measurementDelay} ms · Automation: ${s.automation.enabled ? 'enabled' : 'disabled'}</small></div>`+
    `<div class="status-card"><span>Automatic Control</span><strong>${autoText}</strong><small>State: ${s.automation.state}</small></div>`+
    `<div class="status-card"><span>Shade</span><strong>${s.shade.position}%</strong><small>Target ${s.shade.target}% · ${s.shade.moving ? 'moving' : 'idle'}</small></div>`+
    `<div class="status-card"><span>Radio</span><strong>${s.radio.radioInitialized ? 'Ready' : 'Not ready'}</strong><small>Paired: ${s.shade.paired} · Learning: ${s.radio.learningRemote}</small></div>`;
  pairStateButton.textContent = s.shade.paired ? 'Mark Unpaired' : 'Mark Paired';
  setValue('shadeName', s.shade.name); setValue('remoteAddress', s.shade.remoteAddress); setValue('rollingCode', s.shade.rollingCode);
  linkedRemote.textContent = s.shade.linkedRemoteAddress ? `${s.shade.linkedRemoteAddress} / 0x${s.shade.linkedRemoteAddressHex}` : 'none';
  setValue('position', s.shade.position); setValue('upTime', secondsFromMs(s.shade.upTime)); setValue('downTime', secondsFromMs(s.shade.downTime)); setValue('repeats', s.shade.repeats);
  setChecked('autoEnabled', s.automation.enabled); setValue('upLux', s.automation.upLuxThreshold); setValue('downLux', s.automation.downLuxThreshold);
  setValue('upWait', secondsFromMs(s.automation.upWaitTime)); setValue('downWait', secondsFromMs(s.automation.downWaitTime));
  setValue('lightDelay', secondsFromMs(s.light.measurementDelay));
  setValue('ssid', s.settings.ssid); setValue('hostname', s.settings.hostname); setValue('ntpServer', s.settings.ntpServer); setValue('posixZone', s.settings.posixZone);
  return s;
}
async function refreshStatus(){
  if(actionInFlight || statusInFlight) return;
  statusInFlight = true;
  try { await loadStatus(); }
  finally { statusInFlight = false; }
}
async function withAction(action) {
  actionInFlight = true;
  try {
    return await action();
  } finally {
    actionInFlight = false;
  }
}
async function sendShadeCommand(shadeCommand){await withAction(async()=>api('/api/command','POST',{command:shadeCommand})); await loadStatus()}
async function pair(){await withAction(async()=>api('/api/pair','POST')); await loadStatus()}
async function confirmPair(){await api('/api/pair/confirm','POST'); await loadStatus()}
async function togglePaired(){await api('/api/pair/confirm','POST',{paired:pairStateButton.textContent.includes('Paired')}); await loadStatus()}
async function learnRemote(){await api('/api/remote/learn','POST'); alert('Learn mode active. Press a button on the existing remote.'); await loadStatus()}
async function clearRemote(){await api('/api/remote/clear','POST'); await loadStatus()}
async function saveShade(){await api('/api/shade','POST',{name:val('shadeName'),remoteAddress:num('remoteAddress'),rollingCode:num('rollingCode'),position:num('position'),upTime:msFromSeconds('upTime'),downTime:msFromSeconds('downTime'),repeats:num('repeats')}); clearDirty(['shadeName','remoteAddress','rollingCode','position','upTime','downTime','repeats']); await loadStatus()}
async function saveAutomation(){await api('/api/automation','POST',{enabled:checked('autoEnabled'),upLuxThreshold:num('upLux'),downLuxThreshold:num('downLux'),upWaitTime:msFromSeconds('upWait'),downWaitTime:msFromSeconds('downWait'),lightMeasurementDelay:msFromSeconds('lightDelay')}); clearDirty(['autoEnabled','upLux','downLux','upWait','downWait','lightDelay']); await loadStatus()}
async function scanWiFi(){
  const select = document.getElementById('wifiNetworks');
  const previous = select.value;
  select.innerHTML = '<option value="">Scanning...</option>';
  const result = await withAction(async()=>api('/api/wifi/scan'));
  if (result.scanning) {
    setTimeout(() => scanWiFi().catch(console.warn), 1000);
    return;
  }
  select.innerHTML = '<option value="">Choose a network</option>';
  result.networks.forEach(network => {
    const option = document.createElement('option');
    option.value = network.ssid;
    option.textContent = `${network.ssid || '(hidden)'} · ${network.rssi} dBm${network.secure ? ' · secured' : ' · open'}`;
    select.appendChild(option);
  });
  if (previous) select.value = previous;
}
function selectWiFiNetwork(){
  const selected = val('wifiNetworks');
  if (selected) {
    document.getElementById('ssid').value = selected;
    dirtyFields.add('ssid');
  }
}
async function saveWiFi(){await api('/api/wifi','POST',{ssid:val('ssid'),passphrase:val('pass')}); clearDirty(['wifiNetworks','ssid','pass']); alert('WiFi saved. Reboot or wait for reconnect.'); await loadStatus()}
async function saveSettings(){await api('/api/settings','POST',{hostname:val('hostname'),ntpServer:val('ntpServer'),posixZone:val('posixZone')}); clearDirty(['hostname','ntpServer','posixZone']); alert('Settings saved. Reboot for hostname changes.'); await loadStatus()}
async function reboot(){await api('/api/reboot','POST'); alert('Rebooting.')}
document.addEventListener('input', e => { if (e.target.id && e.target.matches('input,select')) dirtyFields.add(e.target.id); });
document.addEventListener('change', e => { if (e.target.id && e.target.matches('input,select')) dirtyFields.add(e.target.id); });
loadStatus()
  .then(s => { if (!s.network.connected) scanWiFi().catch(console.warn); })
  .catch(e => status.textContent = e.message);
refreshTimer = setInterval(() => refreshStatus().catch(console.warn), 2000);
</script>
</body>
</html>
)rawliteral";
}

void Web::begin() {
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server.on("/api/command", HTTP_ANY, [this]() { handleCommand(); });
    server.on("/api/pair", HTTP_POST, [this]() { handlePair(); });
    server.on("/api/pair/confirm", HTTP_POST, [this]() { handleConfirmPair(); });
    server.on("/api/remote/learn", HTTP_POST, [this]() { handleLearnRemote(); });
    server.on("/api/remote/clear", HTTP_POST, [this]() { handleClearRemote(); });
    server.on("/api/shade", HTTP_POST, [this]() { handleShadeSettings(); });
    server.on("/api/wifi", HTTP_POST, [this]() { handleWiFi(); });
    server.on("/api/wifi/scan", HTTP_GET, [this]() { handleWiFiScan(); });
    server.on("/api/settings", HTTP_POST, [this]() { handleSettings(); });
    server.on("/api/automation", HTTP_ANY, [this]() { handleAutomation(); });
    server.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
    server.onNotFound([this]() { handleNotFound(); });
    server.begin();
    Serial.println("Web server started");
}

void Web::loop() {
    server.handleClient();
    if (rebootRequested && millis() > rebootAt) {
        ESP.restart();
    }
}

void Web::end() {
    server.stop();
}

void Web::sendCORSHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void Web::sendJson(const int statusCode, const JsonDocument& document) {
    String output;
    serializeJson(document, output);
    sendCORSHeaders();
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.send(statusCode, "application/json", output);
}

void Web::sendError(const int statusCode, const char* message) {
    JsonDocument document;
    document["ok"] = false;
    document["error"] = message;
    sendJson(statusCode, document);
}

bool Web::parseJsonBody(JsonDocument& document) {
    if (!server.hasArg("plain") || server.arg("plain").isEmpty()) return true;
    DeserializationError error = deserializeJson(document, server.arg("plain"));
    if (error) {
        sendError(400, "Invalid JSON body");
        return false;
    }
    return true;
}

void Web::handleRoot() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.send_P(200, "text/html", INDEX_HTML);
}

void Web::handleStatus() {
    JsonDocument document;
    document["ok"] = true;
    document["version"] = FW_VERSION;

    JsonObject network = document["network"].to<JsonObject>();
    network["connected"] = net.connected();
    network["connecting"] = net.connecting();
    network["softAP"] = net.softAPOpened;
    network["ip"] = net.connected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    network["rssi"] = net.connected() ? WiFi.RSSI() : 0;

    JsonObject settingsJson = document["settings"].to<JsonObject>();
    settingsJson["hostname"] = settings.hostname;
    settingsJson["serverId"] = settings.serverId;
    settingsJson["ssid"] = settings.WIFI.ssid;
    settingsJson["ntpServer"] = settings.ntpServer;
    settingsJson["posixZone"] = settings.posixZone;

    JsonObject light = document["light"].to<JsonObject>();
    light["lux"] = lightMeter.getCurrentLux();
    light["measurementDelay"] = lightMeter.getMeasurementDelay();

    JsonObject shade = document["shade"].to<JsonObject>();
    somfy.shade.appendJson(shade);

    JsonObject radio = document["radio"].to<JsonObject>();
    radio["enabled"] = somfy.transceiver.config.enabled;
    radio["radioInitialized"] = somfy.transceiver.config.radioInitialized;
    radio["frequency"] = somfy.transceiver.config.frequency;
    radio["TXPin"] = somfy.transceiver.config.TXPin;
    radio["RXPin"] = somfy.transceiver.config.RXPin;
    radio["SCKPin"] = somfy.transceiver.config.SCKPin;
    radio["MOSIPin"] = somfy.transceiver.config.MOSIPin;
    radio["MISOPin"] = somfy.transceiver.config.MISOPin;
    radio["CSNPin"] = somfy.transceiver.config.CSNPin;
    radio["learningRemote"] = somfy.isLearningRemote();

    JsonObject automation = document["automation"].to<JsonObject>();
    automation["enabled"] = automaticBlindController.isEnabled();
    automation["state"] = automaticBlindController.getStateName();
    automation["statusCode"] = automaticBlindController.getStatusCode();
    automation["remainingWaitTime"] = automaticBlindController.getRemainingWaitTime();
    automation["upLuxThreshold"] = automaticBlindController.getUpLuxThreshold();
    automation["downLuxThreshold"] = automaticBlindController.getDownLuxThreshold();
    automation["upWaitTime"] = automaticBlindController.getUpWaitTime();
    automation["downWaitTime"] = automaticBlindController.getDownWaitTime();

    sendJson(200, document);
}

void Web::handleCommand() {
    JsonDocument document;
    if (!parseJsonBody(document)) return;

    String command = server.arg("command");
    JsonVariant commandVariant = document["command"];
    if (!commandVariant.isNull()) command = commandVariant.as<String>();
    if (command.isEmpty()) {
        sendError(400, "Missing command");
        return;
    }
    if (!validCommand(command)) {
        sendError(400, "Unsupported command");
        return;
    }

    somfy.shade.sendCommand(translateSomfyCommand(command));
    JsonDocument response;
    response["ok"] = true;
    response["command"] = command;
    sendJson(200, response);
}

void Web::handlePair() {
    somfy.shade.sendCommand(somfy_commands::Prog);
    JsonDocument response;
    response["ok"] = true;
    response["message"] = "Prog command sent. Mark paired if the shade jogged.";
    sendJson(200, response);
}

void Web::handleConfirmPair() {
    JsonDocument document;
    if (!parseJsonBody(document)) return;

    bool paired = true;
    readBool(document, "paired", paired);
    somfy.shade.setPaired(paired);
    JsonDocument response;
    response["ok"] = true;
    response["paired"] = paired;
    sendJson(200, response);
}

void Web::handleLearnRemote() {
    somfy.beginRemoteLearn();
    JsonDocument response;
    response["ok"] = true;
    response["message"] = "Remote learn mode active";
    sendJson(200, response);
}

void Web::handleClearRemote() {
    somfy.clearLinkedRemote();
    JsonDocument response;
    response["ok"] = true;
    response["message"] = "Linked remote cleared";
    sendJson(200, response);
}

void Web::handleShadeSettings() {
    JsonDocument document;
    if (!parseJsonBody(document)) return;

    uint32_t numericValue = 0;
    float floatValue = 0.0f;
    bool boolValue = false;

    if (document["name"].is<const char*>()) {
        strlcpy(somfy.shade.name, document["name"].as<const char*>(), sizeof(somfy.shade.name));
    }
    if (readUInt(document, "remoteAddress", numericValue)) somfy.shade.setRemoteAddress(numericValue);
    if (readUInt(document, "rollingCode", numericValue)) somfy.shade.setRollingCode(static_cast<uint16_t>(constrain(numericValue, 0UL, 65535UL)));
    if (readFloat(document, "position", floatValue)) somfy.shade.setCurrentPosition(constrain(floatValue, 0.0f, 100.0f));
    if (readUInt(document, "upTime", numericValue)) somfy.shade.upTime = numericValue == 0 ? 1 : numericValue;
    if (readUInt(document, "downTime", numericValue)) somfy.shade.downTime = numericValue == 0 ? 1 : numericValue;
    if (readUInt(document, "repeats", numericValue)) somfy.shade.repeats = static_cast<uint8_t>(constrain(numericValue, 1UL, 10UL));
    if (readBool(document, "paired", boolValue)) somfy.shade.paired = boolValue;
    somfy.shade.save();

    JsonDocument response;
    response["ok"] = true;
    sendJson(200, response);
}

void Web::handleWiFi() {
    JsonDocument document;
    if (!parseJsonBody(document)) return;

    const char* ssid = document["ssid"] | "";
    const char* passphrase = document["passphrase"] | "";
    settings.WIFI.set(ssid, passphrase);
    settings.WIFI.save();
    net.connectWiFi();

    JsonDocument response;
    response["ok"] = true;
    response["message"] = "WiFi saved; connection started";
    sendJson(200, response);
}

void Web::handleWiFiScan() {
    int networkCount = WiFi.scanComplete();
    if (networkCount == WIFI_SCAN_RUNNING) {
        if (wifiScanStarted && millis() - wifiScanStartedAt > WIFI_SCAN_TIMEOUT_MS) {
            WiFi.scanDelete();
            wifiScanStarted = false;
            sendError(500, "WiFi scan timed out");
            return;
        }

        JsonDocument response;
        response["ok"] = true;
        response["scanning"] = true;
        sendJson(200, response);
        return;
    }

    if (networkCount == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
        wifiScanStarted = true;
        wifiScanStartedAt = millis();
        net.stopWiFiAttempt();
        WiFi.mode(WIFI_AP_STA);
        const int result = WiFi.scanNetworks(true, true, false, 500);
        if (result != WIFI_SCAN_RUNNING && result < 0) {
            wifiScanStarted = false;
            WiFi.scanDelete();
            sendError(500, "WiFi scan failed to start");
            return;
        }

        JsonDocument response;
        response["ok"] = true;
        response["scanning"] = true;
        sendJson(200, response);
        return;
    }

    JsonDocument response;
    response["ok"] = true;
    response["scanning"] = false;
    JsonArray networks = response["networks"].to<JsonArray>();
    for (int index = 0; index < networkCount; index++) {
        addOrUpdateScannedNetwork(
            networks,
            WiFi.SSID(index),
            WiFi.RSSI(index),
            WiFi.encryptionType(index) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    wifiScanStarted = false;
    sendJson(200, response);
}

void Web::handleSettings() {
    JsonDocument document;
    if (!parseJsonBody(document)) return;

    settings.setHostname(document["hostname"] | settings.hostname);
    settings.setNtp(document["ntpServer"] | settings.ntpServer, document["posixZone"] | settings.posixZone);
    settings.save();
    settings.applyTime();

    JsonDocument response;
    response["ok"] = true;
    response["message"] = "Settings saved";
    sendJson(200, response);
}

void Web::handleAutomation() {
    if (server.method() == HTTP_POST) {
        JsonDocument document;
        if (!parseJsonBody(document)) return;

        uint32_t numericValue = 0;
        float floatValue = 0.0f;
        bool boolValue = false;

        if (readBool(document, "enabled", boolValue)) automaticBlindController.setEnabled(boolValue);
        if (readFloat(document, "upLuxThreshold", floatValue)) automaticBlindController.setUpLuxThreshold(floatValue);
        if (readFloat(document, "downLuxThreshold", floatValue)) automaticBlindController.setDownLuxThreshold(floatValue);
        if (readUInt(document, "upWaitTime", numericValue)) automaticBlindController.setUpWaitTime(numericValue);
        if (readUInt(document, "downWaitTime", numericValue)) automaticBlindController.setDownWaitTime(numericValue);
        if (readUInt(document, "lightMeasurementDelay", numericValue)) lightMeter.setMeasurementDelay(numericValue);
    }
    handleStatus();
}

void Web::handleReboot() {
    rebootRequested = true;
    rebootAt = millis() + 500;
    JsonDocument response;
    response["ok"] = true;
    response["message"] = "Rebooting";
    sendJson(200, response);
}

void Web::handleNotFound() {
    if (server.method() == HTTP_OPTIONS) {
        sendCORSHeaders();
        server.send(204);
        return;
    }
    sendError(404, "Not found");
}
