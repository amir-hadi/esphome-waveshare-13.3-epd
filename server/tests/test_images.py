import pytest
from httpx import AsyncClient
from PIL import Image
from io import BytesIO
import app.immich as immich_mod


def _dummy_image_bytes(w: int = 1600, h: int = 1200) -> bytes:
    img = Image.new("L", (w, h), color=128)
    buf = BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


@pytest.mark.asyncio
async def test_next_image_range(client: AsyncClient, monkeypatch):
    # Stub immich list and fetch
    async def fake_list(album_id: str):
        return [{"id": "asset-1"}]

    async def fake_get(asset_id: str):
        return _dummy_image_bytes()

    monkeypatch.setattr(immich_mod.immich, "list_album_assets", fake_list)
    monkeypatch.setattr(immich_mod.immich, "get_asset_bytes", fake_get)

    # Register device
    await client.post("/devices/register", json={"device_id": "dev-img"})

    # Full download
    r = await client.get("/images/next", params={"device_id": "dev-img"})
    assert r.status_code == 200
    total = len(r.content)

    # Range request
    headers = {"Range": f"bytes=0-{min(4095, total-1)}"}
    r = await client.get(
        "/images/next", params={"device_id": "dev-img"}, headers=headers
    )
    assert r.status_code == 206
    assert r.headers.get("Content-Range") is not None
    assert len(r.content) == min(4096, total)
