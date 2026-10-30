from pathlib import Path

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

BASE_DIR = Path(__file__).resolve().parent.parent


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=BASE_DIR / ".env",
        env_file_encoding="utf-8",
        case_sensitive=False,
        extra="ignore",
    )

    app_name: str = "Voice Note Server"
    host: str = "0.0.0.0"
    port: int = Field(default=3000, ge=1, le=65535)
    log_level: str = "INFO"

    database_url: str = f"sqlite:///{(BASE_DIR / 'data' / 'notes.db').as_posix()}"
    audio_dir: Path = BASE_DIR / "data" / "audio"
    # Five minutes of the device's 16 kHz / 16-bit / mono WAV is 9.6 MB.
    max_audio_seconds: int = Field(default=300, ge=1, le=600)
    # This applies both while a command is waiting for the device and while it
    # is recording/uploading, so it must exceed the longest recording.
    command_ttl_seconds: int = Field(default=600, ge=30, le=3600)

    device_api_key: str = ""
    gemini_api_key: str = ""
    gemini_model: str = "gemini-3.5-flash"
    gemini_timeout_seconds: int = Field(default=90, ge=10, le=300)

    session_secret: str = ""
    cookie_secure: bool = False

    admin_username: str = "admin"
    admin_password: str = ""

    app_timezone: str = "UTC"
    ask_context_notes: int = Field(default=10, ge=0, le=50)

    @field_validator("audio_dir", mode="after")
    @classmethod
    def resolve_audio_dir(cls, value: Path) -> Path:
        if not value.is_absolute():
            return (BASE_DIR / value).resolve()
        return value


settings = Settings()
