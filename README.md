# Voice Note Device

A phone-triggered voice recorder built around an ESP32, an INMP441 I2S
microphone, and an SSD1306 OLED. The ESP32 streams a 16 kHz mono WAV to a local
FastAPI server, which stores the audio, serves a web UI, and optionally sends the
audio to Gemini for transcription, summary, tasks, and tags.

The important behavior: **recording does not call Gemini automatically.** A new
note is saved as `recorded` and is immediately playable. AI is opt-in per note
from the website.

## Repository layout

```text
.
├── firmware/voice_note/     Active PlatformIO ESP32 firmware
├── server/                  FastAPI server, web UI, API, test script
└── README.md
```

`server/README.md` contains the full server, API, and deployment documentation.

## Features

- Phone-triggered recording: the website queues a command; the device polls and
  records only when asked.
- Chunked WAV upload while recording, so the device does not need a large audio
  buffer.
- Notes list with inline audio playback, search, and a detail page.
- AI is optional per note: **Generate summary with AI** or **Retry with AI**.
- Failed AI notes keep their audio and can be retried later.
- Ask history and an optional `/api/ask` voice-question endpoint.
- Login, admin user management, CSRF protection, and a device API key.

## Quick start

### 1. Server

```bash
cd server
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
```

Edit `server/.env` and set at least:

```dotenv
DEVICE_API_KEY=<long random value>
SESSION_SECRET=<long random value>
ADMIN_PASSWORD=<your admin password>
GEMINI_API_KEY=<optional; leave empty to use audio-only mode>
GEMINI_MODEL=gemini-3.5-flash
```

Generate random values with:

```bash
python -c "import secrets; print(secrets.token_urlsafe(32))"
```

Start the server:

```bash
cd server
.venv/bin/python run.py
```

Open `http://<server-lan-ip>:3000/notes` and log in with the admin account from
`.env`.

### 2. ESP32 firmware

```bash
cp firmware/voice_note/include/secrets.example.h \
   firmware/voice_note/include/secrets.h
```

Edit `secrets.h` with your Wi-Fi credentials, the server URL, and the same
`DEVICE_API_KEY` used by the server. Then build and flash:

```bash
pio run -d firmware/voice_note
pio run -d firmware/voice_note -t upload
```

`secrets.h` is gitignored. The device polls `/api/device/command` every 5 seconds
and shows `Ready | Use phone to record` when idle.

### 3. Test without the device

```bash
cd server
.venv/bin/python test_device.py
```

This uploads `samples/sample_note.wav` and polls the note status.

## Current hardware wiring

### ESP32 (active firmware)

| Signal | ESP32 pin |
| --- | --- |
| INMP441 SD | GPIO32 |
| INMP441 SCK | GPIO33 |
| INMP441 WS | GPIO25 |
| INMP441 VDD | 3.3 V |
| INMP441 GND / L/R | GND / GND (left channel) |
| OLED SDA | GPIO21 |
| OLED SCL | GPIO22 |
| OLED VDD / GND | 3.3 V / GND |
| OLED address | `0x3C` |

The firmware uses I2S `I2S_NUM_0`, 16 kHz, 16-bit, mono. It streams the WAV to
`POST /api/note` using chunked HTTP and sends `X-Command-Id` so the website
request is marked complete.

## Recording length and data size

The default firmware records **60 seconds** per trigger. It uploads each small
PCM block immediately with chunked HTTP while it records, so recording length
does not consume a large ESP32 RAM buffer. At 16 kHz / 16-bit mono:

- 1 second = 32 KB
- 1 minute = 1.92 MB
- 5 minutes = 9.6 MB

A 5-minute note is fine for the laptop server and the website. To change the
length, change the default in `firmware/voice_note/src/main.cpp`, or add
`build_flags = -D RECORD_SECONDS=300` to `firmware/voice_note/platformio.ini`.
Then make the server windows at least as long as the recording plus its upload
time:

1. `MAX_AUDIO_SECONDS` in `server/.env` (default: 300 seconds)
2. `COMMAND_TTL_SECONDS` in `server/.env` (default: 600 seconds)

The firmware's WAV header is written with the selected final length before the
first packet, and the server accepts HTTP chunked uploads. This makes a
one-minute recording about 1.92 MB and a five-minute recording about 9.6 MB
without allocating either full recording on the ESP32.

For Gemini, 5 minutes of WAV is about 12.8 MB as base64, under the 20 MB inline
request limit. Ten minutes would require compression or the Gemini Files API.

## STM32 migration assessment

The current hardware is an ESP32. The removed `stmMain.md` was a Nucleo-F446RE
INMP441 bit-bang test. The question was: STM32 as the main controller, ESP32 as a
Wi-Fi module, mic and display on the STM32, and Whisper running locally on the
STM32.

### Short answer

- **STM32 + ESP32 Wi-Fi + mic/display on STM32 + Whisper on the laptop/Pi:** very
  feasible. This is a moderate firmware refactor, roughly 2-4 days including
  wiring and testing.
- **Whisper running on a Nucleo-F446RE:** not feasible. The F446RE has 512 KB
  flash and 128 KB SRAM; Whisper tiny int8 is roughly 39 MB of model data and
  needs far more compute and memory. Even with external SDRAM it would be too
  slow.
- **Whisper running on STM32MP1/N6 or another Linux/NPU part:** possible in
  principle, but a multi-week project with model conversion, quantization, and
  memory/NPU work. A Raspberry Pi or the existing laptop is the practical local
  Whisper host.

### Feasible split: STM32 main, ESP32 Wi-Fi, Whisper on laptop/Pi

STM32 responsibilities:

- Read the INMP441 over hardware I2S with DMA.
- Drive the SSD1306 OLED.
- Run the command/status state machine.
- Send framed audio chunks and commands to the ESP32 over UART.
- Optionally run a small VAD/keyword spotter.

ESP32 responsibilities:

- Wi-Fi connection.
- Poll `/api/device/command`.
- Upload the WAV stream to `/api/note` (or a new endpoint if the server changes).
- Forward status/results back to the STM32 over UART.

Server responsibilities:

- Store the WAV and note metadata.
- Serve the website.
- Run Whisper locally (faster-whisper/whisper.cpp) or call Gemini, depending on
  configuration.

### Suggested STM32F446RE wiring

This uses hardware I2S2 and I2C1, avoiding the old bit-bang pin conflict:

| Signal | STM32F446RE pin |
| --- | --- |
| INMP441 SD | PC3 (I2S2_SD) |
| INMP441 SCK | PB10 (I2S2_CK) |
| INMP441 WS | PB12 (I2S2_WS) |
| INMP441 VDD / GND / L/R | 3.3 V / GND / GND |
| OLED SDA | PB9 (I2C1_SDA) |
| OLED SCL | PB8 (I2C1_SCL) |
| OLED VDD / GND | 3.3 V / GND |
| ESP32 UART RX | STM32 PA2 (USART2_TX) -> ESP32 GPIO16 |
| ESP32 UART TX | STM32 PA3 (USART2_RX) <- ESP32 GPIO17 |
| Ground | Common GND |

Notes:

- Use **460800 or 921600 baud** for the STM32-ESP link. At 16 kHz/16-bit mono,
  audio is 32 KB/s; 115200 baud is not enough.
- Power the ESP32 from a separate 3.3 V regulator that can supply its peak
  current; do not rely on the Nucleo 3.3 V rail. Tie grounds together.
- The ESP32 no longer connects to the mic or OLED; it only has power, ground, and
  UART.
- If you keep the old bit-bang wiring (SD PA4, SCK PA5, WS PB9), move the OLED to
  I2C2 (PB10/PB11), but PB10 conflicts with hardware I2S2. Prefer the I2S2/I2C1
  table above for a production build.

### Software changes

- **STM32 firmware:** new PlatformIO project or STM32Cube/HAL project. Implement
  I2S DMA, OLED, UART framing, command handling, and audio chunking. Do not
  bit-bang I2S for production.
- **ESP32 firmware:** strip out I2S and OLED code; keep Wi-Fi, HTTP, command
  polling, and add a UART protocol. The current `firmware/voice_note` is a good
  starting point for the HTTP side.
- **Server:** unchanged if the ESP32 still uploads a WAV to `/api/note`. If the
  STM32 runs Whisper locally, add an endpoint for transcript/audio or keep the
  server as storage + website only.
- **Protocol:** use a simple framed UART protocol with a magic byte, length,
  sequence number, and CRC. Add flow control or a ring buffer for audio.

### If you really want local Whisper

Use a Linux-capable board (Raspberry Pi Zero 2 W, Pi 4/5, or the laptop) and run
`faster-whisper` or `whisper.cpp`. Keep the STM32 as the audio/UI controller and
the ESP32 as the Wi-Fi link. On the STM32F446RE, run a small keyword-spotting
model (microWakeWord/TFLite Micro) instead of Whisper.

## Security notes

- `server/.env`, `server/data/`, `server/.venv/`, and
  `firmware/voice_note/include/secrets.h` are gitignored.
- The device API key is sent over plain HTTP. Keep the server on a trusted LAN or
  put it behind a VPN/HTTPS reverse proxy.
- The removed root ESP32 sketch contained Wi-Fi credentials in older git
  history. Rotate that Wi-Fi password before publishing or sharing the repo.
- Change the first-run admin password from the website after logging in.

## License

No license has been added yet.
