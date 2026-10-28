# Voice Note Server

FastAPI server for an ESP32 AI voice-note device. The ESP32 uploads 16 kHz,
16-bit, mono WAV files over plain HTTP. The server stores the audio, transcribes
and organizes it with Google Gemini, and serves a mobile-friendly website for
browsing notes, asking questions, and administering users.

Phase 1 (record mode) and Phase 2 (ask mode) are implemented. Phase 3
(admin-managed firmware updates) is not included yet.

## Requirements

- Python 3.11 or newer
- A Google Gemini API key from <https://aistudio.google.com/apikey>
- A machine on the same Wi-Fi network as the ESP32

## Quick start

Run these commands from the `server/` directory.

```bash
cd server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
```

Edit `.env` and fill in at least:

```dotenv
DEVICE_API_KEY=<long random value>
GEMINI_API_KEY=<your Gemini API key>
SESSION_SECRET=<long random value>
ADMIN_PASSWORD=<your admin password>
```

Generate random secrets with:

```bash
openssl rand -hex 32
```

The server refuses to start if `DEVICE_API_KEY` or `SESSION_SECRET` is shorter
than 16 characters, or if any secret still contains a `change-me` placeholder.
The first-run `ADMIN_PASSWORD` must be at least 8 characters.

Start the server:

```bash
python run.py
```

The server binds to `0.0.0.0:3000` by default. Open
`http://<server-ip>:3000` from your phone or computer and log in with
`ADMIN_USERNAME` / `ADMIN_PASSWORD`.

To find the server IP:

```bash
hostname -I
```

If the firewall blocks the port, allow it, for example:

```bash
sudo ufw allow 3000/tcp
```

Alternative start command:

```bash
uvicorn app.main:app --host 0.0.0.0 --port 3000
```

Run a single server process. Background transcription jobs and SQLite are
configured for a small local deployment.

API documentation is available at `http://<server-ip>:3000/api/docs`.

## Configuration

All startup configuration and secrets live in `server/.env`. Relative paths are
resolved against the `server/` directory.

| Variable | Default | Purpose |
| --- | --- | --- |
| `HOST` | `0.0.0.0` | Bind address used by `python run.py` |
| `PORT` | `3000` | HTTP port |
| `LOG_LEVEL` | `INFO` | Python log level |
| `DATABASE_URL` | `sqlite:///./data/notes.db` | SQLAlchemy database URL |
| `AUDIO_DIR` | `./data/audio` | Root directory for WAV files |
| `MAX_AUDIO_SECONDS` | `120` | Maximum accepted recording length |
| `DEVICE_API_KEY` | required | Value for the `X-API-Key` header |
| `GEMINI_API_KEY` | required for AI | Google Gemini API key |
| `GEMINI_MODEL` | `gemini-3.5-flash` | Gemini model name |
| `GEMINI_TIMEOUT_SECONDS` | `90` | Gemini request timeout |
| `SESSION_SECRET` | required | Signs website login cookies |
| `COOKIE_SECURE` | `false` | Set to `true` only when serving over HTTPS |
| `ADMIN_USERNAME` | `admin` | First-run admin username |
| `ADMIN_PASSWORD` | required on first run | First-run admin password |
| `APP_TIMEZONE` | `UTC` | Default IANA timezone for `/api/time` and the website |
| `ASK_CONTEXT_NOTES` | `10` | Default number of recent notes sent to Gemini for `/api/ask` |

The admin can change the timezone and the ask-context note count from
**Settings** in the website. Those runtime values are stored in the database and
override the `.env` defaults. `GEMINI_MODEL` must be a model that accepts audio
input and supports JSON response schemas. `gemini-2.5-flash` is retired for new
API keys; if a model returns 404 or 503, switch `GEMINI_MODEL` to an available
model such as `gemini-3.5-flash`.

### Switching to PostgreSQL

`psycopg` is included in `requirements.txt`. Create a database and change
`DATABASE_URL`:

```dotenv
DATABASE_URL=postgresql+psycopg://voicenotes:password@localhost:5432/voicenotes
```

Restart the server. Tables are created automatically. There is no Alembic
migration setup, so schema changes require your own migration process.

## Website

- **Login** with a normal user or an admin account.
- **Notes** lists notes newest first, with search across title, transcript,
  summary, key points, tasks, and tags.
- **Record now** on the Notes page queues a recording request for the ESP32.
  The device starts recording the next time it polls; it never records on its
  own.
- **Note detail** shows title, timestamp, audio, and — if AI was used —
  transcript, summary, key points, tasks/reminders, and tags.
- Recorded notes are saved and playable without calling Gemini. Use
  **Generate summary with AI** on a note only when you want AI processing.
- Notes that are still processing show a processing badge and refresh
  automatically when transcription finishes.
- Failed notes still keep their audio, show the failure reason, and offer
  **Retry with AI**.
- Notes can be deleted, which also deletes the stored WAV file.
- **Ask history** shows every question and answer, including audio playback.
- **Admin → Users** creates users, grants/removes admin access, resets
  passwords, and deletes users.
- **Admin → Settings** changes the timezone and ask-context size.

The first server start creates the admin account from `ADMIN_USERNAME` and
`ADMIN_PASSWORD`. Changing `ADMIN_PASSWORD` later does not reset an existing
admin password; use **Admin → Users** instead.

## Device API

All device endpoints require this header:

```http
X-API-Key: <DEVICE_API_KEY>
```

Request bodies for `/api/note` and `/api/ask` are raw WAV bytes, not multipart
form data. The WAV must be PCM, 16 kHz, 16-bit, mono. The server rejects other
formats.

### GET /api/time

Returns the current server time in the configured timezone.

```bash
curl -H "X-API-Key: $DEVICE_API_KEY" http://localhost:3000/api/time
```

```json
{
  "status": "ok",
  "epoch": 1793115600,
  "iso": "2026-10-27T21:00:00+01:00",
  "time": "2026-10-27 21:00:00",
  "timezone": "Europe/Berlin"
}
```

`epoch` is a Unix timestamp in seconds. `time` is convenient for a simple
display. `iso` includes the UTC offset.

### GET /api/device/command

The ESP32 polls this endpoint every few seconds. It returns the oldest pending
recording request and marks it `delivered`. If nothing is pending, `command` is
`null`.

```bash
curl -H "X-API-Key: $DEVICE_API_KEY" \
  http://localhost:3000/api/device/command
```

```json
{
  "status": "ok",
  "command": "record",
  "id": "4c4f9e6e-2c72-4a4a-b8e0-0a5d6f8a2b11"
}
```

No command pending:

```json
{"status": "ok", "command": null}
```

The website **Record now** button creates the pending command. Commands older
than two minutes are ignored so a device that was offline does not record an
old request unexpectedly. Only one request is accepted while another is pending
or being recorded.

### POST /api/note

Saves the WAV and creates a note with status `recorded`. It does **not** call
Gemini automatically; the audio can be played immediately from the website. Use
**Generate summary with AI** on the note when you want transcription and
summary.

If the upload was triggered by a command, send the command id so the server can
mark it completed:

```http
X-Command-Id: 4c4f9e6e-2c72-4a4a-b8e0-0a5d6f8a2b11
```

```bash
curl -X POST \
  -H "X-API-Key: $DEVICE_API_KEY" \
  -H "Content-Type: audio/wav" \
  --data-binary @samples/sample_note.wav \
  http://localhost:3000/api/note
```

```json
{
  "status": "ok",
  "id": "7f9c2f34-5e45-4f56-9e2b-11f7a1d4c888",
  "face": "done",
  "display_text": "Recorded"
}
```

When AI is requested from the website, Gemini auto-detects the language and
produces transcript, title, summary, key points, tasks/reminders, and tags. On
success the note becomes `completed`. If Gemini finds no intelligible speech,
or the request fails, the note becomes `failed` with a reason; the audio is
kept and the note can be retried with AI from the website.

### GET /api/notes/{id}/status

Poll this after uploading a note.

```bash
curl -H "X-API-Key: $DEVICE_API_KEY" \
  http://localhost:3000/api/notes/7f9c2f34-5e45-4f56-9e2b-11f7a1d4c888/status
```

Recorded (audio saved, no AI yet):

```json
{
  "status": "ok",
  "id": "7f9c2f34-5e45-4f56-9e2b-11f7a1d4c888",
  "state": "recorded",
  "face": "done",
  "display_text": "Recorded"
}
```

Processing (AI requested from the website):

```json
{
  "status": "ok",
  "id": "7f9c2f34-5e45-4f56-9e2b-11f7a1d4c888",
  "state": "processing",
  "face": "wait",
  "display_text": "Processing"
}
```

Completed:

```json
{
  "status": "ok",
  "id": "7f9c2f34-5e45-4f56-9e2b-11f7a1d4c888",
  "state": "completed",
  "face": "done",
  "display_text": "Done",
  "title": "Milk and dentist"
}
```

Failed:

```json
{
  "status": "ok",
  "id": "7f9c2f34-5e45-4f56-9e2b-11f7a1d4c888",
  "state": "failed",
  "face": "sad",
  "display_text": "No speech",
  "code": "NO_SPEECH",
  "reason": "No speech detected in the recording."
}
```

`status` stays `ok` here because the status lookup itself succeeded. Use `state`
to decide whether to keep polling.

### POST /api/ask

Uploads a spoken question. The server transcribes it, sends recent notes as
context, and returns a short answer for the display. The full question, short
answer, and a longer answer are saved and shown on the website.

```bash
curl -X POST \
  -H "X-API-Key: $DEVICE_API_KEY" \
  -H "Content-Type: audio/wav" \
  --data-binary @samples/sample_question.wav \
  http://localhost:3000/api/ask
```

```json
{
  "status": "ok",
  "face": "done",
  "display_text": "You need to buy milk."
}
```

`display_text` is always 80 characters or fewer on success. This request waits
for Gemini, so use a generous HTTP timeout in the firmware (the server default
Gemini timeout is 90 seconds).

### Error responses

Every API error uses this shape:

```json
{
  "status": "error",
  "code": "NO_SPEECH",
  "face": "sad",
  "display_text": "No speech"
}
```

`display_text` for errors is guaranteed to be 16 characters or fewer. Use the
`code` for logic and `display_text` for the screen.

| HTTP | `code` | `display_text` | Meaning |
| --- | --- | --- | --- |
| 401 | `BAD_KEY` | `Bad key` | Missing or incorrect `X-API-Key` |
| 400 | `NO_AUDIO` | `No audio` | Empty request body |
| 400 | `BAD_AUDIO` | `Bad audio` | Not a valid 16 kHz / 16-bit / mono PCM WAV |
| 413 | `TOO_LONG` | `Too long` | Recording exceeds `MAX_AUDIO_SECONDS` |
| 422 | `NO_SPEECH` | `No speech` | AI found no intelligible speech |
| 404 | `NOTE_NOT_FOUND` | `Not found` | Unknown note ID on the status endpoint |
| 404 | `NOT_FOUND` | `Not found` | Unknown `/api/...` path |
| 400 | `BAD_REQUEST` | `Bad request` | Malformed API request |
| 502 | `AI_ERROR` | `AI unavailable` | Gemini failed or timed out |
| 500 | `SERVER_ERROR` | `Server error` | Internal server error |

Failed notes additionally use these codes in `GET /api/notes/{id}/status`:

| `code` | `display_text` | Meaning |
| --- | --- | --- |
| `NO_SPEECH` | `No speech` | Recording contained no intelligible speech |
| `AI_ERROR` | `AI unavailable` | Gemini failed or timed out |
| `INTERRUPTED` | `Interrupted` | Server restarted while the note was processing |
| `FILE_MISSING` | `Audio missing` | Stored WAV file is missing |
| `FAILED` | `Failed` | Generic processing failure |

`/api/note` returns success immediately after saving the audio. AI processing
starts only when requested from the website; its result is shown there. The
device sees the note as `recorded` and stops polling.

### Faces

The server returns these values in `face`:

| Face | Used for |
| --- | --- |
| `done` | Upload accepted, audio recorded, AI completed, or ask answered |
| `wait` | Note AI processing |
| `sad` | Any API error or failed AI processing |

## Test script

`test_device.py` acts like the ESP32. It uploads the included sample WAV,
prints the JSON response, and polls the note status.

```bash
cd server
source .venv/bin/activate
python test_device.py
```

Options:

```bash
python test_device.py --time
python test_device.py --ask
python test_device.py --wav /path/to/recording.wav
python test_device.py --url http://192.168.1.50:3000
python test_device.py --key YOUR_DEVICE_API_KEY
python test_device.py --no-poll
```

The script reads `server/.env` for `DEVICE_API_KEY` and `PORT`. `--ask` uses
`samples/sample_question.wav`. The default note sample contains synthetic speech
asking to buy milk and call the dentist. A successful note test exits with code
0 once the note is `recorded`; AI processing is optional and can be started from
the website. Ask context only includes `completed` notes, so process a note with
AI first if you want it used as context.

## ESP32 firmware

The active firmware is the separate PlatformIO project at
`../firmware/voice_note`. It:

- Connects to Wi-Fi and shows status on a 128x64 SSD1306 OLED.
- Polls `/api/device/command` every 5 seconds. It does not record on its own.
- On a `record` command, streams 5 seconds of 16 kHz, 16-bit, mono WAV audio
  directly to `/api/note` using chunked HTTP, so it does not need a 160 KB
  recording buffer.
- Sends `X-Command-Id` so the website request is marked completed.
- Polls `/api/notes/{id}/status` and shows the result on the OLED.

Build and upload:

```bash
pio run -d firmware/voice_note
pio run -d firmware/voice_note -t upload
```

`include/secrets.h` is gitignored; copy `include/secrets.example.h` and fill in
your Wi-Fi, server URL, and API key.

The API is plain HTTP. Do not expose the server directly to the public internet.

## Storage

```text
server/
├── app/                     FastAPI application, templates, static assets
├── data/                    Created at runtime, ignored by git
│   ├── notes.db             SQLite database
│   └── audio/
│       ├── notes/<id>.wav
│       └── asks/<id>.wav
├── samples/                 Test WAV files
├── test_device.py           Fake-device upload and poll script
├── .env.example
├── requirements.txt
└── run.py
```

Audio is stored as files on disk. The database stores the relative path, status,
transcript, structured note fields, and failure information.

## Security notes

- This server is designed for a trusted local network over plain HTTP.
- Keep `.env`, the database, and the audio directory out of version control.
  They are covered by `.gitignore`.
- The device API key is sent in clear text over HTTP. Use a VPN such as
  Tailscale/WireGuard if you need access outside your LAN, or put the server
  behind an HTTPS reverse proxy.
- Website sessions are signed cookies. Passwords are hashed with PBKDF2-SHA256.
- Website forms use CSRF tokens.
- `COOKIE_SECURE` must stay `false` for plain HTTP and should be `true` if you
  add HTTPS.

## Phase 3

Firmware update management is intentionally not implemented yet. The existing
admin area and storage layer leave room to add an upload endpoint, firmware
metadata table, and a device-facing version/check endpoint later.
