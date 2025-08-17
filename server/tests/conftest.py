import asyncio
import os
import pytest
from typing import AsyncIterator
from httpx import AsyncClient
from sqlalchemy.ext.asyncio import create_async_engine, AsyncSession
from sqlalchemy.orm import sessionmaker

# Ensure required env vars are present for settings
os.environ.setdefault("IMMICH_BASE_URL", "http://immich.test")
os.environ.setdefault("IMMICH_API_KEY", "test-key")
os.environ.setdefault("IMMICH_ALBUM_ID", "album-1")

from app.db import Base
from app.main import app
import app.db as app_db


@pytest.fixture(scope="session")
def anyio_backend():
    return "asyncio"


@pytest.fixture(scope="session")
async def test_engine():
    engine = create_async_engine(
        "sqlite+aiosqlite:///:memory:", echo=False, future=True
    )
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    try:
        yield engine
    finally:
        await engine.dispose()


@pytest.fixture()
async def test_sessionmaker(test_engine):
    return sessionmaker(bind=test_engine, class_=AsyncSession, expire_on_commit=False)


@pytest.fixture(autouse=True)
async def _patch_engine(monkeypatch, test_engine):
    # Ensure app uses the test engine for startup and sessions
    monkeypatch.setattr(app_db, "get_engine", lambda: test_engine)
    yield


@pytest.fixture()
async def client(test_sessionmaker) -> AsyncIterator[AsyncClient]:
    # Override dependency that yields DB session
    async def override_get_db():
        async with test_sessionmaker() as session:  # type: ignore[call-arg]
            yield session

    app.dependency_overrides[app_db.get_db] = override_get_db

    async with AsyncClient(app=app, base_url="http://testserver") as ac:
        yield ac

    app.dependency_overrides.clear()
