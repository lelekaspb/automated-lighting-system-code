# ESP32 NeoPixel Ambient Light Switch

An ambient light-reactive LED strip controller built with an ESP32, a WS2812B LED strip, and an LDR (photoresistor) voltage divider. The ESP32 measures room brightness, turns the LEDs on in dark conditions and off in bright conditions, and accepts color, brightness and LDR on/off commands over MQTT.

## What the Sketch Does

- Reads ambient light through GPIO34 using the ESP32 ADC (Analog-to-Digital Converter), once per second, and prints `LDR: <value>` to the Serial Monitor.
- Averages 12 sensor samples to reduce noise.
- Uses separate bright and dark thresholds (hysteresis) to avoid rapid flickering around the light boundary.
- Drives 137 WS2812B LEDs through GPIO5 using FastLED.
- Runs a short startup LED test before reading the sensor.
- Connects to Wi-Fi and an MQTT broker (TLS) to receive commands.
- The automatic lighting can be switched on and off at runtime (`setLdrEnabled()`, or the `esp32/ldr/enabled` topic).

Current behavior:

- Dark detected (LDR enabled): strip is on with the current color and brightness; the default color is warm white `CRGB(255, 200, 100)`.
- Bright detected (LDR enabled): strip turns off. The chosen color and brightness are kept.
- LDR disabled: the strip stays off regardless of ambient light (for example while you are away).
- Startup: strip briefly displays a dim white test color for 1.5 seconds.
- Brightness defaults to `DEFAULT_BRIGHTNESS` (`50`) and can be changed over MQTT.

## MQTT Topics

The topics are defined in the `MQTT TOPICS` section of [arduino.ino](arduino.ino). Payloads are plain text and must be shorter than 32 characters.

| Topic | Payload | Effect |
| --- | --- | --- |
| `esp32/led/color` (default; configurable in NVS key `color_topic`) | `R,G,B`, each 0-255, e.g. `255,200,100` | Sets the strip color |
| `esp32/led/brightness` | `0`-`255` (`255` = 100%) | Sets the strip brightness |
| `esp32/ldr/enabled` | `1`/`0`, `true`/`false`, `on`/`off` | `1`: automatic lighting (on in the dark, off in the light). `0`: strip stays off |

Invalid payloads are rejected and reported in the Serial Monitor. Settings are not persisted across reboots unless the message is published with the retain flag. The broker host, port, credentials, client ID and color topic are read from NVS (namespace `mqtt`) or from an optional `mqtt_config.h`; the broker's root certificate is in `mqtt_root_ca.h`.

To add a topic, write a handler and add one row to `MQTT_TOPICS[]`; subscribing and dispatching are automatic.

## Wi-Fi Setup

On startup, the ESP32 scans for networks and skips saved networks that are not in range. It tries each visible saved network before starting its
`ESP32-Setup` access point. If none connect (or no networks have been saved),
the setup page is available at `http://192.168.4.1`. Enter the network details
and choose **Save this network for later use** to retain them. Leaving the box
unchecked connects for the current session only. Up to 10 networks can be
saved; adding an already-saved SSID updates its password.

Send `W` in the Serial Monitor to open the setup portal again without deleting
saved networks. Credentials from older versions of the sketch are retained and
tried as a saved network.

## Hardware
See [ESP32 pin reference](esp32_pin_reference.md) for board details and GPIO notes.

| Component | Details |
| --- | --- |
| Microcontroller | ESP32 development board |
| LED strip | WS2812B / NeoPixel strip, 137 LEDs |
| Light sensor | LDR (photoresistor) |
| Divider resistor | 2.2k ohm pull-down resistor |
| Solar panel | 12V panel, charges the batteries |
| Charge controller | CN3791 MPPT solar charger module for single-cell Li-ion (4.2V) |
| Batteries | 2x 18650 Li-ion cells, 3.7V 3500 mAh each, connected in parallel |
| Buck converter | Unmarked step-down module, regulates battery voltage to 5V for the LED strip |

## Power Chain

> [!IMPORTANT]
> **TO DISCUSS WITH GROUP:** how to power the ESP32 (5V rail vs. batteries), and the wiring notes below.

```text
12V solar panel -> CN3791 charger -> 2x 18650 (parallel) -> buck converter (5V) -> WS2812B strip
```

The ESP32 should be powered from the same regulated 5V output as the strip (its `VCC`/`VIN` pin), not directly from the batteries. See the wiring notes below.

### LDR Voltage Divider

```text
3.3V -> LDR -> (junction -> GPIO34) -> 2.2k ohm resistor -> GND
```

> [!WARNING]
> **TO DISCUSS WITH GROUP:** the wiring notes below.

Important wiring notes:

- The strip is turned off in software by clearing the LEDs; there is no hardware power switching.
- Power the ESP32 `VCC`/`VIN` pin from the same regulated 5V rail as the strip, not directly from 3.7V batteries, which cause brownouts. **Never connect USB and an external 5V supply at the same time.**
- At full brightness the strip's light can reach the LDR and cause on/off flicker; shield the LDR from the strip.
- The ESP32 and the 5V LED power supply still need a common system ground reference.
- GPIO34 is input-only, which is suitable for analog sensing.
- WS2812B strips can draw substantial current. Use a 5V supply that is rated for the strip's expected load; do not power 137 LEDs from the ESP32 board's 5V/USB pin.

## Pin Definitions in Code

The pin configuration is at the top of [arduino.ino](arduino.ino):

```cpp
#define NUM_LEDS 137
#define DATA_PIN 5
#define LDR_PIN 34
```

| Code symbol | Value | Meaning |
| --- | --- | --- |
| `NUM_LEDS` | `137` | Number of LEDs on the strip |
| `DATA_PIN` | `5` | WS2812B data output pin |
| `LDR_PIN` | `34` | ADC input for the LDR divider |
| `ENABLE_LDR_CONTROL` | `true` | Initial state of the runtime switch (`false` = strip starts off) |
| `DEFAULT_BRIGHTNESS` | `50` | Initial strip brightness (0-255) |

## Tuning Parameters

The current sketch uses these settings:

```cpp
#define BRIGHT_THRESHOLD 320
#define DARK_THRESHOLD   220

#define SAMPLE_COUNT     12
#define SAMPLE_DELAY_MS  5

#define LDR_POLL_INTERVAL_MS 1000
```

`BRIGHT_THRESHOLD` and `DARK_THRESHOLD` create a hysteresis range. A reading above `320` marks the environment as bright, a reading below `220` marks it as dark, and readings between them keep the previous state.

To tune the controller for a different room, watch the `LDR: <value>` lines in the Serial Monitor in bright and dark conditions, then set the dark threshold below typical dim readings and the bright threshold above typical bright readings. Keep a gap between them.

The sensor is polled every `LDR_POLL_INTERVAL_MS` (1 second). The sketch does not use deep sleep.

## Arduino IDE Setup and Upload

1. Install the latest [Arduino IDE](https://www.arduino.cc/en/software).
2. In Arduino IDE, open **File > Preferences** and add this URL to **Additional Boards Manager URLs**:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Open **Tools > Board > Boards Manager**, search for `esp32`, and install **esp32 by Espressif Systems**.
4. Open **Tools > Manage Libraries** and install **FastLED** and **PubSubClient** (see Dependencies below).
5. Open [arduino.ino](arduino.ino) in Arduino IDE. The IDE may offer to place it in a folder named `arduino`; accept that prompt if shown.
6. Connect the ESP32 board with a data-capable USB cable.
7. Select the board under **Tools > Board**. For many generic boards, select **ESP32 Dev Module**.
8. Select the connected serial port under **Tools > Port**.
9. Click **Verify** to compile, then click **Upload** to flash the sketch.

If uploading stalls at `Connecting...`, hold the board's **BOOT** button while upload begins, then release it once the upload starts.

## Dependencies

Install through the Arduino IDE Library Manager:

- [FastLED](https://fastled.io/) - drives the WS2812B strip.
- [PubSubClient](https://github.com/knolleary/pubsubclient) by Nick O'Leary - MQTT client.

Included with the **esp32 by Espressif Systems** board package (no separate install): `WiFi`, `WiFiClientSecure`, `WebServer` (setup portal) and `Preferences` (saved Wi-Fi/MQTT settings in NVS).

Sketch files in this folder:

- [mqtt_root_ca.h](mqtt_root_ca.h) - root certificate used to verify the MQTT broker's TLS connection.
- [mqtt_config.h](mqtt_config.h) - optional broker settings; copy [mqtt_config.example.h](mqtt_config.example.h) to create it.

## Glossary

- **ADC:** Analog-to-Digital Converter. Converts the voltage at GPIO34 into a numeric light-level reading.
- **LDR:** Light-Dependent Resistor. A resistor whose value changes with ambient light.
- **Hysteresis:** Separate on/off thresholds that prevent rapid toggling near a single boundary.
- **MQTT:** A lightweight publish/subscribe messaging protocol used to send commands to the ESP32.
- **Brownout:** A drop in supply voltage below what the ESP32 needs to run reliably, often caused by a weak supply or sudden current spikes (such as Wi-Fi startup). The chip resets repeatedly, and the LEDs may blink as the sketch restarts.