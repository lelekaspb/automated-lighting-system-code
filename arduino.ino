#include <FastLED.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <WebServer.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mqtt_root_ca.h"
#include <ArduinoJson.h>
#include <Wire.h>
#include <INA226_WE.h>



#if __has_include("mqtt_config.h")
#include "mqtt_config.h"
#endif

#ifndef MQTT_SERVER_HOST
#define MQTT_SERVER_HOST ""
#endif
#ifndef MQTT_SERVER_PORT
#define MQTT_SERVER_PORT 8883
#endif
#ifndef MQTT_USERNAME
#define MQTT_USERNAME ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID "esp32-neopixel"
#endif
#ifndef MQTT_COLOR_TOPIC
#define MQTT_COLOR_TOPIC "esp32/led/color"
#endif

// ============================================================================
// CONFIGURATION
// Pins, thresholds and defaults. MQTT broker defaults come from the optional
// mqtt_config.h (see mqtt_config.example.h).
// ============================================================================

#define NUM_LEDS 137
#define DATA_PIN 16
#define LDR_PIN 34
// Default for the runtime LDR switch (see setLdrEnabled()).
#define ENABLE_LDR_CONTROL true
#define DEFAULT_BRIGHTNESS 20
#define STARTUP_TEST_MS 1500
#define MAX_SAVED_WIFI_NETWORKS 10

// If the averaged LDR reading rises above this value, we treat the room as bright
// and turn the LEDs off.
#define BRIGHT_THRESHOLD 320

// If the averaged LDR reading falls below this value, we treat the room as dark
// and turn the LEDs on.
#define DARK_THRESHOLD   220

#define SAMPLE_COUNT     12
#define SAMPLE_DELAY_MS  5
#define LDR_POLL_INTERVAL_MS 1000

// ============================================================================
// SHARED STATE
// ============================================================================

// LED strip
CRGB leds[NUM_LEDS];
CRGB currentColor(255, 200, 100);
uint8_t currentBrightness = DEFAULT_BRIGHTNESS;

// LDR
bool isLight = false;                       // ambient light state from the LDR
bool ldrEnabled = ENABLE_LDR_CONTROL;       // runtime switch; false = strip stays off
uint32_t lastLdrRead = 0;
uint16_t darkThreshold = DARK_THRESHOLD;
#define THRESHOLD_GAP 100

//Battery (INA226)
INA226_WE ina226 = INA226_WE(0x40);      
bool inaReady = false;                   
uint32_t lastInaRead = 0;                
#define INA_INTERVAL_MS 5000 

// MQTT
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);
uint32_t lastMqttAttempt = 0;
bool timeSyncStarted = false;

// ============================================================================
// WIFI
// Everything about getting the ESP32 online lives in this section.
//
//   Setup AP / portal   SSID "ESP32-Setup" -> http://192.168.4.1 form, opened when
//                       no saved network exists, all saved networks fail, or 'W'
//                       is sent in the Serial Monitor
//   Saved networks      Stored in NVS namespace "wifi" (up to MAX_SAVED_WIFI_NETWORKS)
//   Connect             connectWiFi() tries one network; connectWifiOrStartPortal()
//                       tries all saved ones, then falls back to the portal
//
// Public entry points: connectWifiOrStartPortal(), requestWifiCredentialChange()
// ============================================================================

const char *AP_SSID = "ESP32-Setup";
const char *AP_PASSWORD = "setup1234";
WebServer server(80);
bool portalConnected = false;

const char FORM[] PROGMEM = R"(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Setup</title></head><body>
<h2>WiFi setup</h2>
<form method="POST" action="/save">
  <p><input name="ssid" placeholder="SSID" required></p>
  <p><input name="pass" type="password" placeholder="Password"></p>
  <p><label><input name="save" type="checkbox" value="yes"> Save this network for later use</label></p>
  <button type="submit">Connect</button>
</form></body></html>
)";

void handleRoot() { server.send_P(200, "text/html", FORM); }

bool saveWifiCredentials(const String& ssid, const String& password);
void beginStaConnection(const String& ssid, const String& password, wifi_mode_t mode);

void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("pass");
  bool saveForLater = server.hasArg("save");
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
      (!password.isEmpty() && password.length() < 8)) {
    Serial.println("Rejected Wi-Fi credentials: invalid SSID or password length.");
    server.send(400, "text/plain", "Invalid Wi-Fi credentials.");
    return;
  }

  Serial.printf("Trying Wi-Fi credentials for SSID: %s\n", ssid.c_str());
  beginStaConnection(ssid, password, WIFI_AP_STA);
  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("Wi-Fi connection failed; status=%d\n", WiFi.status());
    WiFi.disconnect(false, true);
    server.send(200, "text/plain",
                "Could not connect to that network. Check the details and try again.");
    return;
  }

  bool saved = true;
  if (saveForLater) {
    saved = saveWifiCredentials(ssid, password);
    if (!saved) {
      Serial.println("Could not save Wi-Fi credentials.");
    }
  }

  portalConnected = !saveForLater || saved;
  String response = "Connected to Wi-Fi. ";
  if (!saveForLater) {
    response += "This network was not saved.";
  } else if (saved) {
    response += "Credentials saved for later use.";
  } else {
    response += "Could not save credentials for later use.";
  }
  server.send(saved ? 200 : 500, "text/plain", response);
}

void startPortal() {
  Serial.println("Starting Wi-Fi setup AP.");
  portalConnected = false;
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("Could not start Wi-Fi setup AP.");
    while (true) {
      delay(1000);
    }
  }
  Serial.print("AP up, open http://");
  Serial.println(WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot);
  server.begin();
  Serial.println("Wi-Fi setup portal is ready.");
  while (!portalConnected) {
    server.handleClient();
    delay(2);
  }
  server.stop();
  WiFi.mode(WIFI_STA);
}

struct WifiCredential {
  String ssid;
  String password;
};

WifiCredential savedWifiNetworks[MAX_SAVED_WIFI_NETWORKS];
uint8_t savedWifiNetworkCount = 0;

void addSavedWifiNetwork(const String& ssid, const String& password) {
  if (ssid.isEmpty() || savedWifiNetworkCount >= MAX_SAVED_WIFI_NETWORKS) {
    return;
  }
  for (uint8_t i = 0; i < savedWifiNetworkCount; i++) {
    if (savedWifiNetworks[i].ssid == ssid) {
      return;
    }
  }
  savedWifiNetworks[savedWifiNetworkCount].ssid = ssid;
  savedWifiNetworks[savedWifiNetworkCount].password = password;
  savedWifiNetworkCount++;
}

bool saveWifiCredentials(const String& ssid, const String& password) {
  Preferences preferences;
  if (!preferences.begin("wifi", false)) {
    Serial.println("Could not open Wi-Fi NVS namespace for writing.");
    return false;
  }

  int targetIndex = -1;
  bool isExistingNetwork = false;
  String legacySsid = preferences.getString("ssid", "");
  int savedNetworkCount = legacySsid.isEmpty() ? 0 : 1;
  if (legacySsid == ssid) {
    preferences.putString("pass", password);
    targetIndex = -2;
    isExistingNetwork = true;
  } else {
    for (int i = 0; i < MAX_SAVED_WIFI_NETWORKS; i++) {
      String key = "ssid" + String(i);
      String storedSsid = preferences.getString(key.c_str(), "");
      if (!storedSsid.isEmpty()) {
        savedNetworkCount++;
      }
      if (storedSsid == ssid) {
        targetIndex = i;
        isExistingNetwork = true;
      } else if (targetIndex == -1 && storedSsid.isEmpty()) {
        targetIndex = i;
      }
    }
  }

  if (targetIndex == -1 ||
      (!isExistingNetwork && savedNetworkCount >= MAX_SAVED_WIFI_NETWORKS)) {
    preferences.end();
    Serial.println("Saved Wi-Fi network limit reached.");
    return false;
  }

  if (targetIndex >= 0) {
    String ssidKey = "ssid" + String(targetIndex);
    String passKey = "pass" + String(targetIndex);
    preferences.putString(ssidKey.c_str(), ssid);
    preferences.putString(passKey.c_str(), password);
    String storedSsid = preferences.getString(ssidKey.c_str(), "");
    String storedPassword = preferences.getString(passKey.c_str(), "");
    bool saved = storedSsid == ssid && storedPassword == password;
    preferences.end();
    if (!saved) {
      Serial.println("Wi-Fi credential save verification failed.");
    }
    return saved;
  }

  String storedPassword = preferences.getString("pass", "");
  bool saved = preferences.getString("ssid", "") == ssid &&
               storedPassword == password;
  preferences.end();
  if (!saved) {
    Serial.println("Wi-Fi credential save verification failed.");
  }
  return saved;
}

bool forgetWifiCredentials(const String& ssid) {
  Preferences preferences;
  if (!preferences.begin("wifi", false)) {
    Serial.println("Could not open Wi-Fi NVS namespace for writing.");
    return false;
  }
  bool removed = false;

  if (preferences.getString("ssid", "") == ssid) {
    preferences.remove("ssid");
    preferences.remove("pass");
    removed = true;
  }
  for (int i = 0; i < MAX_SAVED_WIFI_NETWORKS; i++) {
    String ssidKey = "ssid" + String(i);
    String passKey = "pass" + String(i);
    if (preferences.getString(ssidKey.c_str(), "") == ssid) {
      preferences.remove(ssidKey.c_str());
      preferences.remove(passKey.c_str());
      removed = true;
    }
  }
  preferences.end();
  return removed;
  }
  bool forgetAllWifiCredentials() {
  Preferences preferences;
  if (!preferences.begin("wifi", false)) {
    Serial.println("Could not open Wi-Fi NVS namespace for writing.");
    return false;
  }
  bool cleared = preferences.clear();   // empties the whole "wifi" drawer
  preferences.end();
  return cleared;
  } 


bool loadWifiNetworks() {
  Serial.println("Checking saved Wi-Fi networks in NVS.");
  savedWifiNetworkCount = 0;
  Preferences preferences;
  if (!preferences.begin("wifi", true)) {
    Serial.println("Could not open Wi-Fi NVS namespace.");
    return false;
  }

  String legacySsid = preferences.getString("ssid", "");
  if (!legacySsid.isEmpty()) {
    addSavedWifiNetwork(legacySsid, preferences.getString("pass", ""));
  }
  for (int i = 0; i < MAX_SAVED_WIFI_NETWORKS; i++) {
    String ssidKey = "ssid" + String(i);
    String passwordKey = "pass" + String(i);
    String ssid = preferences.getString(ssidKey.c_str(), "");
    if (!ssid.isEmpty()) {
      addSavedWifiNetwork(ssid, preferences.getString(passwordKey.c_str(), ""));
    }
  }
  preferences.end();

  Serial.printf("Found %u saved Wi-Fi network(s).\n", savedWifiNetworkCount);
  return savedWifiNetworkCount > 0;
}

// Cancel any in-progress station attempt first; otherwise begin() fails with
// "sta is connecting, cannot set config".
void beginStaConnection(const String& ssid, const String& password, wifi_mode_t mode) {
  WiFi.mode(mode);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  delay(500);
  WiFi.begin(ssid.c_str(), password.c_str());
}

bool connectWiFi(const String& ssid, const String& password) {
  Serial.printf("Connecting to Wi-Fi SSID: %s\n", ssid.c_str());
  beginStaConnection(ssid, password, WIFI_STA);
  uint32_t startedAt = millis();
  uint32_t lastProgressAt = startedAt;
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(250);
    if (millis() - lastProgressAt >= 5000) {
      Serial.printf("Still connecting to Wi-Fi; status=%d\n", WiFi.status());
      lastProgressAt = millis();
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.printf("Wi-Fi connection timed out; status=%d\n", WiFi.status());
  return false;
}

void requestWifiCredentialChange() {
  Serial.println("Opening Wi-Fi setup portal; saved networks will be kept.");
  startPortal();
}

// Scan once and report whether the SSID is currently visible.
// Returns true on scan failure so we still fall back to a normal attempt.
bool isSsidVisible(const String& ssid, int networkCount) {
  if (networkCount < 0) {
    return true;
  }
  for (int i = 0; i < networkCount; i++) {
    if (WiFi.SSID(i) == ssid) {
      return true;
    }
  }
  return false;
}

// Entry point used by setup(): try saved networks in order, else open the portal.
void connectWifiOrStartPortal() {
  bool hasSavedNetworks = loadWifiNetworks();
  if (!hasSavedNetworks) {
    Serial.println("No saved Wi-Fi credentials; starting setup AP.");
    startPortal();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  Serial.println("Scanning for saved networks.");
  int networkCount = WiFi.scanNetworks();
  Serial.printf("Scan found %d network(s).\n", networkCount);

  bool connected = false;
  for (uint8_t i = 0; i < savedWifiNetworkCount && !connected; i++) {
    if (!isSsidVisible(savedWifiNetworks[i].ssid, networkCount)) {
      Serial.printf("Skipping saved Wi-Fi network %u of %u (not in range): %s\n",
                    i + 1, savedWifiNetworkCount,
                    savedWifiNetworks[i].ssid.c_str());
      continue;
    }
    Serial.printf("Trying saved Wi-Fi network %u of %u: %s\n",
                  i + 1, savedWifiNetworkCount,
                  savedWifiNetworks[i].ssid.c_str());
    connected = connectWiFi(savedWifiNetworks[i].ssid,
                            savedWifiNetworks[i].password);
  }
  WiFi.scanDelete();
  if (!connected) {
    Serial.println("No saved Wi-Fi network connected; starting setup AP.");
    startPortal();
  }
}
// ============================== end WIFI ====================================

// ============================================================================
// LED STRIP
// Color, brightness and on/off state of the NeoPixel strip.
// The strip is on only while automatic lighting is enabled and the room is dark
// (see ledsShouldBeOn()). Color and brightness are remembered while it is off.
//
// Public entry points: applyLedState(), setLedColor(), setLedBrightness()
// ============================================================================

bool ledsShouldBeOn() {
  // Lights run automatically only while the LDR system is enabled: on in the
  // dark, off in the light. Disabled means off (e.g. away on holiday).
  return ldrEnabled && !isLight;
}

void applyLedState() {
  Serial.printf("applyLedState: on=%d  brightness=%d  color=%d,%d,%d\n",
                ledsShouldBeOn(), currentBrightness,
                currentColor.r, currentColor.g, currentColor.b);   // DEBUG
  if (!ledsShouldBeOn()) {
    FastLED.clear();
    FastLED.show();
    return;
  }

  FastLED.setBrightness(currentBrightness);
  fill_solid(leds, NUM_LEDS, currentColor);
  FastLED.show();
}

void setLedColor(uint8_t red, uint8_t green, uint8_t blue) {
  currentColor = CRGB(red, green, blue);
  applyLedState();
}

void setLedBrightness(uint8_t brightness) {
  currentBrightness = brightness;
  applyLedState();
}

// ============================== end LED STRIP ===============================

// ============================================================================
// LDR (AMBIENT LIGHT)
// Reads the light sensor once per LDR_POLL_INTERVAL_MS and updates isLight with
// hysteresis (BRIGHT_THRESHOLD / DARK_THRESHOLD). The automatic lighting can be
// switched off at runtime with setLdrEnabled(), which keeps the strip off.
//
// Public entry points: configureLdrAdc(), updateLdrState(), setLdrEnabled()
// ============================================================================

void configureLdrAdc() {
  // Allow up to ~3.3V on the ADC input before clipping.
  analogSetPinAttenuation(LDR_PIN, ADC_11db);
}

int readAverageLdr() {
  long total = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    total += analogRead(LDR_PIN);
    delay(SAMPLE_DELAY_MS);
  }

  // Average several samples so one noisy ADC reading does not immediately
  // change the LED state.
  int avg = total / SAMPLE_COUNT;

  return avg;
}

void loadLdrThreshold() {
  Preferences preferences;
  if (!preferences.begin("ldr", true)) {
    return;   
  }
  darkThreshold = preferences.getUShort("dark", DARK_THRESHOLD);
  preferences.end();
}

bool saveLdrThreshold(uint16_t value) {
  Preferences preferences;
  if (!preferences.begin("ldr", false)) {
    return false;
  }
  bool saved = preferences.putUShort("dark", value) > 0;
  preferences.end();
  return saved;
}

void updateLdrState() {
  if (!ldrEnabled || (lastLdrRead != 0 && millis() - lastLdrRead < LDR_POLL_INTERVAL_MS)) {
    return;
  }

  lastLdrRead = millis();
  int ldrValue = readAverageLdr();
  bool previousState = isLight;
  Serial.printf("LDR: %d\n", ldrValue);

  if (ldrValue > darkThreshold + THRESHOLD_GAP) {
    isLight = true;
  } else if (ldrValue < darkThreshold) {
    isLight = false;
  }

  if (isLight != previousState) {
    applyLedState();
  }
}

// Enable/disable the automatic lighting at runtime.
// Disabling turns the strip off and keeps it off; enabling re-reads the sensor.
void setLdrEnabled(bool enabled) {
  ldrEnabled = enabled;
  Serial.printf("LDR control %s\n", enabled ? "enabled" : "disabled");
  if (enabled) {
    lastLdrRead = 0;
    updateLdrState();
  }
  applyLedState();
}
// ============================== end LDR =====================================

// ============================================================================
// MQTT CONNECTION
// Broker settings (NVS namespace "mqtt") and connect/reconnect handling.
// Waits for Wi-Fi and NTP time (needed for TLS) before connecting, retries every
// 5 s, and subscribes to all topics once connected.
//
// Public entry points: loadMqttConfig(), connectMqttIfNeeded()
// ============================================================================

// ============================================================================
// BATTERY (INA226)                                                       NEW
// Reads battery voltage (VBS) and current (shunt IN+ -> IN-) every
// INA_INTERVAL_MS. Positive current = discharging, negative = charging.
// ============================================================================

void setupIna() {
  Wire.begin(21, 22);
  inaReady = ina226.init();
  if (inaReady) {
    ina226.setResistorRange(0.1, 0.8);   // R100 shunt = 0.1 ohm, max ~0.8 A
    Serial.println("INA226 ready.");
  } else {
    Serial.println("INA226 not found, continuing without it.");
  }
}

void readInaIfNeeded() {
  if (!inaReady || millis() - lastInaRead < INA_INTERVAL_MS) {
    return;
  }
  lastInaRead = millis();

  ina226.readAndClearFlags();
  float voltage = ina226.getBusVoltage_V();
  float current = ina226.getCurrent_mA();
  Serial.printf("Battery: %.2f V   Current: %.1f mA\n", voltage, current);
}
// ============================== end BATTERY =================================
struct MqttConfig {
  String serverHost;
  uint16_t serverPort;
  String username;
  String password;
  String clientId;
  String colorTopic;
};

MqttConfig mqttConfig;

void loadMqttConfig() {
  // Load the MQTT configuration from persistent storage.
  Preferences preferences;
  preferences.begin("mqtt", true);
  mqttConfig.serverHost = preferences.getString("server_host", "").c_str();
  mqttConfig.serverPort = preferences.getUInt("server_port", 8883);
  mqttConfig.username = preferences.getString("username", "").c_str();
  mqttConfig.password = preferences.getString("password", "").c_str();
  mqttConfig.clientId = preferences.getString("client_id", "esp32-neopixel").c_str();
  mqttConfig.colorTopic = preferences.getString("color_topic", "esp32/led/color").c_str();
  preferences.end();
}

void subscribeMqttTopics();  // defined in the MQTT TOPICS section
void publishSavedWifiList();

void connectMqttIfNeeded() {
  if (mqttClient.connected() || WiFi.status() != WL_CONNECTED || mqttConfig.serverHost.isEmpty()) {
    return;
  }
  if (time(nullptr) < 1700000000) {
    return;
  }

  uint32_t now = millis();
  if (lastMqttAttempt != 0 && now - lastMqttAttempt < 5000) {
    return;
  }

  lastMqttAttempt = now;
  bool connected;
   Serial.printf("MQTT user: %s\n", mqttConfig.username.c_str()); 
  if (mqttConfig.username.isEmpty()) {
    connected = mqttClient.connect(mqttConfig.clientId.c_str());
  } else {
    connected = mqttClient.connect(mqttConfig.clientId.c_str(), mqttConfig.username.c_str(), mqttConfig.password.c_str());
  }

  if (connected) {
    Serial.println("Connected to MQTT.");
    subscribeMqttTopics();
    publishSavedWifiList();
  } else {
    Serial.printf("MQTT connection failed, state %d\n", mqttClient.state());
  }
}
// ============================== end MQTT CONNECTION =========================

// ============================================================================
// MQTT TOPICS
// All topics the ESP32 listens to live in this section.
//
//   Topic                  Payload                 Effect
//   ---------------------  ----------------------  ----------------------------
//   <color topic>          "R,G,B" (0-255 each)    Sets strip color
//                          (default esp32/led/color, configurable in NVS)
//   esp32/led/brightness   0-255                   Sets strip brightness
//   esp32/ldr/enabled      1/0, true/false, on/off Automatic lighting on / off (0 = strip stays off)
//   esp32/wifi/add         {"ssid":"..","pass":".."} Saves a Wi-Fi network in NVS (do NOT retain)
//   esp32/wifi/forget      "<ssid>" or "all"       Removes one / all saved Wi-Fi networks (do NOT retain)
//
// To add a topic: write a handler, add a name constant (if fixed), and add one
// row to MQTT_TOPICS below. Subscribing and dispatching are automatic.
// ============================================================================

const char *BRIGHTNESS_TOPIC = "esp32/led/brightness";
const char *LDR_ENABLE_TOPIC = "esp32/ldr/enabled";
const char *WIFI_ADD_TOPIC = "esp32/wifi/add";
const char *WIFI_FORGET_TOPIC = "esp32/wifi/forget";
const char *WIFI_SAVED_TOPIC = "esp32/wifi/saved";
const char *LDR_THRESHOLD_TOPIC = "esp32/ldr/threshold";

void publishSavedWifiList() {
  Preferences preferences;
  if (!preferences.begin("wifi", true)) {
    return;
  }

  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();   

  String legacySsid = preferences.getString("ssid", "");   
  if (!legacySsid.isEmpty()) {
    list.add(legacySsid);
  }
  for (int i = 0; i < MAX_SAVED_WIFI_NETWORKS; i++) {      
    String key = "ssid" + String(i);
    String ssid = preferences.getString(key.c_str(), "");
    if (!ssid.isEmpty()) {
      list.add(ssid);
    }
  }
  preferences.end();

  char buffer[512];
  serializeJson(doc, buffer, sizeof(buffer));             
  mqttClient.publish(WIFI_SAVED_TOPIC, buffer, true);      
  Serial.printf("Published saved Wi-Fi list: %s\n", buffer);
}

void handleColorMessage(const char* message) {
  int red;
  int green;
  int blue;
  char extra;
  if (sscanf(message, " %d , %d , %d %c", &red, &green, &blue, &extra) != 3 ||
      red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255) {
    Serial.println("Invalid color. Publish R,G,B values from 0 to 255.");
    return;
  }

  Serial.printf("MQTT color: %d,%d,%d\n", red, green, blue);
  // Stored even while the LDR keeps the strip off; applied when it turns on.
  setLedColor(red, green, blue);
}

void handleBrightnessMessage(const char* message) {
  int value;
  char extra;
  if (sscanf(message, " %d %c", &value, &extra) != 1 || value < 0 || value > 255) {
    Serial.println("Invalid brightness. Publish a value from 0 to 255.");
    return;
  }
  Serial.printf("MQTT brightness: %d\n", value);
  setLedBrightness((uint8_t)value);
}

void handleLdrEnableMessage(const char* message) {
  if (strcmp(message, "1") == 0 || strcasecmp(message, "true") == 0 || strcasecmp(message, "on") == 0) {
    setLdrEnabled(true);
  } else if (strcmp(message, "0") == 0 || strcasecmp(message, "false") == 0 || strcasecmp(message, "off") == 0) {
    setLdrEnabled(false);
  } else {
    Serial.println("Invalid LDR payload. Publish 1 or 0.");
  }
}
void handleLdrThresholdMessage(const char* message) {   
  int value;
  char extra;
  if (sscanf(message, " %d %c", &value, &extra) != 1 ||
      value < 0 || value > 4095 - THRESHOLD_GAP) {
    Serial.println("Invalid threshold. Publish a number from 0 to 3995.");
    return;
  }

  darkThreshold = value;
  saveLdrThreshold(value);
  Serial.printf("LDR threshold: dark below %d, bright above %d\n",
                darkThreshold, darkThreshold + THRESHOLD_GAP);

  lastLdrRead = 0;      
  updateLdrState();
}


void handleWifiAddMessage(const char* message) {
    JsonDocument doc;
  if (deserializeJson(doc, message)) {
    Serial.println("Invalid Wi-Fi message. Use {\"ssid\":\"...\",\"pass\":\"...\"}");
    return;
  }
  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";

  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 63 ||
      (!pass.isEmpty() && pass.length() < 8)) {
    Serial.println("Rejected Wi-Fi credentials: invalid SSID or password length.");
    return;
  }

  if (saveWifiCredentials(ssid, pass)) {
    Serial.printf("Saved Wi-Fi network: %s\n", ssid.c_str());
    publishSavedWifiList();
  } else {
    Serial.println("Could not save Wi-Fi network.");
  }
}
void handleWifiForgetMessage(const char* message) {   
  String ssid = message;
  ssid.trim();  
  if (ssid.isEmpty()) {
    Serial.println("Send a network name, or 'all'.");
    return;
  }

  if (ssid == "all") {
    if (forgetAllWifiCredentials()) {
      Serial.println("Forgot all saved Wi-Fi networks. Setup portal opens after the next restart.");
      publishSavedWifiList();
    } else {
      Serial.println("Could not forget saved Wi-Fi networks.");
    }
    return;
  }

  if (forgetWifiCredentials(ssid)) {
    Serial.printf("Forgot Wi-Fi network: %s\n", ssid.c_str());
    publishSavedWifiList();
  } else {
    Serial.printf("No saved Wi-Fi network named: %s\n", ssid.c_str());
  }
}
struct MqttTopic {
  const char* (*name)();
  void (*handler)(const char* message);
};

const char* colorTopicName() { return mqttConfig.colorTopic.c_str(); }
const char* brightnessTopicName() { return BRIGHTNESS_TOPIC; }
const char* ldrEnableTopicName() { return LDR_ENABLE_TOPIC; }
const char* wifiAddTopicName() { return WIFI_ADD_TOPIC; }
const char* wifiForgetTopicName() { return WIFI_FORGET_TOPIC; }
const char* ldrThresholdTopicName() { return LDR_THRESHOLD_TOPIC; }

const MqttTopic MQTT_TOPICS[] = {
  {colorTopicName,      handleColorMessage},
  {brightnessTopicName, handleBrightnessMessage},
  {ldrEnableTopicName,  handleLdrEnableMessage},
  {wifiAddTopicName, handleWifiAddMessage},
  {wifiForgetTopicName, handleWifiForgetMessage},
  {ldrThresholdTopicName, handleLdrThresholdMessage}
};
const size_t MQTT_TOPIC_COUNT = sizeof(MQTT_TOPICS) / sizeof(MQTT_TOPICS[0]);

void subscribeMqttTopics() {
  for (size_t i = 0; i < MQTT_TOPIC_COUNT; i++) {
    const char* name = MQTT_TOPICS[i].name();
    mqttClient.subscribe(name);
    Serial.printf("Subscribed to %s\n", name);
  }
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (length >= 128) {
    return;
  }

  char message[128];
  memcpy(message, payload, length);
  message[length] = '\0';

  for (size_t i = 0; i < MQTT_TOPIC_COUNT; i++) {
    if (strcmp(topic, MQTT_TOPICS[i].name()) == 0) {
      MQTT_TOPICS[i].handler(message);
      return;
    }
  }
}
// ============================== end MQTT TOPICS =============================

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  loadMqttConfig();
  configureLdrAdc();
  loadLdrThreshold();
  setupIna();

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);

  setLedColor(20, 20, 20);
  delay(STARTUP_TEST_MS);
  currentColor = CRGB(255, 200, 100);
  if (ldrEnabled) {
    int ldrValue = readAverageLdr();
      if (ldrValue > darkThreshold + THRESHOLD_GAP) {
    isLight = true;
    } else if (ldrValue < darkThreshold) {
    isLight = false;
    }
  }
  applyLedState();

  mqttClient.setServer(mqttConfig.serverHost.c_str(), mqttConfig.serverPort);
  mqttClient.setBufferSize(512);
  mqttClient.setCallback(onMqttMessage);
  wifiClient.setCACert(mqttRootCa);

  connectWifiOrStartPortal();
  Serial.println("Send W in Serial Monitor to change Wi-Fi credentials.");
}

void loop() {
  if (Serial.available() > 0) {
    char command = Serial.read();
    if (command == 'w' || command == 'W') {
      requestWifiCredentialChange();
    } else if (command != '\r' && command != '\n') {
      Serial.println("Unknown command. Send W to change Wi-Fi credentials.");
    }
  }

  if (WiFi.status() == WL_CONNECTED && !timeSyncStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    timeSyncStarted = true;
  }
  connectMqttIfNeeded();
  mqttClient.loop();
  updateLdrState();
  readInaIfNeeded();
  
  static bool wasConnected = false;                       // DEBUG
  if (wasConnected && !mqttClient.connected()) {
    Serial.printf("MQTT disconnected! state=%d, WiFi=%d\n",
                  mqttClient.state(), WiFi.status());
  }
  wasConnected = mqttClient.connected();
  delay(2);
}
