import os
from pathlib import Path

from .config import settings


def audio_base_dir() -> Path:
    return settings.audio_dir.resolve()


def save_audio(subdir: str, audio_id: str, data: bytes) -> str:
    relative = f"{subdir}/{audio_id}.wav"
    target = resolve_audio(relative)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(".wav.tmp")
    temporary.write_bytes(data)
    os.replace(temporary, target)
    return relative


def resolve_audio(relative: str) -> Path:
    base = audio_base_dir()
    target = (base / relative).resolve()
    if not target.is_relative_to(base):
        raise ValueError("Audio path escapes the audio directory.")
    return target


def delete_audio(relative: str) -> None:
    try:
        resolve_audio(relative).unlink(missing_ok=True)
    except (OSError, ValueError):
        pass
