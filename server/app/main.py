import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, Request
from fastapi.exception_handlers import (
    http_exception_handler,
    request_validation_exception_handler,
)
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse, PlainTextResponse
from sqlalchemy import func, select, update
from starlette.exceptions import HTTPException as StarletteHTTPException
from starlette.middleware.sessions import SessionMiddleware
from starlette.staticfiles import StaticFiles

from .api import router as api_router
from .config import BASE_DIR, settings
from .db import SessionLocal, init_db
from .errors import (
    BAD_REQUEST,
    NOT_FOUND,
    SERVER_ERROR,
    DeviceError,
)
from .models import (
    STATUS_FAILED,
    STATUS_PROCESSING,
    Ask,
    Note,
    User,
    utcnow,
)
from .security import hash_password, normalize_username
from .web import router as web_router

logging.basicConfig(
    level=settings.log_level.upper(),
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)


class _PollingLogFilter(logging.Filter):
    def filter(self, record: logging.LogRecord) -> bool:
        return "/api/device/command" not in record.getMessage()


logging.getLogger("uvicorn.access").addFilter(_PollingLogFilter())
logger = logging.getLogger(__name__)


def validate_config() -> None:
    if not settings.session_secret:
        raise RuntimeError(
            "SESSION_SECRET is missing. Generate one with: openssl rand -hex 32"
        )
    if len(settings.session_secret) < 16:
        raise RuntimeError("SESSION_SECRET must be at least 16 characters long.")
    if not settings.device_api_key:
        raise RuntimeError(
            "DEVICE_API_KEY is missing. Generate one with: openssl rand -hex 32"
        )
    if len(settings.device_api_key) < 16:
        raise RuntimeError("DEVICE_API_KEY must be at least 16 characters long.")
    if settings.session_secret.startswith("change-me") or settings.device_api_key.startswith(
        "change-me"
    ):
        raise RuntimeError(
            "Replace the change-me placeholders in .env with real random secrets."
        )
    if not settings.gemini_api_key:
        logger.warning(
            "GEMINI_API_KEY is not set. Note processing and ask mode will fail."
        )


def seed_admin() -> None:
    db = SessionLocal()
    try:
        username = normalize_username(settings.admin_username)
        existing = db.scalar(select(User).where(User.username == username))
        if existing is not None:
            if not existing.is_admin:
                logger.warning(
                    "User %s exists but is not an admin. Promote it from another "
                    "admin account if needed.",
                    username,
                )
            return

        user_count = int(db.scalar(select(func.count(User.id))) or 0)
        if not settings.admin_password:
            if user_count == 0:
                raise RuntimeError(
                    "ADMIN_PASSWORD is required on first run so an admin account "
                    "can be created."
                )
            logger.warning(
                "No user named %s exists. Set ADMIN_PASSWORD and restart to create it.",
                username,
            )
            return
        if len(settings.admin_password) < 8:
            raise RuntimeError("ADMIN_PASSWORD must be at least 8 characters long.")
        if settings.admin_password.startswith("change-me"):
            raise RuntimeError("Replace the change-me ADMIN_PASSWORD in .env.")

        db.add(
            User(
                username=username,
                password_hash=hash_password(settings.admin_password),
                is_admin=True,
            )
        )
        db.commit()
        logger.info("Created initial admin user %s", username)
    finally:
        db.close()


def recover_interrupted() -> None:
    db = SessionLocal()
    try:
        now = utcnow()
        notes = db.execute(
            update(Note)
            .where(Note.status == STATUS_PROCESSING)
            .values(
                status=STATUS_FAILED,
                failure_code="INTERRUPTED",
                failure_reason="Server restarted while processing. Retry the note.",
                updated_at=now,
            )
        )
        asks = db.execute(
            update(Ask)
            .where(Ask.status == STATUS_PROCESSING)
            .values(
                status=STATUS_FAILED,
                failure_code="INTERRUPTED",
                failure_reason="Server restarted while processing this question.",
                updated_at=now,
            )
        )
        db.commit()
        if notes.rowcount:
            logger.warning("Marked %s interrupted note(s) as failed.", notes.rowcount)
        if asks.rowcount:
            logger.warning("Marked %s interrupted ask(s) as failed.", asks.rowcount)
    finally:
        db.close()


@asynccontextmanager
async def lifespan(_app: FastAPI):
    validate_config()
    settings.audio_dir.mkdir(parents=True, exist_ok=True)
    init_db()
    seed_admin()
    recover_interrupted()
    logger.info(
        "Voice Note Server ready on %s:%s",
        settings.host,
        settings.port,
    )
    yield


app = FastAPI(
    title=settings.app_name,
    version="1.0.0",
    lifespan=lifespan,
    docs_url="/api/docs",
    redoc_url=None,
    openapi_url="/api/openapi.json",
)

app.add_middleware(
    SessionMiddleware,
    secret_key=settings.session_secret or "startup-validation-pending",
    session_cookie="voice_note_session",
    max_age=14 * 24 * 3600,
    same_site="lax",
    https_only=settings.cookie_secure,
)

app.mount(
    "/static",
    StaticFiles(directory=str(BASE_DIR / "app" / "static")),
    name="static",
)

app.include_router(api_router)
app.include_router(web_router)


@app.middleware("http")
async def security_headers(request: Request, call_next):
    response = await call_next(request)
    response.headers.setdefault("X-Content-Type-Options", "nosniff")
    response.headers.setdefault("X-Frame-Options", "DENY")
    response.headers.setdefault("Referrer-Policy", "same-origin")
    return response


@app.get("/healthz", include_in_schema=False)
def healthz():
    return {"status": "ok"}


@app.exception_handler(DeviceError)
async def device_error_handler(request: Request, exc: DeviceError):
    return JSONResponse(
        status_code=exc.status_code,
        content={
            "status": "error",
            "code": exc.code,
            "face": "sad",
            "display_text": exc.display_text,
        },
    )


@app.exception_handler(StarletteHTTPException)
async def api_http_error_handler(request: Request, exc: StarletteHTTPException):
    if request.url.path.startswith("/api/"):
        if exc.status_code == 404:
            spec = NOT_FOUND
        elif exc.status_code < 500:
            spec = BAD_REQUEST
        else:
            spec = SERVER_ERROR
        return JSONResponse(
            status_code=exc.status_code,
            content={
                "status": "error",
                "code": spec.code,
                "face": "sad",
                "display_text": spec.text,
            },
        )
    return await http_exception_handler(request, exc)


@app.exception_handler(RequestValidationError)
async def api_validation_error_handler(request: Request, exc: RequestValidationError):
    if request.url.path.startswith("/api/"):
        return JSONResponse(
            status_code=BAD_REQUEST.status_code,
            content={
                "status": "error",
                "code": BAD_REQUEST.code,
                "face": "sad",
                "display_text": BAD_REQUEST.text,
            },
        )
    return await request_validation_exception_handler(request, exc)


@app.exception_handler(Exception)
async def unhandled_error_handler(request: Request, exc: Exception):
    logger.exception("Unhandled error on %s", request.url.path)
    if request.url.path.startswith("/api/"):
        return JSONResponse(
            status_code=SERVER_ERROR.status_code,
            content={
                "status": "error",
                "code": SERVER_ERROR.code,
                "face": "sad",
                "display_text": SERVER_ERROR.text,
            },
        )
    return PlainTextResponse("Internal server error", status_code=500)


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        "app.main:app",
        host=settings.host,
        port=settings.port,
        log_level=settings.log_level.lower(),
    )
