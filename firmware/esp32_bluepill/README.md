# ESP32 + STM32 Blue Pill Voice Note Firmware

This folder contains a standalone pair of PlatformIO projects for a two-board
voice recorder and display:

- **ESP32 (`esp32/`)** connects to Wi-Fi, polls the FastAPI server, captures the
  INMP441 microphone on I2S0, and streams a 16 kHz mono 16-bit WAV to
  `POST /api/note` with HTTP chunked transfer encoding.
- **STM32 Blue Pill (`bluepill/`)** drives a 4-pin SSD1306 OLED and receives
  newline-delimited status messages from the ESP32 over USART1 at 115200 baud.
  It also blinks the onboard LED and drives an optional active buzzer.

The existing `firmware/esp32` and `firmware/stm32` Nucleo projects are separate
and are not modified by this build. Both projects in this folder compile, but
they have not yet been validated on physical hardware.

## Architecture

```text
Phone / web UI
      |
      v
FastAPI server  <---- HTTP polling / chunked WAV upload ----  ESP32
                                                               |  I2S0
                                                               v
                                                          INMP441 mic
                                                               |
                                            115200 8N1 UART2  |
                                                               v
                                                        STM32 Blue Pill
                                                          |          |
                                                          v          v
                                                       SSD1306    LED/buzzer
```

## Wiring

### INMP441 microphone to ESP32

| INMP441 | ESP32 |
| --- | --- |
| VDD | 3V3 |
| GND | GND |
| L/R | GND (left channel) |
| SCK / BCLK | GPIO33 |
| WS / LRCLK | GPIO25 |
| SD / DIN | GPIO32 |

The firmware uses `I2S_CHANNEL_FMT_ONLY_LEFT`. If you connect L/R to 3V3
instead, change it to `I2S_CHANNEL_FMT_ONLY_RIGHT` in
`esp32/src/audio_i2s.cpp`.

The INMP441 puts 24-bit samples in 32-bit I2S slots. The driver therefore opens
32-bit slots and converts to 16-bit PCM; the uploaded WAV is still 16 kHz,
mono, 16-bit as required by the server.

### SSD1306 OLED to Blue Pill

| OLED | Blue Pill |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | PB7 |
| SCL | PB6 |
| Address | `0x3C` |

Most 4-pin SSD1306 modules have onboard I2C pull-ups. If your module does not,
add 4.7 kOhm pull-ups from SDA and SCL to 3.3 V.

### ESP32 to Blue Pill UART

| Signal | ESP32 | Blue Pill |
| --- | --- | --- |
| ESP32 TX2 -> STM32 RX | GPIO18 | PA10 |
| ESP32 RX2 <- STM32 TX | GPIO16 | PA9 |
| Ground | GND | GND (mandatory) |

Both boards use 3.3 V UART levels. Do not use 5 V logic levels on the ESP32.
The Blue Pill's PA9/PA10 are USART1 and are not the USB pins; the USB connector
on the Blue Pill is not used by this firmware. Do not connect a USB-TTL adapter
to PA9/PA10 while the ESP32 link is connected. GPIO16/18 are free on a normal
ESP32-WROOM `esp32dev` board; on ESP32-WROVER modules they may be reserved for
PSRAM.

### Optional LED and buzzer

| Peripheral | Blue Pill pin | Notes |
| --- | --- | --- |
| Status LED | PC13 | Onboard LED, active LOW |
| Active buzzer | PB0 | Active HIGH; use a transistor if the buzzer draws more than a few mA |

To use a different buzzer pin, change `kBuzzerPin` in
`bluepill/src/main.cpp`. Set `kBuzzerActiveHigh = false` for an active-low
buzzer module. For a passive buzzer, drive it with a timer/PWM square wave
instead of the simple on/off hook.

Power each board through its own USB supply and connect the grounds together.
Do not power the Blue Pill from the ESP32 3V3 pin.

## Server configuration

The server is the same FastAPI app in `server/`. Start it from the `server/`
directory:

```bash
cd server
.venv/bin/python run.py
```

In `server/.env`, make sure:

- `DEVICE_API_KEY` matches `API_KEY` in `secrets.h`.
- `MAX_AUDIO_SECONDS` is at least the firmware's recording length.
- `COMMAND_TTL_SECONDS` is long enough for one recording plus upload.

The current default recording length is **10 seconds**. The web UI queues a
`record` command; the ESP32 polls `GET /api/device/command` every 5 seconds.

## Build and flash

### ESP32

```bash
cp firmware/esp32_bluepill/esp32/include/secrets.example.h \
   firmware/esp32_bluepill/esp32/include/secrets.h
```

Edit `secrets.h`:

```cpp
#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"
#define SERVER_URL "http://192.168.1.50:3000"
#define API_KEY "same-value-as-DEVICE_API_KEY"
```

`SERVER_URL` must be a plain HTTP origin: no path, no HTTPS, no trailing slash.
`secrets.h` is gitignored. Then build and flash:

```bash
pio run -d firmware/esp32_bluepill/esp32
pio run -d firmware/esp32_bluepill/esp32 -t upload
```

The ESP32 USB serial monitor is 115200 baud and prints Wi-Fi, command, upload,
and status messages.

### Blue Pill

The Blue Pill has no onboard USB-serial bridge. The default configuration uses
an ST-Link:

```bash
pio run -d firmware/esp32_bluepill/bluepill
pio run -d firmware/esp32_bluepill/bluepill -t upload
```

Connect ST-Link `SWDIO`, `SWCLK`, `GND`, and `3V3` to the Blue Pill. If you use
an external USB-TTL adapter and the STM32 serial bootloader instead, set
`upload_protocol = serial` and configure `upload_port` in
`bluepill/platformio.ini`, then put the board in bootloader mode with BOOT0.
Disconnect the ESP32 from PA9/PA10 while flashing over those pins.

### Recording length

The default is 10 seconds. Override it with a build flag in
`esp32/platformio.ini`:

```ini
build_flags = -D RECORD_SECONDS=30
```

Values from 1 to 300 are allowed. Increase `MAX_AUDIO_SECONDS` on the server to
match.

## UART status protocol

The ESP32 sends one line per state change, plus a heartbeat every 5 seconds:

```text
STATUS:<STATE>[:<detail>]\n
```

The Blue Pill ignores unknown lines and unknown states. Detail text is optional
and may contain colons. Examples:

```text
STATUS:BOOT:ESP32 starting
STATUS:WIFI:Connecting Wi-Fi
STATUS:READY:192.168.1.50
STATUS:RECORDING:Speak now
STATUS:UPLOADING:Saving note
STATUS:COMPLETED:Recorded
STATUS:ERROR:Wi-Fi failed
```

Visible UI states and indicators:

| State | OLED title | LED | Buzzer |
| --- | --- | --- | --- |
| `BOOT` | STARTING | 500 ms blink | none |
| `WIFI` | WI-FI | 500 ms blink | none |
| `READY` | READY | 1 s blink | one short beep on entry |
| `RECORDING` | RECORDING | 250 ms blink | one short beep on entry |
| `UPLOADING` | UPLOADING | 120 ms blink | none |
| `COMPLETED` | COMPLETED | solid on | two short beeps |
| `ERROR` | ERROR | 150 ms blink | three short beeps |

The peripheral hooks are in `bluepill/src/main.cpp`: `startIndicators()`,
`updateLed()`, and `updateBuzzer()`. Add application-specific behaviour there.

## Test procedure

1. Start the FastAPI server and confirm the web UI is reachable from the
   phone/laptop.
2. Power both boards and connect their grounds.
3. Open the ESP32 serial monitor. It should print the Wi-Fi address and
   `READY`.
4. The Blue Pill should show `READY` within 5 seconds (the heartbeat resends the
   current state).
5. Queue a recording from the web UI.
6. On the next poll the OLED changes to `RECORDING`; speak after you see
   `Speak now`.
7. The OLED shows `UPLOADING` while the final HTTP response is awaited, then
   `COMPLETED`, and returns to `READY`.
8. The new note appears in the web UI and should play at 16 kHz mono.
9. Reset the Blue Pill while the ESP32 is idle. It should resynchronise to
   `READY` within 5 seconds, which verifies the UART link and heartbeat.

The shared UART state protocol has a host-side test:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic \
  -Ifirmware/esp32_bluepill/common \
  firmware/esp32_bluepill/tests/hmi_protocol_test.cpp \
  -o /tmp/hmi_protocol_test
/tmp/hmi_protocol_test
```

The server has a device smoke test that does not need either board:

```bash
cd server
.venv/bin/python test_device.py
```

That test only checks the FastAPI device API, not the microphone, display, or
UART link.

## Troubleshooting

### OLED stays blank

- Verify 3.3 V and GND at the module.
- Verify SDA is on PB7 and SCL is on PB6.
- Confirm the address is `0x3C`; some modules use `0x3D`.
- Add I2C pull-ups if the module has none.
- Try `Wire.setClock(100000)` in `display.cpp` for long or noisy wires.
- The code assumes a 128x64 panel. For a 128x32 panel, change `kHeight` in
  `display.cpp` to 32.

### No UART status on the Blue Pill

- Check that ESP32 GPIO18 goes to Blue Pill PA10 and ESP32 GPIO16 goes to PA9.
- Connect the grounds.
- Confirm both ends are 115200 8N1.
- Watch the ESP32 serial log for the `HMI -> ...` lines.
- The Blue Pill does not print debug logs on the ESP32 link; the OLED and
  indicators are the diagnostic output.

### Microphone records silence

- Confirm INMP441 VDD is 3.3 V and L/R is tied to GND.
- Verify SCK = GPIO33, WS = GPIO25, SD = GPIO32.
- Check the ESP32 boot log for `I2S init failed`.
- Some INMP441 breakouts need a 100 kOhm pull-down on SD.
- If you tied L/R high instead, switch the channel format as described above.

### Upload fails

- `SERVER_URL` must be a plain HTTP origin, for example
  `http://192.168.1.50:3000`, with no path and no trailing slash.
- Make sure the phone/laptop firewall allows port 3000 from the ESP32.
- Verify `API_KEY` exactly matches `DEVICE_API_KEY`.
- Increase `MAX_AUDIO_SECONDS` if the server reports `Too long`.
- A command is delivered only once. If a recording fails, queue a new command
  from the web UI; the firmware intentionally does not retry delivered
  commands.

### Choppy or dropped audio

- Check Wi-Fi signal strength; the ESP32 must stream while recording.
- The firmware opens the HTTP request before capture and uses a 16-buffer I2S
  DMA queue. A blocked or very slow server can still cause an overrun.
- Avoid other high-bandwidth traffic on the same Wi-Fi network during a test.

## Design notes and limitations

- Audio is never buffered in full on the ESP32; each I2S block is converted and
  sent immediately to the server. The server still buffers the HTTP body and
  writes the final WAV.
- Because audio is streamed while recording, `RECORDING` covers most of the
  transfer; `UPLOADING` is shown while the final HTTP response is awaited. On a
  fast LAN it can be brief.
- The link between the boards is intentionally simple ASCII. There is no CRC or
  framing beyond newline termination, because it carries only short status
  messages and a periodic heartbeat. It is not used for audio.
- Status text is limited to 47 characters. Longer messages are truncated.
- The device supports one recording at a time. A new server command is polled
  after the current recording/upload completes.
- HTTPS, offline recording, automatic retry of a delivered command, and AI
  processing on the device are out of scope. AI remains opt-in on the server.
