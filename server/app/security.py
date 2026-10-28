import hashlib
import hmac
import re
import secrets
from base64 import b64decode, b64encode
from typing import Annotated

from fastapi import Depends, Header, HTTPException, Request
from sqlalchemy.orm import Session

from .config import settings
from .db import get_db
from .errors import BAD_KEY, DeviceError
from .models import User

PBKDF2_ITERATIONS = 600_000
USERNAME_RE = re.compile(r"^[a-z0-9_.-]{3,32}$")


def normalize_username(value: str) -> str:
    return value.strip().lower()


def valid_username(value: str) -> bool:
    return bool(USERNAME_RE.fullmatch(normalize_username(value)))


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    digest = hashlib.pbkdf2_hmac(
        "sha256", password.encode("utf-8"), salt, PBKDF2_ITERATIONS
    )
    return (
        f"pbkdf2_sha256${PBKDF2_ITERATIONS}$"
        f"{b64encode(salt).decode()}${b64encode(digest).decode()}"
    )


def verify_password(password: str, encoded: str) -> bool:
    try:
        algorithm, iterations, salt_b64, digest_b64 = encoded.split("$", 3)
        if algorithm != "pbkdf2_sha256":
            return False
        salt = b64decode(salt_b64)
        expected = b64decode(digest_b64)
        actual = hashlib.pbkdf2_hmac(
            "sha256", password.encode("utf-8"), salt, int(iterations)
        )
        return hmac.compare_digest(actual, expected)
    except (ValueError, TypeError):
        return False


def require_device_key(
    x_api_key: Annotated[str | None, Header(alias="X-API-Key")] = None,
) -> None:
    if (
        not settings.device_api_key
        or not x_api_key
        or not hmac.compare_digest(x_api_key, settings.device_api_key)
    ):
        raise DeviceError(BAD_KEY)


def current_user(request: Request, db: Session) -> User | None:
    user_id = request.session.get("user_id")
    if user_id is None:
        return None
    user = db.get(User, user_id)
    if user is None:
        request.session.pop("user_id", None)
    return user


def require_user(request: Request, db: Session = Depends(get_db)) -> User:
    user = current_user(request, db)
    if user is None:
        raise HTTPException(status_code=303, headers={"Location": "/login"})
    request.state.user = user
    return user


def require_admin(user: User = Depends(require_user)) -> User:
    if not user.is_admin:
        raise HTTPException(status_code=403, detail="Admin access required")
    return user


def csrf_token(request: Request) -> str:
    token = request.session.get("csrf")
    if not token:
        token = secrets.token_urlsafe(32)
        request.session["csrf"] = token
    return token


def check_csrf(request: Request, token: str) -> None:
    expected = request.session.get("csrf")
    if not expected or not token or not hmac.compare_digest(expected, token):
        raise HTTPException(status_code=400, detail="Invalid CSRF token")


def flash(request: Request, message: str, category: str = "success") -> None:
    flashes = request.session.setdefault("flashes", [])
    flashes.append({"message": message, "category": category})


def pop_flashes(request: Request) -> list[dict[str, str]]:
    return request.session.pop("flashes", [])
