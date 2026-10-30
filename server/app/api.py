import asyncio
import io
import logging
import wave
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Annotated

from fastapi import APIRouter, Depends, Request
from sqlalchemy import select
from sqlalchemy.orm import Session

from . import gemini
from .config import settings
from .db import SessionLocal, get_db
from .errors import (
    AI_ERROR,
    BAD_AUDIO,
    NO_AUDIO,
    NO_SPEECH,
    NOTE_NOT_FOUND,
    SERVER_ERROR,
    TOO_LONG,
    DeviceError,
    failure_display_text,
)
from .models import (
    COMMAND_COMPLETED,
    COMMAND_DELIVERED,
    COMMAND_PENDING,
    STATUS_COMPLETED,
    STATUS_FAILED,
    STATUS_PROCESSING,
    STATUS_RECORDED,
    Ask,
    DeviceCommand,
    Note,
    new_id,
    utcnow,
)
from .security import require_device_key
from .settings_store import get_ask_context_count, get_timezone
from .storage import resolve_audio, save_audio

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api", tags=["device"])
# Keep a delivered command active for the complete recording and upload.  This
# prevents the web UI from accepting another command halfway through a longer
# live upload.
COMMAND_TTL = timedelta(seconds=settings.command_ttl_seconds)


@dataclass(frozen=True)
class WavInfo:
    duration_seconds: float
    frames: int
    sample_rate: int


def parse_wav(data: bytes) -> WavInfo:
    try:
        with wave.open(io.BytesIO(data), "rb") as wav_file:
            channels = wav_file.getnchannels()
            sample_width = wav_file.getsampwidth()
            sample_rate = wav_file.getframerate()
            frames = wav_file.getnframes()
    except Exception as exc:
        raise DeviceError(BAD_AUDIO) from exc

    if channels != 1 or sample_width != 2 or sample_rate != 16000:
        raise DeviceError(BAD_AUDIO)
    if frames <= 0:
        raise DeviceError(NO_AUDIO)

    duration = frames / sample_rate
    if duration > settings.max_audio_seconds:
        raise DeviceError(TOO_LONG)
    return WavInfo(duration_seconds=duration, frames=frames, sample_rate=sample_rate)


async def read_wav(request: Request) -> tuple[bytes, WavInfo]:
    max_bytes = settings.max_audio_seconds * 32000 + 1_000_000
    content_length = request.headers.get("content-length")
    if content_length:
        try:
            if int(content_length) > max_bytes:
                raise DeviceError(TOO_LONG)
        except ValueError:
            pass

    data = await request.body()
    if not data:
        raise DeviceError(NO_AUDIO)
    if len(data) > max_bytes:
        raise DeviceError(TOO_LONG)

    return data, parse_wav(data)


def fallback_title(transcript: str) -> str:
    text = " ".join(transcript.split())
    return text[:80] if text else "Untitled note"


def fail_note(db: Session, note: Note, code: str, reason: str) -> None:
    note.status = STATUS_FAILED
    note.failure_code = code
    note.failure_reason = reason[:1000]
    note.updated_at = utcnow()
    db.commit()


def fail_ask(db: Session, ask: Ask, code: str, reason: str) -> None:
    ask.status = STATUS_FAILED
    ask.failure_code = code
    ask.failure_reason = reason[:1000]
    ask.updated_at = utcnow()
    db.commit()


async def process_note(note_id: str) -> None:
    db = SessionLocal()
    try:
        note = db.get(Note, note_id)
        if note is None or note.status != STATUS_PROCESSING:
            return

        try:
            wav_bytes = resolve_audio(note.audio_path).read_bytes()
        except (OSError, ValueError):
            fail_note(db, note, "FILE_MISSING", "The audio file is missing.")
            return

        try:
            result = await asyncio.wait_for(
                asyncio.to_thread(gemini.extract_note, wav_bytes),
                timeout=settings.gemini_timeout_seconds + 5,
            )
        except gemini.NoSpeechError:
            fail_note(db, note, "NO_SPEECH", "No speech detected in the recording.")
            return
        except TimeoutError:
            fail_note(db, note, "AI_ERROR", "Gemini timed out while processing the note.")
            return
        except gemini.GeminiError as exc:
            logger.warning("Gemini failed for note %s: %s", note_id, exc)
            fail_note(db, note, "AI_ERROR", str(exc))
            return
        except Exception:
            logger.exception("Unexpected error while processing note %s", note_id)
            fail_note(db, note, "AI_ERROR", "Unexpected error while processing the note.")
            return

        note.transcript = result.transcript
        note.language = result.language or None
        note.title = result.title or fallback_title(result.transcript)
        note.summary = result.summary or result.transcript[:500]
        note.key_points = result.key_points
        note.tasks = result.tasks
        note.tags = result.tags
        note.status = STATUS_COMPLETED
        note.failure_code = None
        note.failure_reason = None
        note.updated_at = utcnow()
        db.commit()
        logger.info("Note %s processed successfully", note_id)
    finally:
        db.close()


def build_context(db: Session, limit: int) -> str:
    if limit <= 0:
        return ""

    notes = db.scalars(
        select(Note)
        .where(Note.status == STATUS_COMPLETED)
        .order_by(Note.created_at.desc())
        .limit(limit)
    ).all()

    blocks: list[str] = []
    total = 0
    for note in notes:
        transcript = (note.transcript or "").strip()
        if len(transcript) > 1500:
            transcript = transcript[:1500] + "..."
        block = (
            f"Date: {note.created_at.date().isoformat()}\n"
            f"Title: {note.title or '(untitled)'}\n"
            f"Summary: {note.summary or '(none)'}\n"
            f"Key points: {', '.join(map(str, note.key_points or [])) or '(none)'}\n"
            f"Tasks: {', '.join(map(str, note.tasks or [])) or '(none)'}\n"
            f"Tags: {', '.join(map(str, note.tags or [])) or '(none)'}\n"
            f"Transcript: {transcript or '(none)'}"
        )
        if total + len(block) > 20000:
            break
        blocks.append(block)
        total += len(block)
    return "\n\n".join(blocks)


@router.get("/time")
def get_time(
    db: Annotated[Session, Depends(get_db)],
    _: Annotated[None, Depends(require_device_key)],
):
    zone = get_timezone(db)
    now = datetime.now(zone)
    return {
        "status": "ok",
        "epoch": int(now.timestamp()),
        "iso": now.isoformat(timespec="seconds"),
        "time": now.strftime("%Y-%m-%d %H:%M:%S"),
        "timezone": str(zone),
    }


@router.get("/device/command")
def get_device_command(
    db: Annotated[Session, Depends(get_db)],
    _: Annotated[None, Depends(require_device_key)],
):
    cutoff = utcnow() - COMMAND_TTL
    command = db.scalar(
        select(DeviceCommand)
        .where(
            DeviceCommand.status == COMMAND_PENDING,
            DeviceCommand.created_at >= cutoff,
        )
        .order_by(DeviceCommand.created_at.asc())
        .limit(1)
    )
    if command is None:
        return {"status": "ok", "command": None}

    command.status = COMMAND_DELIVERED
    command.updated_at = utcnow()
    db.commit()
    logger.info("Delivered device command %s", command.id)
    return {"status": "ok", "command": command.command, "id": command.id}


@router.post("/note")
async def create_note(
    request: Request,
    db: Annotated[Session, Depends(get_db)],
    _: Annotated[None, Depends(require_device_key)],
):
    data, info = await read_wav(request)
    note_id = new_id()
    try:
        audio_path = save_audio("notes", note_id, data)
    except OSError as exc:
        logger.exception("Could not save note audio")
        raise DeviceError(SERVER_ERROR) from exc

    note = Note(
        id=note_id,
        status=STATUS_RECORDED,
        title="Voice note",
        audio_path=audio_path,
        duration_seconds=info.duration_seconds,
    )
    db.add(note)
    db.commit()

    command_id = request.headers.get("x-command-id", "").strip()
    if command_id:
        command = db.get(DeviceCommand, command_id)
        if command is not None and command.status in (
            COMMAND_PENDING,
            COMMAND_DELIVERED,
        ):
            command.status = COMMAND_COMPLETED
            command.note_id = note.id
            command.updated_at = utcnow()
            db.commit()

    return {
        "status": "ok",
        "id": note.id,
        "face": "done",
        "display_text": "Recorded",
    }


@router.get("/notes/{note_id}/status")
def get_note_status(
    note_id: str,
    db: Annotated[Session, Depends(get_db)],
    _: Annotated[None, Depends(require_device_key)],
):
    note = db.get(Note, note_id)
    if note is None:
        raise DeviceError(NOTE_NOT_FOUND)

    if note.status == STATUS_RECORDED:
        return {
            "status": "ok",
            "id": note.id,
            "state": "recorded",
            "face": "done",
            "display_text": "Recorded",
        }

    if note.status == STATUS_PROCESSING:
        return {
            "status": "ok",
            "id": note.id,
            "state": "processing",
            "face": "wait",
            "display_text": "Processing",
        }

    if note.status == STATUS_COMPLETED:
        return {
            "status": "ok",
            "id": note.id,
            "state": "completed",
            "face": "done",
            "display_text": "Done",
            "title": note.title,
        }

    code = note.failure_code or "FAILED"
    return {
        "status": "ok",
        "id": note.id,
        "state": "failed",
        "face": "sad",
        "display_text": failure_display_text(code),
        "code": code,
        "reason": note.failure_reason,
    }


@router.post("/ask")
async def ask(
    request: Request,
    db: Annotated[Session, Depends(get_db)],
    _: Annotated[None, Depends(require_device_key)],
):
    data, _info = await read_wav(request)
    ask_id = new_id()
    try:
        audio_path = save_audio("asks", ask_id, data)
    except OSError as exc:
        logger.exception("Could not save ask audio")
        raise DeviceError(SERVER_ERROR) from exc

    ask_record = Ask(id=ask_id, status=STATUS_PROCESSING, audio_path=audio_path)
    db.add(ask_record)
    db.commit()

    context = build_context(db, get_ask_context_count(db))
    try:
        result = await asyncio.wait_for(
            asyncio.to_thread(gemini.answer_question, data, context),
            timeout=settings.gemini_timeout_seconds + 5,
        )
    except gemini.NoSpeechError:
        fail_ask(db, ask_record, "NO_SPEECH", "No speech detected in the recording.")
        raise DeviceError(NO_SPEECH)
    except TimeoutError:
        fail_ask(db, ask_record, "AI_ERROR", "Gemini timed out while answering.")
        raise DeviceError(AI_ERROR)
    except gemini.GeminiError as exc:
        logger.warning("Gemini failed for ask %s: %s", ask_id, exc)
        fail_ask(db, ask_record, "AI_ERROR", str(exc))
        raise DeviceError(AI_ERROR)
    except Exception:
        logger.exception("Unexpected error while answering ask %s", ask_id)
        fail_ask(db, ask_record, "AI_ERROR", "Unexpected error while answering.")
        raise DeviceError(SERVER_ERROR)

    ask_record.question = result.question
    ask_record.answer = result.answer
    ask_record.details = result.details
    ask_record.language = result.language or None
    ask_record.status = STATUS_COMPLETED
    ask_record.failure_code = None
    ask_record.failure_reason = None
    ask_record.updated_at = utcnow()
    db.commit()

    return {
        "status": "ok",
        "face": "done",
        "display_text": result.answer,
    }
