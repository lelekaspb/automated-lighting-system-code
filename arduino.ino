#include <FastLED.h>
#include "esp_sleep.h"

#define NUM_LEDS 137
#define DATA_PIN 5
#define LDR_PIN 34
#define SWITCH_PIN 26

// MJE3055T low-side switch is wired as active-HIGH:
// GPIO HIGH -> transistor ON -> NeoPixel GND connected
// GPIO LOW  -> transistor OFF -> NeoPixel GND disconnected
#define SWITCH_ACTIVE_HIGH true
#define STARTUP_TEST_MS 1500


// enable when MOSFET is installed
// #define MOSFET_PIN 26  // or any free GPIO

// If the averaged LDR reading rises above this value, we treat the room as bright
// and turn the LEDs off.
#define BRIGHT_THRESHOLD 320

// If the averaged LDR reading falls below this value, we treat the room as dark
// and turn the LEDs on.
#define DARK_THRESHOLD   220

#define SAMPLE_COUNT     12
#define SAMPLE_DELAY_MS  5

#define BRIGHT_SLEEP_SECONDS 300
#define DARK_SLEEP_SECONDS 30

CRGB leds[NUM_LEDS];
RTC_DATA_ATTR bool isLight = false;

void configureLdrAdc() {
  // Allow up to ~3.3V on the ADC input before clipping.
  analogSetPinAttenuation(LDR_PIN, ADC_11db);
}

void setStripPower(bool on) {
  bool level = SWITCH_ACTIVE_HIGH ? on : !on;
  digitalWrite(SWITCH_PIN, level ? HIGH : LOW);
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
  if (!isLight) {
    setStripPower(true);
    delay(10);
    fill_solid(leds, NUM_LEDS, CRGB(255, 200, 100));
    FastLED.show();
  } else {
    FastLED.clear();
    FastLED.show();
    delay(10);
    setStripPower(false);
  }
}

void sleepForSeconds(uint32_t seconds) {
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  delay(50);
  esp_deep_sleep_start();
}

void setup() {
  pinMode(SWITCH_PIN, OUTPUT);
  setStripPower(false); // LEDs off by default
  configureLdrAdc();

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);

  // Startup self-test: power strip and show a brief color so hardware faults
  // are obvious before sensor logic runs.
  setStripPower(true);
  fill_solid(leds, NUM_LEDS, CRGB(20, 20, 20));
  FastLED.show();
  delay(STARTUP_TEST_MS);
  FastLED.clear();
  FastLED.show();

  int ldrValue = readAverageLdr();
  // Hysteresis:
  // - above BRIGHT_THRESHOLD -> switch to bright state
  // - below DARK_THRESHOLD   -> switch to dark state
  // - between thresholds     -> keep previous state
  //
  // This gap between thresholds prevents rapid back-and-forth switching
  // when the reading hovers near one boundary.
  if (ldrValue > BRIGHT_THRESHOLD) {
    isLight = true;
  } else if (ldrValue < DARK_THRESHOLD) {
    isLight = false;
  } else {
    // Keep the previous state when the reading sits in the hysteresis band.
  }

  applyLedState();

  if (isLight) {
    sleepForSeconds(BRIGHT_SLEEP_SECONDS);
  }
  sleepForSeconds(DARK_SLEEP_SECONDS);


}

void loop() {
  // Intentionally unused. The sketch reboots into setup() after each wake.
}
