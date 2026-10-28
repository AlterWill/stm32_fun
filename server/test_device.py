#!/usr/bin/env python3
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from dotenv import load_dotenv

BASE_DIR = Path(__file__).resolve().parent
load_dotenv(BASE_DIR / ".env")


def default_url() -> str:
    if os.getenv("SERVER_URL"):
        return os.environ["SERVER_URL"].rstrip("/")
    port = os.getenv("PORT", "3000")
    return f"http://127.0.0.1:{port}"


def call(
    base_url: str,
    path: str,
    api_key: str,
    data: bytes | None = None,
    timeout: float = 30,
) -> tuple[int, dict]:
    request = urllib.request.Request(
        f"{base_url}{path}",
        data=data,
        method="POST" if data is not None else "GET",
    )
    request.add_header("X-API-Key", api_key)
    request.add_header("Accept", "application/json")
    if data is not None:
        request.add_header("Content-Type", "audio/wav")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
            status = response.status
    except urllib.error.HTTPError as exc:
        raw = exc.read()
        status = exc.code
    except urllib.error.URLError as exc:
        print(f"Connection failed: {exc.reason}", file=sys.stderr)
        sys.exit(1)

    try:
        payload = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        print(f"Non-JSON response (HTTP {status}): {raw[:500]!r}", file=sys.stderr)
        sys.exit(1)
    return status, payload


def print_payload(status: int, payload: dict) -> None:
    print(f"HTTP {status}")
    print(json.dumps(payload, indent=2, ensure_ascii=False))


def poll_note(base_url: str, api_key: str, note_id: str, timeout: int) -> str:
    deadline = time.monotonic() + timeout
    last_state = None
    while time.monotonic() < deadline:
        status, payload = call(
            base_url, f"/api/notes/{note_id}/status", api_key, timeout=30
        )
        state = payload.get("state", "unknown")
        if state != last_state:
            print_payload(status, payload)
            last_state = state
        if state != "processing":
            return state
        time.sleep(2)
    print(f"Timed out waiting for note {note_id}.", file=sys.stderr)
    return "timeout"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Upload a WAV to the voice note server like an ESP32 would."
    )
    parser.add_argument("--url", default=default_url())
    parser.add_argument("--key", default=os.getenv("DEVICE_API_KEY", ""))
    parser.add_argument("--wav", type=Path)
    parser.add_argument("--ask", action="store_true", help="Test /api/ask instead of /api/note.")
    parser.add_argument("--time", action="store_true", help="Test /api/time and exit.")
    parser.add_argument("--no-poll", action="store_true", help="Do not poll note status.")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()

    if not args.key:
        print(
            "DEVICE_API_KEY is not set. Put it in server/.env or pass --key.",
            file=sys.stderr,
        )
        return 2

    base_url = args.url.rstrip("/")

    if args.time:
        status, payload = call(base_url, "/api/time", args.key)
        print_payload(status, payload)
        return 0 if status == 200 and payload.get("status") == "ok" else 1

    if args.wav is None:
        args.wav = BASE_DIR / "samples" / (
            "sample_question.wav" if args.ask else "sample_note.wav"
        )
    if not args.wav.is_file():
        print(f"WAV file not found: {args.wav}", file=sys.stderr)
        return 2

    audio = args.wav.read_bytes()
    print(f"Uploading {args.wav} ({len(audio)} bytes) to {base_url}")

    if args.ask:
        status, payload = call(
            base_url,
            "/api/ask",
            args.key,
            data=audio,
            timeout=max(30, args.timeout),
        )
        print_payload(status, payload)
        return 0 if status == 200 and payload.get("status") == "ok" else 1

    status, payload = call(base_url, "/api/note", args.key, data=audio)
    print_payload(status, payload)
    if status != 200 or payload.get("status") != "ok":
        return 1
    if args.no_poll:
        return 0

    state = poll_note(base_url, args.key, payload["id"], args.timeout)
    return 0 if state in ("recorded", "completed") else 1


if __name__ == "__main__":
    raise SystemExit(main())
