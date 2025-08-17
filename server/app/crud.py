from __future__ import annotations
from datetime import datetime, timedelta, timezone as dt_timezone
from sqlalchemy import select, func
from sqlalchemy.ext.asyncio import AsyncSession
from .models import Device, ImageDownload, DeviceSchedule
from .config import settings
from croniter import croniter


async def upsert_device(
    session: AsyncSession,
    device_id: str,
    name: str | None,
    location: str | None,
    timezone: str | None,
) -> Device:
    res = await session.execute(select(Device).where(Device.device_id == device_id))
    dev = res.scalar_one_or_none()
    if dev is None:
        dev = Device(
            device_id=device_id,
            name=name,
            location=location,
            timezone=timezone,
            default_wake_time=settings.default_wake_time,
            min_days_before_repeat=settings.min_days_before_repeat,
        )
        session.add(dev)
    else:
        dev.name = name or dev.name
        dev.location = location or dev.location
        dev.timezone = timezone or dev.timezone
    dev.last_seen_at = datetime.utcnow()
    await session.commit()
    await session.refresh(dev)
    return dev


async def mark_image_download(
    session: AsyncSession, device: Device, asset_id: str
) -> None:
    dl = ImageDownload(device_id_fk=device.id, asset_id=asset_id)
    session.add(dl)
    device.last_image_asset_id = asset_id
    await session.commit()


async def get_recent_asset_ids(session: AsyncSession, device: Device) -> set[str]:
    cutoff = datetime.utcnow() - timedelta(days=device.min_days_before_repeat)
    res = await session.execute(
        select(ImageDownload.asset_id).where(
            ImageDownload.device_id_fk == device.id,
            ImageDownload.downloaded_at >= cutoff,
        )
    )
    return {row[0] for row in res.fetchall()}


async def get_device_by_device_id(
    session: AsyncSession, device_id: str
) -> Device | None:
    res = await session.execute(select(Device).where(Device.device_id == device_id))
    return res.scalar_one_or_none()


async def list_schedules(session: AsyncSession, device: Device) -> list[DeviceSchedule]:
    res = await session.execute(
        select(DeviceSchedule).where(DeviceSchedule.device_id_fk == device.id)
    )
    return list(res.scalars().all())


async def add_schedule(
    session: AsyncSession, device: Device, name: str, cron: str, active: bool = True
) -> DeviceSchedule:
    sched = DeviceSchedule(device_id_fk=device.id, name=name, cron=cron, active=active)
    session.add(sched)
    await session.commit()
    await session.refresh(sched)
    return sched


async def delete_schedule(
    session: AsyncSession, device: Device, schedule_id: int
) -> bool:
    res = await session.execute(
        select(DeviceSchedule).where(
            DeviceSchedule.id == schedule_id, DeviceSchedule.device_id_fk == device.id
        )
    )
    sched = res.scalar_one_or_none()
    if not sched:
        return False
    await session.delete(sched)
    await session.commit()
    return True


def _next_from_crons(crons: list[str], now_ts: int) -> int | None:
    if not crons:
        return None
    now_dt = datetime.fromtimestamp(now_ts, tz=dt_timezone.utc)
    next_times: list[int] = []
    for c in crons:
        try:
            it = croniter(c, now_dt)
            n = it.get_next(datetime)
            next_times.append(int(n.replace(tzinfo=dt_timezone.utc).timestamp()))
        except Exception:
            continue
    return min(next_times) if next_times else None


async def compute_next_wake_epoch(session: AsyncSession, device: Device) -> int:
    # Collect active schedules for the device
    schedules = await list_schedules(session, device)
    crons = [s.cron for s in schedules if s.active]
    now_ts = int(datetime.utcnow().replace(tzinfo=dt_timezone.utc).timestamp())
    nxt = _next_from_crons(crons, now_ts)
    if nxt is not None:
        return nxt
    # Fallback to daily default time (UTC naive): today/tomorrow at HH:MM
    try:
        hh, mm = device.default_wake_time.split(":")
        today = datetime.utcnow().replace(
            hour=int(hh), minute=int(mm), second=0, microsecond=0
        )
        if today.timestamp() <= now_ts:
            today = today + timedelta(days=1)
        return int(today.timestamp())
    except Exception:
        return now_ts + 6 * 60 * 60  # 6h fallback
