from __future__ import annotations
from pydantic import BaseModel, Field
from typing import Optional


class RegisterDeviceIn(BaseModel):
    device_id: str
    name: Optional[str] = None
    location: Optional[str] = None
    timezone: Optional[str] = None


class DeviceOut(BaseModel):
    device_id: str
    name: Optional[str] = None
    location: Optional[str] = None
    timezone: Optional[str] = None
    default_wake_time: str
    min_days_before_repeat: int

    class Config:
        from_attributes = True


class ScheduleIn(BaseModel):
    name: str
    cron: str
    active: bool = True


class ScheduleOut(BaseModel):
    id: int
    name: str
    cron: str
    active: bool

    class Config:
        from_attributes = True


class ConfigOut(BaseModel):
    server_base_url: str
    image_url: str
    wake_time: str
    next_wake_epoch: int
    panel_width: int
    panel_height: int
    min_days_before_repeat: int
