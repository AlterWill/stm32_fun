import uuid
from datetime import UTC, datetime
from typing import Any

from sqlalchemy import JSON, Boolean, DateTime, Float, Index, Integer, String, Text
from sqlalchemy.orm import Mapped, mapped_column

from .db import Base

STATUS_RECORDED = "recorded"
STATUS_PROCESSING = "processing"
STATUS_COMPLETED = "completed"
STATUS_FAILED = "failed"

COMMAND_PENDING = "pending"
COMMAND_DELIVERED = "delivered"
COMMAND_COMPLETED = "completed"
COMMAND_FAILED = "failed"


def utcnow() -> datetime:
    return datetime.now(UTC)


def new_id() -> str:
    return str(uuid.uuid4())


class User(Base):
    __tablename__ = "users"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    username: Mapped[str] = mapped_column(String(64), unique=True, index=True)
    password_hash: Mapped[str] = mapped_column(String(255))
    is_admin: Mapped[bool] = mapped_column(Boolean, default=False)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow)


class Note(Base):
    __tablename__ = "notes"
    __table_args__ = (Index("ix_notes_status_created", "status", "created_at"),)

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=new_id)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, index=True
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, onupdate=utcnow
    )
    status: Mapped[str] = mapped_column(String(16), default=STATUS_PROCESSING, index=True)
    audio_path: Mapped[str] = mapped_column(String(512))
    duration_seconds: Mapped[float] = mapped_column(Float, default=0.0)
    language: Mapped[str | None] = mapped_column(String(32), nullable=True)
    transcript: Mapped[str | None] = mapped_column(Text, nullable=True)
    title: Mapped[str | None] = mapped_column(String(200), nullable=True)
    summary: Mapped[str | None] = mapped_column(Text, nullable=True)
    key_points: Mapped[list[Any]] = mapped_column(JSON, default=list)
    tasks: Mapped[list[Any]] = mapped_column(JSON, default=list)
    tags: Mapped[list[Any]] = mapped_column(JSON, default=list)
    failure_code: Mapped[str | None] = mapped_column(String(32), nullable=True)
    failure_reason: Mapped[str | None] = mapped_column(Text, nullable=True)


class DeviceCommand(Base):
    __tablename__ = "device_commands"
    __table_args__ = (Index("ix_device_commands_status_created", "status", "created_at"),)

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=new_id)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, index=True
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, onupdate=utcnow
    )
    command: Mapped[str] = mapped_column(String(32), default="record")
    status: Mapped[str] = mapped_column(String(16), default=COMMAND_PENDING, index=True)
    note_id: Mapped[str | None] = mapped_column(String(36), nullable=True)


class Ask(Base):
    __tablename__ = "asks"

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=new_id)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, index=True
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, onupdate=utcnow
    )
    status: Mapped[str] = mapped_column(String(16), default=STATUS_PROCESSING)
    audio_path: Mapped[str] = mapped_column(String(512))
    language: Mapped[str | None] = mapped_column(String(32), nullable=True)
    question: Mapped[str | None] = mapped_column(Text, nullable=True)
    answer: Mapped[str | None] = mapped_column(String(80), nullable=True)
    details: Mapped[str | None] = mapped_column(Text, nullable=True)
    failure_code: Mapped[str | None] = mapped_column(String(32), nullable=True)
    failure_reason: Mapped[str | None] = mapped_column(Text, nullable=True)


class AppSetting(Base):
    __tablename__ = "app_settings"

    key: Mapped[str] = mapped_column(String(64), primary_key=True)
    value: Mapped[str] = mapped_column(Text)
