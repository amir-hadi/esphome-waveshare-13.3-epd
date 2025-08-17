import pytest
from httpx import AsyncClient


@pytest.mark.asyncio
async def test_register_and_fetch_config(client: AsyncClient):
    # Register device
    r = await client.post(
        "/devices/register", json={"device_id": "dev-1", "name": "Test"}
    )
    assert r.status_code == 200
    data = r.json()
    assert data["device_id"] == "dev-1"

    # Initially, no schedules -> fallback to default_wake_time present and next_wake_epoch int
    r = await client.get("/devices/dev-1/config")
    assert r.status_code == 200
    cfg = r.json()
    assert "image_url" in cfg and cfg["image_url"].endswith("device_id=dev-1")
    assert isinstance(cfg["next_wake_epoch"], int)


@pytest.mark.asyncio
async def test_schedules_and_next_wake(client: AsyncClient):
    # Register a device
    r = await client.post("/devices/register", json={"device_id": "dev-2"})
    assert r.status_code == 200

    # Add two schedules
    r = await client.post(
        "/devices/dev-2/schedules",
        json={"name": "every-30m", "cron": "*/30 * * * *", "active": True},
    )
    assert r.status_code == 200

    r = await client.post(
        "/devices/dev-2/schedules",
        json={"name": "daily-3am", "cron": "0 3 * * *", "active": True},
    )
    assert r.status_code == 200

    # List schedules
    r = await client.get("/devices/dev-2/schedules")
    assert r.status_code == 200
    items = r.json()
    assert len(items) == 2

    # Config should include next_wake_epoch based on croniter
    r = await client.get("/devices/dev-2/config")
    assert r.status_code == 200
    cfg = r.json()
    assert isinstance(cfg["next_wake_epoch"], int)


@pytest.mark.asyncio
async def test_delete_schedule(client: AsyncClient):
    await client.post("/devices/register", json={"device_id": "dev-3"})
    r = await client.post(
        "/devices/dev-3/schedules",
        json={"name": "test", "cron": "*/15 * * * *", "active": True},
    )
    assert r.status_code == 200
    sched_id = r.json()["id"]

    r = await client.get("/devices/dev-3/schedules")
    assert any(s["id"] == sched_id for s in r.json())

    r = await client.delete(f"/devices/dev-3/schedules/{sched_id}")
    assert r.status_code == 200

    r = await client.get("/devices/dev-3/schedules")
    assert all(s["id"] != sched_id for s in r.json())
