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
#ifndef MQTT_BRIGHTNESS_TOPIC
#define MQTT_BRIGHTNESS_TOPIC "esp32/led/brightness"
#endif
#define MAX_BRIGHTNESS 80

#define NUM_LEDS 137
#define DATA_PIN 5
#define LDR_PIN 34
#define SWITCH_PIN 26
#define ENABLE_LDR_CONTROL false
#define USE_STRIP_POWER_SWITCH false
#define SWITCH_ACTIVE_HIGH true
#define STARTUP_TEST_MS 1500

// If the averaged LDR reading rises above this value, we treat the room as bright
// and turn the LEDs off.
#define BRIGHT_THRESHOLD 320

// If the averaged LDR reading falls below this value, we treat the room as dark
// and turn the LEDs on.
#define DARK_THRESHOLD   220

#define SAMPLE_COUNT     12
#define SAMPLE_DELAY_MS  5
#define LDR_POLL_INTERVAL_MS 1000

CRGB leds[NUM_LEDS];
CRGB currentColor(255, 200, 100);
uint8_t currentBrightness = 20;
bool isLight = false;
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);
uint32_t lastMqttAttempt = 0;
uint32_t lastLdrRead = 0;
bool timeSyncStarted = false;

const char *AP_SSID = "ESP32-Setup";
const char *AP_PASSWORD = "setup1234";
WebServer server(80);

const char FORM[] PROGMEM = R"(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Setup</title></head><body>
<h2>WiFi setup</h2>
<form method="POST" action="/save">
  <p><input name="ssid" placeholder="SSID" required></p>
  <p><input name="pass" type="password" placeholder="Password"></p>
  <button type="submit">Save</button>
</form></body></html>
)";

void handleRoot() { server.send_P(200, "text/html", FORM); }

void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("pass");
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
      (!password.isEmpty() && password.length() < 8)) {
    Serial.println("Rejected Wi-Fi credentials: invalid SSID or password length.");
    server.send(400, "text/plain", "Invalid Wi-Fi credentials.");
    return;
  }

  Serial.printf("Saving Wi-Fi credentials for SSID: %s\n", ssid.c_str());
  Preferences preferences;
  if (!preferences.begin("wifi", false)) {
    Serial.println("Could not open Wi-Fi NVS namespace for writing.");
    server.send(500, "text/plain", "Could not save Wi-Fi credentials.");
    return;
  }
  preferences.putString("ssid", ssid);
  preferences.putString("pass", password);
  String storedSsid = preferences.getString("ssid", "");
  String storedPassword = preferences.getString("pass", "");
  bool saved = storedSsid == ssid && preferences.isKey("pass") &&
               storedPassword == password;
  preferences.end();
  if (!saved) {
    Serial.println("Wi-Fi credential save verification failed.");
    server.send(500, "text/plain", "Could not save Wi-Fi credentials.");
    return;
  }

  Serial.println("Wi-Fi credentials saved and verified; restarting.");
  server.send(200, "text/html", "Saved. Restarting...");
  delay(1000);
  ESP.restart();
}

void startPortal() {
  Serial.println("Starting Wi-Fi setup AP.");
  WiFi.mode(WIFI_AP);
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
  while (true) {
    server.handleClient();
    delay(2);
  }
}

struct MqttConfig {
  String serverHost;
  uint16_t serverPort;
  String username;
  String password;
  String clientId;
  String colorTopic;
  String brightnessTopic;
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
  mqttConfig.brightnessTopic = preferences.getString("brightness_topic", "esp32/led/brightness").c_str();
  preferences.end();
}

void configureLdrAdc() {
  // Allow up to ~3.3V on the ADC input before clipping.
  analogSetPinAttenuation(LDR_PIN, ADC_11db);
}

void setStripPower(bool on) {
  if (!USE_STRIP_POWER_SWITCH) {
    return;
  }

  bool level = SWITCH_ACTIVE_HIGH ? on : !on;
  digitalWrite(SWITCH_PIN, level ? HIGH : LOW);
}

void setLedColor(uint8_t red, uint8_t green, uint8_t blue) {
  currentColor = CRGB(red, green, blue);
  fill_solid(leds, NUM_LEDS, currentColor);
  FastLED.show();
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

void applyLedState() {
  if (ENABLE_LDR_CONTROL && isLight) {
    FastLED.clear();
    FastLED.show();
    setStripPower(false);
    return;
  }

  setStripPower(true);
  setLedColor(currentColor.r, currentColor.g, currentColor.b);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (length >= 32) {
    return;
  }

  char message[32];
  memcpy(message, payload, length);
  message[length] = '\0';

  if(strcmp(topic, mqttConfig.brightnessTopic.c_str()) == 0) {
    int value = atoi(message);
    if (value > MAX_BRIGHTNESS) {
      value = MAX_BRIGHTNESS;
    }
    if (value < 0) {
      value = 0;
    }
    currentBrightness = value;
    FastLED.setBrightness(currentBrightness);
    FastLED.show();
    Serial.printf("MQTT brightness: %d\n", currentBrightness);
    return;
  }

  if (strcmp (topic, mqttConfig.colorTopic.c_str()) != 0) {
      return;
  }

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
  if (!ENABLE_LDR_CONTROL || !isLight) {
    setLedColor(red, green, blue);
  } else {
    currentColor = CRGB(red, green, blue);
  }
}

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
  if (mqttConfig.username.isEmpty()) {
    connected = mqttClient.connect(mqttConfig.clientId.c_str());
  } else {
    connected = mqttClient.connect(mqttConfig.clientId.c_str(), mqttConfig.username.c_str(), mqttConfig.password.c_str());
  }

  if (connected) {
    Serial.printf("Connected to MQTT; subscribing to %s\n", 
                  mqttConfig.colorTopic.c_str(),
                  mqttConfig.brightnessTopic.c_str());
    mqttClient.subscribe(mqttConfig.colorTopic.c_str());
    mqttClient.subscribe(mqttConfig.brightnessTopic.c_str());
  } else {
    Serial.printf("MQTT connection failed, state %d\n", mqttClient.state());
  }
}

void updateLdrState() {
  if (!ENABLE_LDR_CONTROL || millis() - lastLdrRead < LDR_POLL_INTERVAL_MS) {
    return;
  }

  lastLdrRead = millis();
  int ldrValue = readAverageLdr();
  bool previousState = isLight;

  if (ldrValue > BRIGHT_THRESHOLD) {
    isLight = true;
  } else if (ldrValue < DARK_THRESHOLD) {
    isLight = false;
  }

  if (isLight != previousState) {
    applyLedState();
  }
}

bool connectWiFi(const String& ssid, const String& password) {
  Serial.printf("Connecting to Wi-Fi SSID: %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
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

bool loadWifiCredentials(String& ssid, String& password) {
  Serial.println("Checking Wi-Fi credentials in NVS.");
  Preferences preferences;
  if (!preferences.begin("wifi", true)) {
    Serial.println("Could not open Wi-Fi NVS namespace.");
    return false;
  }

  ssid = preferences.getString("ssid", "").c_str();
  password = preferences.getString("pass", "").c_str();
  preferences.end();

  if (ssid.isEmpty()) {
    Serial.println("Wi-Fi credentials not found in NVS.");
    return false;
  }

  Serial.printf("Wi-Fi credentials found in NVS for SSID: %s.\n", ssid.c_str());
  return true;
}

bool clearWifiCredentials() {
  Preferences preferences;
  if (!preferences.begin("wifi", false)) {
    Serial.println("Could not open Wi-Fi NVS namespace for deletion.");
    return false;
  }

  preferences.remove("ssid");
  preferences.remove("pass");
  bool cleared = !preferences.isKey("ssid") && !preferences.isKey("pass");
  preferences.end();
  return cleared;
}

void requestWifiCredentialChange() {
  Serial.println("Deleting saved Wi-Fi credentials.");
  if (!clearWifiCredentials()) {
    Serial.println("Could not delete saved Wi-Fi credentials; keeping current connection.");
    return;
  }

  Serial.println("Wi-Fi credentials deleted; restarting into setup AP.");
  delay(500);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  loadMqttConfig();
  if (USE_STRIP_POWER_SWITCH) {
    pinMode(SWITCH_PIN, OUTPUT);
    setStripPower(false);
  }
  if (ENABLE_LDR_CONTROL) {
    configureLdrAdc();
  }

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(currentBrightness);

  setStripPower(true);
  setLedColor(20, 20, 20);
  delay(STARTUP_TEST_MS);
  currentColor = CRGB(255, 200, 100);
  if (ENABLE_LDR_CONTROL) {
    int ldrValue = readAverageLdr();
    if (ldrValue > BRIGHT_THRESHOLD) {
      isLight = true;
    } else if (ldrValue < DARK_THRESHOLD) {
      isLight = false;
    }
  }
  applyLedState();

  mqttClient.setServer(mqttConfig.serverHost.c_str(), mqttConfig.serverPort);
  mqttClient.setCallback(onMqttMessage);
  wifiClient.setCACert(mqttRootCa);

  String wifiSsid;
  String wifiPassword;
  bool hasSavedCredentials = loadWifiCredentials(wifiSsid, wifiPassword);
  if (!hasSavedCredentials) {
    Serial.println("No saved Wi-Fi credentials; starting setup AP.");
    startPortal();
  }

  Serial.printf("Connecting to Wi-Fi network: %s\n", wifiSsid.c_str());
  if (!connectWiFi(wifiSsid, wifiPassword)) {
    Serial.println("Saved Wi-Fi connection failed; starting setup AP.");
    startPortal();
  }
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
  delay(2);
}
