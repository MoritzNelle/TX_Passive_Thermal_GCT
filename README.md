# TX Passive Thermal GCT

Firmware for the master station of a set of passive thermal ground control targets (GCTs). The targets are placed on the ground in the flight area and are used as temperature references when thermal drone images are calibrated. The master is an ESP32 with a 20x4 LCD, a real-time clock, an SD card, a status LED and a button. It talks to four targets over ESP-NOW and shows their connection state and average temperature on the LCD. While logging is on, it asks all targets for their temperatures at a fixed interval and writes them to its SD card.

The target firmware is in [RX_Passive_Thermal_GCT](https://github.com/MoritzNelle/RX_Passive_Thermal_GCT). In the code the targets are also called servants ("servent").

## Why

The usual way to calibrate thermal drone images is with aluminium sheets on the ground and a handheld thermal gun. That method is not very accurate, and the temperature of the references drifts during a flight. These targets measure and timestamp their own temperature for as long as the master is logging, so any drift during the flight is recorded.

## Hardware

What the code and `platformio.ini` use:

- ESP32 board (PlatformIO board `nodemcu-32s`)
- 20x4 character LCD on I2C, address 0x27
- DS3231 real-time clock on I2C
- SD card module on SPI
- One NeoPixel RGB LED for status
- One push button to start and stop logging

## Pins

| Function | GPIO |
|---|---|
| SD card CS | 5 |
| NeoPixel status LED | 4 |
| Logging button | 0 |

The button pin is set to `INPUT` without the internal pull-up and reads LOW when pressed. The SD card (SPI) and the LCD and RTC (I2C) use the ESP32 default pins, which the code does not set: SCK 18, MISO 19, MOSI 23, SDA 21, SCL 22.

## How it works

At start-up the master sets up the LCD, checks ESP-NOW, the four target peers, the SD card and the RTC, and prints the result on the serial monitor. Then it runs this loop:

- Every `pingCheckIntervall` it sends a connection test (1001) to each target and shows a tick or an `x` per target on the LCD. In the same step it tells all targets whether logging is on (1002) or off (1003).
- While logging is off, it asks for temperatures (3001) every `tempUpdateIntervall` and only shows them.
- A button press switches logging on or off. The first reading is taken right after logging starts.
- While logging is on, it asks for temperatures every `logIntervall`, writes them to the SD card and counts down to the next reading.
- If no target answers, the LCD shows "ERROR: No connection", the LED blinks red and logging is switched off. The master stays in this state until a target answers again.

| Action ID | Direction | Meaning |
|---|---|---|
| 1001 | Master to target | Connection test |
| 1002 | Master to target | Logging is on. The target also saves every reading to its own SD card. |
| 1003 | Master to target | Logging is off |
| 3001 | Master to target | Request all temperatures |
| 2001 | Target to master | Answer with all nine temperatures |

The master sends `struct_message { int actionID; float value; }`. The targets only read `actionID`. The connection test sends the `int` alone. A target answers with one packet of this struct, which has to be the same in both firmwares:

```cpp
typedef struct temp {
    int actionID;   // 2001
    float sens1;
    float sens2;
    // ...
    float sens9;
} temp;
```

### LCD

| Row | Content |
|---|---|
| 1 | Date and time from the RTC |
| 2 | Connection of targets S1 to S4 (tick or `x`) |
| 3 | Average of the nine readings per target, or `-` when the target is not connected |
| 4 | Status: `Idle (ready to log)`, `Logging: N s`, `Retrieving Data...`, `Updating Temperature` or an error |

## Build and flash

This is a PlatformIO project with the Arduino framework. On the first build PlatformIO downloads the ESP32 platform and these libraries:

- RTClib and Adafruit BusIO (Adafruit)
- LiquidCrystal_I2C (marcoschwartz)
- Adafruit NeoPixel
- SPI and Wire

In VS Code with the PlatformIO extension, open the folder and use Build and Upload. On the command line:

```sh
pio run                        # build
pio run -t upload              # build and flash
pio device monitor -b 115200   # serial monitor
```

The only environment is `az-delivery-devkit-v4`. `platformio.ini` does not set a monitor speed, so pass `-b 115200` to read the self check and the send log on the serial monitor.

If your computer does not find the board, install the CP210x USB to UART driver from `board_driver/` (Windows and macOS). Linux already includes it.

## Settings

The timing settings are at the top of `src/main.cpp`. All values are in milliseconds.

| Variable | Default | What it does |
|---|---|---|
| `sendTimeout` | `1000` | How long the master waits for a target's answer |
| `logIntervall` | `10000` | Time between two logged readings. The code comment asks for 10000 (10 s) or more. |
| `pingCheckIntervall` | `1000` | Time between connection tests. Use the same value as `pingInterval` on the targets. |
| `tempUpdateIntervall` | `10000` | Time between display updates while logging is off |

Further down in the same file:

- `broadcastAddresses` holds the MAC addresses of the four targets. The order sets the target number: the first entry is S1 on the LCD and target 1 in the log. Give each target the matching `GCTID` in its own firmware.
- The master's own MAC address goes into `masterAdress` in each target's firmware. esptool prints a board's MAC address near the start of every upload, so you can copy it from the upload output.
- `LiquidCrystal_I2C lcd(0x27, 20, 4)` sets the LCD address. Change it if your display uses a different one.
- The log file name is set in `setup()` (`/data_master.csv`).

The master is written for exactly four targets. The number 4 is fixed in the send loops, the peer list and the LCD layout.

### Setting the clock

The log uses the time from the DS3231. To set it to your computer's time, uncomment this line in `setup()` and flash:

```cpp
//rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
```

Then comment it out again and flash once more. If you leave it in, the clock goes back to the compile time at every restart.

## Status LED

| LED | Meaning |
|---|---|
| Yellow, steady (at start-up) | Self check running |
| Off | A temperature request is running |
| Green, blinking | Idle, all four targets connected |
| Yellow, blinking | Idle, one to three targets connected |
| Green, steady | Logging, all four targets connected |
| Yellow, steady (while logging) | Logging, one to three targets connected |
| Red, blinking | No target connected, or an SD card error |
| Red, steady | ESP-NOW, peer or RTC error at start-up |

## Error numbers on the LCD

| Nr. | Meaning |
|---|---|
| 2 | No SD card found, or the log file can't be opened |
| 4 | SD card mount failed |
| 5 | A target could not be added as ESP-NOW peer |
| 6 | ESP-NOW could not start |

An RTC failure shows no number on the LCD. The LED turns red and the master stops.

## Log file

The master appends to `/data_master.csv` on its SD card. Each logged reading adds nine lines per connected target, one per sensor. The file has no header row.

```
timestamp,target,sensor,temperature
```

| Column | Content |
|---|---|
| `timestamp` | Time from the master's RTC, `YYYY-MM-DD HH:MM:SS` |
| `target` | 1 to 4, the target's position in `broadcastAddresses` |
| `sensor` | 1 to 9 |
| `temperature` | °C with two decimals. -127.00 means the sensor did not answer (the DallasTemperature value for a disconnected sensor). |

A target that fails the connection test gets a single line with sensor `123456789` and temperature `NAN`. Example lines (made-up values):

```
2024-06-12 10:15:00,1,1,24.31
2024-06-12 10:15:00,1,2,24.25
2024-06-12 10:15:00,3,123456789,NAN
```

The targets write the same readings to their own SD cards, with their own RTC time.

## Known issues

Open TODOs from the top of `src/main.cpp`:

- The logging countdown sometimes jumps to about 42947XXX seconds.
- A button press is not always detected.
- When the connection is lost, the display still shows the last temperature values.
- Old digits are not always cleared on the display, for example when a value gets shorter.
- The display and the LED freeze while temperatures are requested ("Updating Temperature").
- For a moment, the LED and the error message sometimes show "No connection" while the connection is fine.
