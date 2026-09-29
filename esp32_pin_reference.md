# ESP32 D1 mini style board: board info and pin reference

## Board info

| | |
|---|---|
| Board | Generic ESP32 "D1 mini" style dev board (Wemos/LOLIN D1 mini ESP32 form factor) |
| USB | USB-C, with a separate USB-to-serial chip and a reset button |
| Chip | ESP32-D0WD-V3, revision 301 (v3.1) |
| Cores | 2 (Xtensa LX6) |
| Flash | 4 MB (4194304 bytes) |
| Wireless | Wi-Fi 802.11 b/g/n, Bluetooth |
| Module | Unbranded; shield reads `ESP-32`, same size and pin layout as ESP32-WROOM-32 |
| GPIO logic level | 3.3 V |

## Pin layout
 
Viewed from the component side (the side with the ESP-32 shield), antenna on the left, USB-C on the right. There are two header rows along each long edge: an **outer** row (at the board edge) and an **inner** row (next to the module). Columns are numbered 1 to 10 from the antenna end.
 
```
                        1    2    3    4    5    6    7    8    9    10
                       (antenna end)                         (USB-C end)
 
  top edge   outer     TXD  RXD  IO22 IO21 IO17 IO16 GND  VCC  TDO  SDD
             inner     GND  IO27 IO25 IO32 TDI  IO4  IO0  IO2  SD1  CLK
 
        ┌──────────────────────────────────────────────────────────────┐
        │  antenna    ┌────────────────────────┐                [USB-C] │
        │             │   ESP-32 module        │                        │
        │             └────────────────────────┘             [button]   │
        └──────────────────────────────────────────────────────────────┘
 
             inner     GND  NC   SVN  IO35 IO33 IO34 TMS  NC   SD2  CMD
  bottom edge outer    RST  SVP  IO26 IO18 IO19 IO23 IO5  3.3V TCK  SD3
```
 
The two **outer** rows follow the standard Wemos D1 mini ESP32 pinout (top edge: TXD to VCC, bottom edge: RST to 3.3V). The two **inner** rows carry the additional pins.
 
| Row | Left to right (columns 1 to 10) |
|---|---|
| Top edge, outer | `TXD` `RXD` `IO22` `IO21` `IO17` `IO16` `GND` `VCC` `TDO` `SDD` |
| Top edge, inner | `GND` `IO27` `IO25` `IO32` `TDI` `IO4` `IO0` `IO2` `SD1` `CLK` |
| Bottom edge, inner | `GND` `NC` `SVN` `IO35` `IO33` `IO34` `TMS` `NC` `SD2` `CMD` |
| Bottom edge, outer | `RST` `SVP` `IO26` `IO18` `IO19` `IO23` `IO5` `3.3V` `TCK` `SD3` |

## Power and control pins

| Label | Function |
|---|---|
| `GND` | Ground (several, all connected) |
| `VCC` | Supply rail from USB-C (nominally 5 V) |
| `3.3V` | 3.3 V output from the on-board regulator |
| `RST` | Reset (chip `EN`), pull to GND to reset |
| `NC` | Not connected |

## GPIO pins

| Label | GPIO | Function / notes |
|---|---|---|
| `IO0` | 0 | Strapping pin (boot mode), ADC2, touch |
| `IO2` | 2 | Strapping pin, ADC2, touch |
| `IO4` | 4 | General purpose, ADC2, touch |
| `IO5` | 5 | Strapping pin, VSPI CS |
| `IO16` | 16 | General purpose, UART2 RX |
| `IO17` | 17 | General purpose, UART2 TX |
| `IO18` | 18 | VSPI SCK |
| `IO19` | 19 | VSPI MISO |
| `IO21` | 21 | I2C SDA (Arduino default) |
| `IO22` | 22 | I2C SCL (Arduino default) |
| `IO23` | 23 | VSPI MOSI |
| `IO25` | 25 | DAC1, ADC2 |
| `IO26` | 26 | DAC2, ADC2 |
| `IO27` | 27 | General purpose, ADC2, touch |
| `IO32` | 32 | ADC1, touch |
| `IO33` | 33 | ADC1, touch |
| `IO34` | 34 | ADC1, **input only** |
| `IO35` | 35 | ADC1, **input only** |
| `SVP` | 36 | `SENSOR_VP`, ADC1, **input only** |
| `SVN` | 39 | `SENSOR_VN`, ADC1, **input only** |
| `TXD` | 1 | UART0 TX (USB serial) |
| `RXD` | 3 | UART0 RX (USB serial) |

## JTAG pins (also usable as GPIO)

| Label | GPIO | Notes |
|---|---|---|
| `TDI` | 12 | `MTDI`, strapping pin (flash voltage), ADC2, touch |
| `TCK` | 13 | `MTCK`, ADC2, touch |
| `TMS` | 14 | `MTMS`, ADC2, touch |
| `TDO` | 15 | `MTDO`, strapping pin (boot log), ADC2, touch |

## Flash pins (connected to on-board flash, not usable as GPIO)

| Label | GPIO |
|---|---|
| `CLK` | 6 |
| `SD0` (`SDD`) | 7 |
| `SD1` | 8 |
| `SD2` | 9 |
| `SD3` | 10 |
| `CMD` | 11 |

## Documentation

- ESP32 datasheet: https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
- ESP32-WROOM-32 datasheet (pin definitions, strapping pins): https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf
- ESP32 technical reference manual (IO_MUX and GPIO matrix): https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf
- ESP-IDF GPIO reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html
- ESP-IDF ADC reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc/index.html
- Arduino-ESP32 docs: https://docs.espressif.com/projects/arduino-esp32/en/latest/
- Wemos/LOLIN docs (original D1 mini ESP32): https://www.wemos.cc/en/latest/
