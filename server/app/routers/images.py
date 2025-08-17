from __future__ import annotations
from typing import Optional
from fastapi import APIRouter, Depends, HTTPException, Request, Response
from fastapi.responses import StreamingResponse
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select
from ..db import get_db
from ..crud import get_device_by_device_id, get_recent_asset_ids, mark_image_download
from ..immich import immich
from ..config import settings
from ..image_proc import center_crop_resize_to_panel, pack_grayscale_4bpp

router = APIRouter(prefix="/images", tags=["images"])


async def select_next_asset_id(db: AsyncSession, device_id: str) -> str:
    dev = await get_device_by_device_id(db, device_id)
    if dev is None:
        raise HTTPException(status_code=404, detail="device not found")
    assets = await immich.list_album_assets(settings.immich_album_id)
    if not assets:
        raise HTTPException(status_code=404, detail="album empty")
    recent = await get_recent_asset_ids(db, dev)
    for asset in assets:
        asset_id = asset.get("id") or asset.get("assetId") or asset.get("asset_id")
        if not asset_id:
            continue
        if asset_id in recent:
            continue
        return asset_id
    # If all recent, fall back to the first
    fallback = (
        assets[0].get("id") or assets[0].get("assetId") or assets[0].get("asset_id")
    )
    if not fallback:
        raise HTTPException(status_code=500, detail="invalid asset")
    return fallback


@router.get("/next")
async def next_image(
    request: Request, device_id: str, db: AsyncSession = Depends(get_db)
):
    dev = await get_device_by_device_id(db, device_id)
    if dev is None:
        raise HTTPException(status_code=404, detail="device not found")
    asset_id = await select_next_asset_id(db, device_id)
    binary = await immich.get_asset_bytes(asset_id)
    img = center_crop_resize_to_panel(
        binary, settings.panel_width, settings.panel_height
    )
    packed = pack_grayscale_4bpp(img)

    # Persist that this device got this asset now
    await mark_image_download(db, dev, asset_id)

    # Implement Range support (bytes= start-end)
    range_header: Optional[str] = request.headers.get("range") or request.headers.get(
        "Range"
    )
    total_size = len(packed)
    start = 0
    end = total_size - 1
    status_code = 200
    headers = {
        "Accept-Ranges": "bytes",
        "Content-Type": "application/octet-stream",
        "Content-Length": str(total_size),
    }
    if range_header and range_header.startswith("bytes="):
        try:
            range_spec = range_header.split("=", 1)[1]
            s, e = range_spec.split("-")
            start = int(s) if s else 0
            end = int(e) if e else total_size - 1
            if start < 0 or end < start or end >= total_size:
                raise ValueError("bad range")
            status_code = 206
            headers["Content-Range"] = f"bytes {start}-{end}/{total_size}"
            headers["Content-Length"] = str(end - start + 1)
        except Exception:
            raise HTTPException(status_code=416, detail="invalid range")

    chunk = memoryview(packed)[start : end + 1]
    return Response(
        content=chunk.tobytes(),
        media_type="application/octet-stream",
        status_code=status_code,
        headers=headers,
    )
