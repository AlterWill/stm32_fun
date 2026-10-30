# AGENTS.md

Instructions for coding agents working in this repository.

## Branch and scope

- Active development branch: `feat-stm32-main`, cut from `origin/feat-recording`.
- The branch is not merged into `main`. Local `main` is at `4fb65c8`, before the
  remote merge commit `4eb716f`.
- Do not merge, rebase, or force-push shared branches unless the user explicitly
  asks. To publish this branch, use `git push -u origin feat-stm32-main`; it
  currently tracks `origin/feat-recording`.
- Do not commit changes unless the user asks. Never commit `.env`, `secrets.h`,
  databases, recorded audio, `.pio/`, or `.venv/`.
- The repository also contains report artifacts (`mc (1).pdf`, `current.tex`,
  `overleaf_report.zip`, `figures/`, `report_sections/`). They are not firmware.
  Do not delete or rewrite them unless the task is report-related.

## Architecture

The active target is a split ESP32 + STM32 Blue Pill system:

- **ESP32** reads the INMP441 microphone over I2S, connects to Wi-Fi, polls the
  server, records audio, and streams a 16 kHz mono 16-bit WAV to `/api/note`
  using chunked HTTP. It also sends line-based status messages to the Blue Pill.
- **STM32 Blue Pill** receives those status messages over USART1, drives the
  SSD1306 OLED, and controls the onboard LED and optional buzzer. It has no
  Wi-Fi and no HTTP code.
- **Laptop/FastAPI server** stores notes, serves the web UI, and optionally calls
  Gemini. There is no local Whisper implementation on the Blue Pill.
- The active firmware lives in `firmware/esp32_bluepill/`:
  - `esp32/` is the ESP32 project.
  - `bluepill/` is the STM32F103 project.
  - `common/hmi_protocol.h` is the shared status protocol.
  - `tests/hmi_protocol_test.cpp` is the host-side protocol test.
- `firmware/stm32/` (old Nucleo-F446RE project) has been removed.
  `firmware/esp32/` is retained only as a legacy Nucleo bridge reference.
  `firmware/voice_note/` is the original single-ESP32 reference.

### Pin map (source of truth)

Use the active firmware code, not older migration tables:

| Signal | Connection |
| --- | --- |
| INMP441 SCK / WS / SD | ESP32 GPIO33 / GPIO25 / GPIO32 |
| INMP441 VDD / GND / L-R | 3.3 V / GND / GND |
| OLED SDA / SCL | Blue Pill PB7 / PB6 (auto-detect also tries PB9 / PB8) |
| OLED address / supply | `0x3C` or `0x3D` / 3.3 V and GND |
| ESP32 UART2 RX | GPIO16 <- Blue Pill PA9 (USART1 TX) |
| ESP32 UART2 TX | GPIO18 -> Blue Pill PA10 (USART1 RX) |
| Ground | Common ground; power each board through its own USB |
| Blue Pill LED / buzzer | PC13 active-low / PB0 active-high (optional) |

- ESP32-Blue Pill link: **115200 baud**, 8N1.
- USB serial/debug logs: **115200 baud**.
- The old Nucleo wiring (PB10/PB12/PC3, PB9/PB8, PC6/PC7, 921600 baud) is no
  longer active. `transition.md` and older README sections are historical.

## Firmware behavior

### ESP32 (`firmware/esp32_bluepill/esp32`)

- Board: `esp32dev`; framework: Arduino.
- I2S0 master RX, 16 kHz, 32-bit slots, mono; DMA buffers sized for 128-sample
  blocks. Samples are converted to 16-bit little-endian PCM before upload.
- Polls `GET /api/device/command` every 5 seconds with `X-API-Key`.
- Default recording is **10 seconds**. Override with
  `build_flags = -D RECORD_SECONDS=<seconds>` in
  `firmware/esp32_bluepill/esp32/platformio.ini`; valid range is 1-300.
- Sends `STATUS:WIFI`, `STATUS:READY:<ip>`, `STATUS:RECORDING:<text>`,
  `STATUS:UPLOADING:<text>`, `STATUS:COMPLETED:<text>`, and
  `STATUS:ERROR:<text>` to the Blue Pill on USART2.
- Uploads chunked WAV to `POST /api/note` with `X-Command-Id`; no full audio
  buffer is kept in RAM.
- `SERVER_URL` must be a plain HTTP origin such as
  `http://192.168.1.50:3000`; no path, no HTTPS, no Tailscale address.
- There is no offline recording, and a delivered command is not automatically
  retried.
- If Wi-Fi fails, the firmware scans and prints visible networks to the debug
  UART; a disconnect reason of 15 normally means a wrong WPA2 password.

### Blue Pill (`firmware/esp32_bluepill/bluepill`)

- Board: `bluepill_f103c8`; framework: Arduino.
- USART1 on PA9/PA10 at 115200 8N1 receives `STATUS` lines from the ESP32.
- Parses `STATUS:<STATE>[:<detail>]\n`; states are `BOOT`, `WIFI`, `READY`,
  `RECORDING`, `UPLOADING`, `COMPLETED`, and `ERROR`.
- Drives SSD1306 on I2C1. It tries PB7/PB6 and PB9/PB8, at addresses `0x3C` and
  `0x3D`, so both common Blue Pill wiring choices work.
- LED/buzzer patterns: Ready 1 beep, Recording 1 beep, Completed 2 beeps, Error
  3 beeps. The onboard LED is PC13 active-low.
- `SERIAL_RX_BUFFER_SIZE=256` is set because the OLED redraw can be slower than
  the status stream.

### Status protocol (`firmware/esp32_bluepill/common/hmi_protocol.h`)

- ASCII lines terminated by `\n`; optional `\r` is ignored.
- Format: `STATUS:<STATE>` with an optional `:<detail>` suffix.
- The detail is sanitized by the ESP32 (newlines/tabs replaced) and capped at 47
  characters.
- A 5-second heartbeat repeats the current state.
- There is no CRC or sequence number; this link carries UI status only.

## Server

- FastAPI app in `server/`; start from that directory with
  `.venv/bin/python run.py`.
- Device endpoints: `GET /api/device/command`, `POST /api/note`, and note status
  polling. The web UI queues commands.
- `MAX_AUDIO_SECONDS` defaults to 300 in the branch, but the current local
  `server/.env` still sets `120`. Raise it before testing recordings longer than
  two minutes. `COMMAND_TTL_SECONDS` defaults to 600.
- AI is opt-in per note. Recording never calls Gemini automatically. The working
  model is `gemini-3.5-flash`; do not switch back to the retired 2.5 model.
- Login is `admin`; the password is in `server/.env`. Do not print secrets.
- `DEVICE_API_KEY` in `server/.env` must match `API_KEY` in
  `firmware/esp32_bluepill/esp32/include/secrets.h`.
- If server code changed, restart the server; switching git branches does not
  reload a running process.

## Build and test

```bash
# ESP32 build and upload (requires local secrets.h)
cp firmware/esp32_bluepill/esp32/include/secrets.example.h \
  firmware/esp32_bluepill/esp32/include/secrets.h
# edit secrets.h: WIFI_SSID, WIFI_PASSWORD, SERVER_URL, API_KEY
pio run -d firmware/esp32_bluepill/esp32
pio run -d firmware/esp32_bluepill/esp32 -t upload --upload-port /dev/ttyUSB0

# Blue Pill build and upload (ST-Link by default)
pio run -d firmware/esp32_bluepill/bluepill
pio run -d firmware/esp32_bluepill/bluepill -t upload

# Host-side status protocol test
g++ -std=c++17 -Wall -Wextra -pedantic \
  firmware/esp32_bluepill/tests/hmi_protocol_test.cpp -o /tmp/hmi_protocol_test
/tmp/hmi_protocol_test

# Server smoke test against a running server
cd server && .venv/bin/python test_device.py
```

- The protocol test and both active firmware builds pass as of 2026-10-30.
- There is no configured lint/typecheck task for the server. Use
  `.venv/bin/python -m compileall app` and `test_device.py` for smoke checks.
- The ESP32 serial port on the current bench machine is `/dev/ttyUSB0`.
- The Blue Pill requires an ST-Link (or a USB-TTL adapter plus the STM32
  bootloader) to flash. Disconnect the ESP32 from PA9/PA10 when using a serial
  bootloader.
- Hardware validation is still pending for the combined ESP32/Blue Pill audio
  link; do not claim it is fully verified without a real recording test.

## Change rules

- Keep audio at 16 kHz, mono, 16-bit. If duration changes, update both firmware
  projects, `MAX_AUDIO_SECONDS`, `COMMAND_TTL_SECONDS`, and the README together.
- Keep the ESP32-Blue Pill link at 115200 8N1.
- Do not add blocking delays in the ESP32 audio capture/stream path.
- Do not attempt to run Whisper on the Blue Pill. Use the laptop/Pi server for
  transcription/summarization.
- Do not add automatic Gemini calls on upload; preserve the opt-in AI flow.
- Keep `README.md` and this file in sync when behavior, wiring, or commands
  change.
- Do not print secrets. The Wi-Fi password, API key, admin password, and session
  secret live in `secrets.h` and `server/.env`.
- The user wants the Blue Pill, not the old Nucleo-F446RE project. Do not
  reintroduce Nucleo-only wiring or the 921600-baud UART link.
