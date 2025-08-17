from __future__ import annotations
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.ext.asyncio import AsyncSession
from ..db import get_db
from ..schemas import RegisterDeviceIn, DeviceOut, ConfigOut, ScheduleIn, ScheduleOut
from ..crud import (
    upsert_device,
    get_device_by_device_id,
    list_schedules,
    add_schedule,
    delete_schedule,
    compute_next_wake_epoch,
)
from ..config import settings

router = APIRouter(prefix="/devices", tags=["devices"])


@router.post("/register", response_model=DeviceOut)
async def register_device(
    payload: RegisterDeviceIn, db: AsyncSession = Depends(get_db)
):
    dev = await upsert_device(
        db,
        device_id=payload.device_id,
        name=payload.name,
        location=payload.location,
        timezone=payload.timezone,
    )
    return DeviceOut.model_validate(dev)


@router.get("/{device_id}/config", response_model=ConfigOut)
async def get_config(device_id: str, db: AsyncSession = Depends(get_db)):
    dev = await get_device_by_device_id(db, device_id)
    if dev is None:
        raise HTTPException(status_code=404, detail="device not found")
    image_url = f"{settings.server_base_url}/images/next?device_id={device_id}"
    next_epoch = await compute_next_wake_epoch(db, dev)
    return ConfigOut(
        server_base_url=settings.server_base_url,
        image_url=image_url,
        wake_time=dev.default_wake_time,
        next_wake_epoch=next_epoch,
        panel_width=settings.panel_width,
        panel_height=settings.panel_height,
        min_days_before_repeat=dev.min_days_before_repeat,
    )


@router.get("/{device_id}/schedules", response_model=list[ScheduleOut])
async def get_schedules(device_id: str, db: AsyncSession = Depends(get_db)):
    dev = await get_device_by_device_id(db, device_id)
    if dev is None:
        raise HTTPException(status_code=404, detail="device not found")
    items = await list_schedules(db, dev)
    return [ScheduleOut.model_validate(i) for i in items]


@router.post("/{device_id}/schedules", response_model=ScheduleOut)
async def create_schedule(
    device_id: str, payload: ScheduleIn, db: AsyncSession = Depends(get_db)
):
    dev = await get_device_by_device_id(db, device_id)
    if dev is None:
        raise HTTPException(status_code=404, detail="device not found")
    sched = await add_schedule(
        db, dev, name=payload.name, cron=payload.cron, active=payload.active
    )
    return ScheduleOut.model_validate(sched)


@router.delete("/{device_id}/schedules/{schedule_id}")
async def remove_schedule(
    device_id: str, schedule_id: int, db: AsyncSession = Depends(get_db)
):
    dev = await get_device_by_device_id(db, device_id)
    if dev is None:
        raise HTTPException(status_code=404, detail="device not found")
    ok = await delete_schedule(db, dev, schedule_id)
    if not ok:
        raise HTTPException(status_code=404, detail="schedule not found")
    return {"ok": True}
