from __future__ import annotations
import asyncio
from typing import Any
import httpx
from .config import settings


class ImmichClient:
    def __init__(self, base_url: str, api_key: str):
        self._base = base_url.rstrip("/")
        self._api_key = api_key
        self._headers = {"x-api-key": api_key}

    async def list_album_assets(self, album_id: str) -> list[dict[str, Any]]:
        url = f"{self._base}/api/albums/{album_id}"
        async with httpx.AsyncClient(timeout=30) as client:
            resp = await client.get(url, headers=self._headers)
            resp.raise_for_status()
            data = resp.json()
            assets = data.get("assets", [])
            return assets

    async def get_asset_bytes(self, asset_id: str) -> bytes:
        url = f"{self._base}/api/assets/{asset_id}/original"
        async with httpx.AsyncClient(timeout=None) as client:
            resp = await client.get(url, headers=self._headers)
            resp.raise_for_status()
            return resp.content


immich = ImmichClient(settings.immich_base_url, settings.immich_api_key)
