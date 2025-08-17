from __future__ import annotations
from datetime import datetime, timedelta
from sqlalchemy import String, Integer, DateTime, Boolean, ForeignKey, UniqueConstraint
from sqlalchemy.orm import Mapped, mapped_column, relationship
from .db import Base


class Device(Base):
    __tablename__ = "devices"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String(64), unique=True, index=True)
    name: Mapped[str | None] = mapped_column(String(128), nullable=True)
    location: Mapped[str | None] = mapped_column(String(128), nullable=True)
    timezone: Mapped[str | None] = mapped_column(String(64), nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)
    last_seen_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)
    default_wake_time: Mapped[str] = mapped_column(String(8), default="03:00")
    min_days_before_repeat: Mapped[int] = mapped_column(Integer, default=7)
    last_image_asset_id: Mapped[str | None] = mapped_column(String(64), nullable=True)

    downloads: Mapped[list[ImageDownload]] = relationship(back_populates="device")
    schedules: Mapped[list[DeviceSchedule]] = relationship(
        back_populates="device", cascade="all, delete-orphan"
    )


class DeviceSchedule(Base):
    __tablename__ = "device_schedules"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id_fk: Mapped[int] = mapped_column(ForeignKey("devices.id"))
    name: Mapped[str] = mapped_column(String(64))
    cron: Mapped[str] = mapped_column(String(64))
    active: Mapped[bool] = mapped_column(Boolean, default=True)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)

    device: Mapped[Device] = relationship(back_populates="schedules")


class ImageAsset(Base):
    __tablename__ = "image_assets"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    asset_id: Mapped[str] = mapped_column(String(64), unique=True, index=True)
    filename: Mapped[str | None] = mapped_column(String(256), nullable=True)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)

    __table_args__ = (UniqueConstraint("asset_id", name="uq_image_assets_asset_id"),)


class ImageDownload(Base):
    __tablename__ = "image_downloads"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id_fk: Mapped[int] = mapped_column(ForeignKey("devices.id"))
    asset_id: Mapped[str] = mapped_column(String(64))
    downloaded_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)

    device: Mapped[Device] = relationship(back_populates="downloads")
