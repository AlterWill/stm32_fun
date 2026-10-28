from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from sqlalchemy.orm import Session

from .config import settings
from .models import AppSetting


def get_setting(db: Session, key: str, default: str | None = None) -> str | None:
    row = db.get(AppSetting, key)
    if row is None or row.value is None:
        return default
    return row.value


def set_setting(db: Session, key: str, value: str) -> None:
    row = db.get(AppSetting, key)
    if row is None:
        db.add(AppSetting(key=key, value=value))
    else:
        row.value = value


def get_timezone_name(db: Session) -> str:
    name = get_setting(db, "timezone", settings.app_timezone) or "UTC"
    try:
        ZoneInfo(name)
    except (ZoneInfoNotFoundError, ValueError):
        return "UTC"
    return name


def get_timezone(db: Session) -> ZoneInfo:
    return ZoneInfo(get_timezone_name(db))


def get_ask_context_count(db: Session) -> int:
    raw = get_setting(db, "ask_context_notes", str(settings.ask_context_notes))
    try:
        value = int(raw) if raw is not None else settings.ask_context_notes
    except (TypeError, ValueError):
        value = settings.ask_context_notes
    return max(0, min(50, value))
