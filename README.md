# ESP32 NeoPixel Ambient Light Switch

An ambient light-reactive LED strip controller built with an ESP32, a WS2812B LED strip, and an LDR (photoresistor) voltage divider. The ESP32 measures room brightness, turns the LEDs on in dark conditions, turns them off in bright conditions, and sleeps between measurements to reduce power use.

## What the Sketch Does

- Reads ambient light through GPIO34 using the ESP32 ADC (Analog-to-Digital Converter).
- Averages 12 sensor samples to reduce noise.
- Uses separate bright and dark thresholds (hysteresis) to avoid rapid flickering around the light boundary.
- Drives 137 WS2812B LEDs through GPIO5 using FastLED.
- Switches LED strip ground power through an active-HIGH low-side transistor on GPIO26.
- Runs a short startup LED test before reading the sensor.
- Enters deep sleep after each measurement, waking periodically to check the light level again.

Current behavior:

- Dark detected: strip turns on with warm white `CRGB(255, 200, 100)`.
- Bright detected: strip turns off.
- Startup: strip briefly displays a dim white test color for 1.5 seconds.
- LED brightness is fixed at `50` by `FastLED.setBrightness(50)`; it is not automatically adjusted.

## Hardware
See [ESP32 pin reference](esp32_pin_reference.md) for board details and GPIO notes.

| Component | Details |
| --- | --- |
| Microcontroller | ESP32 development board |
| LED strip | WS2812B / NeoPixel strip, 137 LEDs |
| Light sensor | LDR (photoresistor) |
| Divider resistor | 2.2k ohm pull-down resistor |
| Strip power switch | MJE3055T low-side transistor, active-HIGH control |
| LED power supply | Regulated 5V supply sized for the LED strip |

## Circuit Connections

| ESP32 pin / supply | Connects to | Purpose |
| --- | --- | --- |
| GPIO5 | WS2812B `DIN` | LED data signal |
| GPIO26 | Transistor control input | Controls the switched LED-strip ground path |
| GPIO34 | LDR/resistor junction | Analog ambient-light input |
| 3.3V | LDR top leg | Powers the sensor voltage divider |
| GND | 2.2k ohm resistor bottom leg | Divider reference ground |
| 5V supply | WS2812B `5V` | LED strip power |
| WS2812B `GND` | Transistor switched output | Switched low-side return for the strip |
| ESP32 GND | Power supply ground and transistor return | Common system ground reference |

### LDR Voltage Divider

```text
3.3V -> LDR -> (junction -> GPIO34) -> 2.2k ohm resistor -> GND
```

### LED Power Switching

```text
GPIO26 HIGH -> transistor ON  -> WS2812B GND connected -> strip powered
GPIO26 LOW  -> transistor OFF -> WS2812B GND disconnected -> strip unpowered
```

Important wiring notes:

- The LED strip ground is deliberately switched through the transistor. Connecting the strip ground directly to ESP32 ground bypasses the power switch.
- The ESP32 and the 5V LED power supply still need a common system ground reference.
- GPIO34 is input-only, which is suitable for analog sensing.
- WS2812B strips can draw substantial current. Use a 5V supply that is rated for the strip's expected load; do not power 137 LEDs from the ESP32 board's 5V/USB pin.

## Pin Definitions in Code

The pin configuration is at the top of [arduino.ino](arduino.ino):

```cpp
#define NUM_LEDS 137
#define DATA_PIN 5
#define LDR_PIN 34
#define SWITCH_PIN 26
```

| Code symbol | Value | Meaning |
| --- | --- | --- |
| `NUM_LEDS` | `137` | Number of LEDs on the strip |
| `DATA_PIN` | `5` | WS2812B data output pin |
| `LDR_PIN` | `34` | ADC input for the LDR divider |
| `SWITCH_PIN` | `26` | Low-side transistor control pin |
| `SWITCH_ACTIVE_HIGH` | `true` | HIGH powers the strip; LOW disconnects its ground |

## Tuning Parameters

The current sketch uses these settings:

```cpp
#define BRIGHT_THRESHOLD 320
#define DARK_THRESHOLD   220

#define SAMPLE_COUNT     12
#define SAMPLE_DELAY_MS  5

#define BRIGHT_SLEEP_SECONDS 300
#define DARK_SLEEP_SECONDS 30
```

`BRIGHT_THRESHOLD` and `DARK_THRESHOLD` create a hysteresis range. A reading above `320` marks the environment as bright, a reading below `220` marks it as dark, and readings between them keep the previous state stored through deep sleep.

To tune the controller for a different room, temporarily add `Serial` logging around `ldrValue`, record the values in bright and dark conditions, then set the dark threshold below typical dim readings and the bright threshold above typical bright readings. Keep a gap between them.

The sketch waits 300 seconds after a bright reading and 30 seconds after a dark reading before checking again. Increase either sleep duration to reduce wake frequency and power use.

## Arduino IDE Setup and Upload

1. Install the latest [Arduino IDE](https://www.arduino.cc/en/software).
2. In Arduino IDE, open **File > Preferences** and add this URL to **Additional Boards Manager URLs**:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Open **Tools > Board > Boards Manager**, search for `esp32`, and install **esp32 by Espressif Systems**.
4. Open **Tools > Manage Libraries**, search for `FastLED`, and install the **FastLED** library.
5. Open [arduino.ino](arduino.ino) in Arduino IDE. The IDE may offer to place it in a folder named `arduino`; accept that prompt if shown.
6. Connect the ESP32 board with a data-capable USB cable.
7. Select the board under **Tools > Board**. For many generic boards, select **ESP32 Dev Module**.
8. Select the connected serial port under **Tools > Port**.
9. Click **Verify** to compile, then click **Upload** to flash the sketch.

If uploading stalls at `Connecting...`, hold the board's **BOOT** button while upload begins, then release it once the upload starts.

## Dependency

- [FastLED](https://fastled.io/) - install through Arduino IDE Library Manager.
- [PubSubClient](https://github.com/knolleary/pubsubclient) by Nick O'Leary - install through Arduino IDE Library Manager for MQTT support.

## Glossary

- **ADC:** Analog-to-Digital Converter. Converts the voltage at GPIO34 into a numeric light-level reading.
- **LDR:** Light-Dependent Resistor. A resistor whose value changes with ambient light.
- **Hysteresis:** Separate on/off thresholds that prevent rapid toggling near a single boundary.
- **Deep sleep:** A low-power ESP32 mode. The device restarts at `setup()` after its timer wake-up.