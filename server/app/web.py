from datetime import UTC
from typing import Annotated
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from fastapi import APIRouter, BackgroundTasks, Depends, Form, HTTPException, Request
from fastapi.responses import FileResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
from sqlalchemy import Text, cast, func, or_, select
from sqlalchemy.orm import Session

from .api import COMMAND_TTL, process_note
from .config import BASE_DIR, settings
from .db import get_db
from .models import (
    COMMAND_DELIVERED,
    COMMAND_PENDING,
    STATUS_FAILED,
    STATUS_PROCESSING,
    STATUS_RECORDED,
    Ask,
    DeviceCommand,
    Note,
    User,
    utcnow,
)
from .security import (
    check_csrf,
    csrf_token,
    current_user,
    flash,
    hash_password,
    normalize_username,
    pop_flashes,
    require_admin,
    require_user,
    valid_username,
    verify_password,
)
from .settings_store import (
    get_ask_context_count,
    get_timezone_name,
    set_setting,
)
from .storage import delete_audio, resolve_audio

router = APIRouter(tags=["website"])
templates = Jinja2Templates(directory=str(BASE_DIR / "app" / "templates"))
templates.env.globals["csrf_token"] = csrf_token
templates.env.globals["app_name"] = settings.app_name


def local_datetime(value, tz_name: str):
    if value is None:
        return ""
    if value.tzinfo is None:
        value = value.replace(tzinfo=UTC)
    try:
        zone = ZoneInfo(tz_name)
    except (ZoneInfoNotFoundError, ValueError):
        zone = UTC
    return value.astimezone(zone).strftime("%Y-%m-%d %H:%M")


def format_duration(value) -> str:
    try:
        seconds = int(round(float(value or 0)))
    except (TypeError, ValueError):
        seconds = 0
    return f"{seconds // 60}:{seconds % 60:02d}"


templates.env.filters["localdt"] = local_datetime
templates.env.filters["duration"] = format_duration


def render(request: Request, name: str, context: dict | None = None, status_code: int = 200):
    payload = dict(context or {})
    payload.setdefault("request", request)
    payload.setdefault("user", getattr(request.state, "user", None))
    payload["flashes"] = pop_flashes(request)
    return templates.TemplateResponse(
        request=request, name=name, context=payload, status_code=status_code
    )


def audio_available(relative_path: str) -> bool:
    try:
        return resolve_audio(relative_path).is_file()
    except ValueError:
        return False


def admin_count(db: Session) -> int:
    return int(
        db.scalar(
            select(func.count(User.id)).where(User.is_admin.is_(True))
        )
        or 0
    )


@router.get("/")
def index(user: User = Depends(require_user)):
    return RedirectResponse("/notes", status_code=303)


@router.get("/login")
def login_page(request: Request, db: Session = Depends(get_db)):
    if current_user(request, db) is not None:
        return RedirectResponse("/notes", status_code=303)
    return render(request, "login.html")


@router.post("/login")
def login(
    request: Request,
    username: Annotated[str, Form()],
    password: Annotated[str, Form()],
    csrf: Annotated[str, Form()],
    db: Session = Depends(get_db),
):
    check_csrf(request, csrf)
    user = db.scalar(
        select(User).where(User.username == normalize_username(username))
    )
    if user is None or not verify_password(password, user.password_hash):
        return render(
            request,
            "login.html",
            {"error": "Invalid username or password."},
            status_code=401,
        )
    request.session.clear()
    request.session["user_id"] = user.id
    csrf_token(request)
    return RedirectResponse("/notes", status_code=303)


@router.post("/logout")
def logout(
    request: Request,
    csrf: Annotated[str, Form()],
    user: User = Depends(require_user),
):
    check_csrf(request, csrf)
    request.session.clear()
    return RedirectResponse("/login", status_code=303)


@router.get("/notes")
def notes_page(
    request: Request,
    q: str = "",
    page: int = 1,
    db: Session = Depends(get_db),
    user: User = Depends(require_user),
):
    q = q.strip()
    per_page = 20
    filters = []
    if q:
        like = f"%{q}%"
        filters.append(
            or_(
                Note.title.ilike(like),
                Note.summary.ilike(like),
                Note.transcript.ilike(like),
                cast(Note.tags, Text).ilike(like),
                cast(Note.key_points, Text).ilike(like),
                cast(Note.tasks, Text).ilike(like),
            )
        )

    count_statement = select(func.count()).select_from(Note)
    notes_statement = select(Note)
    if filters:
        count_statement = count_statement.where(*filters)
        notes_statement = notes_statement.where(*filters)

    total = int(db.scalar(count_statement) or 0)
    total_pages = max(1, (total + per_page - 1) // per_page)
    page = max(1, min(page, total_pages))
    notes = db.scalars(
        notes_statement.order_by(Note.created_at.desc())
        .offset((page - 1) * per_page)
        .limit(per_page)
    ).all()
    audio_available_ids = {
        note.id for note in notes if audio_available(note.audio_path)
    }

    active_command = db.scalar(
        select(DeviceCommand)
        .where(
            DeviceCommand.status.in_([COMMAND_PENDING, COMMAND_DELIVERED]),
            DeviceCommand.created_at >= utcnow() - COMMAND_TTL,
        )
        .order_by(DeviceCommand.created_at.desc())
        .limit(1)
    )

    return render(
        request,
        "notes.html",
        {
            "notes": notes,
            "q": q,
            "page": page,
            "total_pages": total_pages,
            "total": total,
            "tz": get_timezone_name(db),
            "active_command": active_command,
            "audio_available_ids": audio_available_ids,
        },
    )


@router.post("/device/record")
def request_device_record(
    request: Request,
    csrf: Annotated[str, Form()],
    db: Session = Depends(get_db),
    user: User = Depends(require_user),
):
    check_csrf(request, csrf)
    cutoff = utcnow() - COMMAND_TTL
    existing = db.scalar(
        select(DeviceCommand)
        .where(
            DeviceCommand.status.in_([COMMAND_PENDING, COMMAND_DELIVERED]),
            DeviceCommand.created_at >= cutoff,
        )
        .order_by(DeviceCommand.created_at.desc())
        .limit(1)
    )
    if existing is not None:
        if existing.status == COMMAND_PENDING:
            flash(request, "Recording already requested. Waiting for the device.", "info")
        else:
            flash(request, "The device is already recording.", "info")
    else:
        db.add(DeviceCommand(command="record", status=COMMAND_PENDING))
        db.commit()
        flash(request, "Recording requested. The device will start in a few seconds.")
    return RedirectResponse("/notes", status_code=303)


@router.get("/notes/{note_id}")
def note_detail(
    note_id: str,
    request: Request,
    db: Session = Depends(get_db),
    user: User = Depends(require_user),
):
    note = db.get(Note, note_id)
    if note is None:
        raise HTTPException(status_code=404, detail="Note not found")
    return render(
        request,
        "note_detail.html",
        {
            "note": note,
            "tz": get_timezone_name(db),
            "audio_available": audio_available(note.audio_path),
        },
    )


@router.get("/notes/{note_id}/status")
def note_status(
    note_id: str,
    db: Session = Depends(get_db),
    user: User = Depends(require_user),
):
    note = db.get(Note, note_id)
    if note is None:
        raise HTTPException(status_code=404, detail="Note not found")
    return {"state": note.status}


@router.get("/notes/{note_id}/audio")
def note_audio(
    note_id: str,
    db: Session = Depends(get_db),
    user: User = Depends(require_user),
):
    note = db.get(Note, note_id)
    if note is None:
        raise HTTPException(status_code=404, detail="Note not found")
    try:
        path = resolve_audio(note.audio_path)
    except ValueError as exc:
        raise HTTPException(status_code=404, detail="Audio not found") from exc
    if not path.is_file():
        raise HTTPException(status_code=404, detail="Audio not found")
    return FileResponse(
        path,
        media_type="audio/wav",
        headers={"Content-Disposition": f'inline; filename="{note.id}.wav"'},
    )


@router.post("/notes/{note_id}/delete")
def delete_note(
    note_id: str,
    request: Request,
    csrf: Annotated[str, Form()],
    db: Session = Depends(get_db),
    user: User = Depends(require_user),
):
    check_csrf(request, csrf)
    note = db.get(Note, note_id)
    if note is None:
        raise HTTPException(status_code=404, detail="Note not found")
    delete_audio(note.audio_path)
    db.delete(note)
    db.commit()
    flash(request, "Note deleted.")
    return RedirectResponse("/notes", status_code=303)


@router.post("/notes/{note_id}/retry")
def retry_note(
    note_id: str,
    request: Request,
    background_tasks: BackgroundTasks,
    csrf: Annotated[str, Form()],
    db: Session = Depends(get_db),
    user: User = Depends(require_user),
):
    check_csrf(request, csrf)
    note = db.get(Note, note_id)
    if note is None:
        raise HTTPException(status_code=404, detail="Note not found")
    if note.status not in (STATUS_RECORDED, STATUS_FAILED):
        flash(request, "This note cannot be processed with AI right now.", "error")
    elif not audio_available(note.audio_path):
        flash(request, "The audio file is missing.", "error")
    else:
        note.status = STATUS_PROCESSING
        note.failure_code = None
        note.failure_reason = None
        note.updated_at = utcnow()
        db.commit()
        background_tasks.add_task(process_note, note.id)
        flash(request, "AI processing started.")
    return RedirectResponse(f"/notes/{note.id}", status_code=303)


@router.get("/asks")
def asks_page(
    request: Request,
    page: int = 1,
    db: Session = Depends(get_db),
    user: User = Depends(require_user),
):
    per_page = 20
    total = int(db.scalar(select(func.count()).select_from(Ask)) or 0)
    total_pages = max(1, (total + per_page - 1) // per_page)
    page = max(1, min(page, total_pages))
    asks = db.scalars(
        select(Ask)
        .order_by(Ask.created_at.desc())
        .offset((page - 1) * per_page)
        .limit(per_page)
    ).all()
    return render(
        request,
        "asks.html",
        {
            "asks": asks,
            "page": page,
            "total_pages": total_pages,
            "total": total,
            "tz": get_timezone_name(db),
        },
    )


@router.get("/asks/{ask_id}/audio")
def ask_audio(
    ask_id: str,
    db: Session = Depends(get_db),
    user: User = Depends(require_user),
):
    ask = db.get(Ask, ask_id)
    if ask is None:
        raise HTTPException(status_code=404, detail="Question not found")
    try:
        path = resolve_audio(ask.audio_path)
    except ValueError as exc:
        raise HTTPException(status_code=404, detail="Audio not found") from exc
    if not path.is_file():
        raise HTTPException(status_code=404, detail="Audio not found")
    return FileResponse(
        path,
        media_type="audio/wav",
        headers={"Content-Disposition": f'inline; filename="{ask.id}.wav"'},
    )


@router.get("/admin/users")
def admin_users(
    request: Request,
    db: Session = Depends(get_db),
    admin: User = Depends(require_admin),
):
    users = db.scalars(select(User).order_by(User.username)).all()
    return render(
        request,
        "admin_users.html",
        {"users": users, "tz": get_timezone_name(db)},
    )


@router.post("/admin/users")
def admin_create_user(
    request: Request,
    username: Annotated[str, Form()],
    password: Annotated[str, Form()],
    csrf: Annotated[str, Form()],
    is_admin: Annotated[str | None, Form()] = None,
    db: Session = Depends(get_db),
    admin: User = Depends(require_admin),
):
    check_csrf(request, csrf)
    normalized = normalize_username(username)
    if not valid_username(normalized):
        flash(request, "Username must be 3-32 characters: a-z, 0-9, _ . -", "error")
    elif len(password) < 8:
        flash(request, "Password must be at least 8 characters.", "error")
    elif db.scalar(select(User).where(User.username == normalized)) is not None:
        flash(request, "That username already exists.", "error")
    else:
        db.add(
            User(
                username=normalized,
                password_hash=hash_password(password),
                is_admin=bool(is_admin),
            )
        )
        db.commit()
        flash(request, f"User {normalized} created.")
    return RedirectResponse("/admin/users", status_code=303)


@router.post("/admin/users/{user_id}/toggle-admin")
def admin_toggle_admin(
    user_id: int,
    request: Request,
    csrf: Annotated[str, Form()],
    db: Session = Depends(get_db),
    admin: User = Depends(require_admin),
):
    check_csrf(request, csrf)
    target = db.get(User, user_id)
    if target is None:
        flash(request, "User not found.", "error")
    elif target.id == admin.id:
        flash(request, "You cannot change your own admin role.", "error")
    elif target.is_admin and admin_count(db) <= 1:
        flash(request, "At least one admin is required.", "error")
    else:
        target.is_admin = not target.is_admin
        db.commit()
        flash(request, f"Updated {target.username}.")
    return RedirectResponse("/admin/users", status_code=303)


@router.post("/admin/users/{user_id}/password")
def admin_reset_password(
    user_id: int,
    request: Request,
    password: Annotated[str, Form()],
    csrf: Annotated[str, Form()],
    db: Session = Depends(get_db),
    admin: User = Depends(require_admin),
):
    check_csrf(request, csrf)
    target = db.get(User, user_id)
    if target is None:
        flash(request, "User not found.", "error")
    elif len(password) < 8:
        flash(request, "Password must be at least 8 characters.", "error")
    else:
        target.password_hash = hash_password(password)
        db.commit()
        flash(request, f"Password updated for {target.username}.")
    return RedirectResponse("/admin/users", status_code=303)


@router.post("/admin/users/{user_id}/delete")
def admin_delete_user(
    user_id: int,
    request: Request,
    csrf: Annotated[str, Form()],
    db: Session = Depends(get_db),
    admin: User = Depends(require_admin),
):
    check_csrf(request, csrf)
    target = db.get(User, user_id)
    if target is None:
        flash(request, "User not found.", "error")
    elif target.id == admin.id:
        flash(request, "You cannot delete your own account.", "error")
    elif target.is_admin and admin_count(db) <= 1:
        flash(request, "At least one admin is required.", "error")
    else:
        db.delete(target)
        db.commit()
        flash(request, f"User {target.username} deleted.")
    return RedirectResponse("/admin/users", status_code=303)


@router.get("/admin/settings")
def admin_settings(
    request: Request,
    db: Session = Depends(get_db),
    admin: User = Depends(require_admin),
):
    return render(
        request,
        "admin_settings.html",
        {
            "timezone_name": get_timezone_name(db),
            "ask_context_notes": get_ask_context_count(db),
            "gemini_model": settings.gemini_model,
            "max_audio_seconds": settings.max_audio_seconds,
            "audio_dir": str(settings.audio_dir),
            "database_backend": db.get_bind().url.get_backend_name(),
            "default_timezone": settings.app_timezone,
        },
    )


@router.post("/admin/settings")
def admin_save_settings(
    request: Request,
    timezone_name: Annotated[str, Form()],
    ask_context_notes: Annotated[str, Form()],
    csrf: Annotated[str, Form()],
    db: Session = Depends(get_db),
    admin: User = Depends(require_admin),
):
    check_csrf(request, csrf)
    timezone_name = timezone_name.strip()
    errors = []

    try:
        ZoneInfo(timezone_name)
    except (ZoneInfoNotFoundError, ValueError):
        errors.append("Unknown timezone. Use an IANA name such as Europe/Berlin.")

    try:
        context_count = int(ask_context_notes)
        if context_count < 0 or context_count > 50:
            raise ValueError
    except (TypeError, ValueError):
        context_count = 0
        errors.append("Ask context notes must be a number from 0 to 50.")

    if errors:
        for message in errors:
            flash(request, message, "error")
    else:
        set_setting(db, "timezone", timezone_name)
        set_setting(db, "ask_context_notes", str(context_count))
        db.commit()
        flash(request, "Settings saved.")
    return RedirectResponse("/admin/settings", status_code=303)
