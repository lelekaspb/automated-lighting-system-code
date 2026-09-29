#include <FastLED.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mqtt_root_ca.h"

#if __has_include("mqtt_config.h")
#include "mqtt_config.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
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
bool isLight = false;
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);
uint32_t lastWifiAttempt = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastLdrRead = 0;
bool timeSyncStarted = false;

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
  if (strcmp(topic, MQTT_COLOR_TOPIC) != 0 || length >= 32) {
    return;
  }

  char message[32];
  memcpy(message, payload, length);
  message[length] = '\0';

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

void connectWifiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED || WIFI_SSID[0] == '\0') {
    return;
  }

  uint32_t now = millis();
  if (lastWifiAttempt != 0 && now - lastWifiAttempt < 10000) {
    return;
  }

  lastWifiAttempt = now;
  Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectMqttIfNeeded() {
  if (mqttClient.connected() || WiFi.status() != WL_CONNECTED || MQTT_SERVER_HOST[0] == '\0') {
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
  if (MQTT_USERNAME[0] == '\0') {
    connected = mqttClient.connect(MQTT_CLIENT_ID);
  } else {
    connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
  }

  if (connected) {
    Serial.printf("Connected to MQTT; subscribing to %s\n", MQTT_COLOR_TOPIC);
    mqttClient.subscribe(MQTT_COLOR_TOPIC);
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

void setup() {
  Serial.begin(115200);
  if (USE_STRIP_POWER_SWITCH) {
    pinMode(SWITCH_PIN, OUTPUT);
    setStripPower(false);
  }
  if (ENABLE_LDR_CONTROL) {
    configureLdrAdc();
  }

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);

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

  mqttClient.setServer(MQTT_SERVER_HOST, MQTT_SERVER_PORT);
  mqttClient.setCallback(onMqttMessage);
  wifiClient.setCACert(mqttRootCa);
  WiFi.mode(WIFI_STA);
  connectWifiIfNeeded();
}

void loop() {
  connectWifiIfNeeded();
  if (WiFi.status() == WL_CONNECTED && !timeSyncStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    timeSyncStarted = true;
  }
  connectMqttIfNeeded();
  mqttClient.loop();
  updateLdrState();
  delay(2);
}
