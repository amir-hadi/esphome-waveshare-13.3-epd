from __future__ import annotations
import asyncio
from fastapi import FastAPI
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import text
from .db import get_engine, Base
from .routers import devices, images

app = FastAPI(title="EPD Backend")


@app.on_event("startup")
async def on_startup() -> None:
    # Create tables if not existing
    engine = get_engine()
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)


app.include_router(devices.router)
app.include_router(images.router)


@app.get("/")
async def root():
    return {"ok": True, "service": "epd-backend"}
