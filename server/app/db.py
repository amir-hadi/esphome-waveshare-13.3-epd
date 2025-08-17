from sqlalchemy.ext.asyncio import AsyncEngine, AsyncSession, create_async_engine
from sqlalchemy.orm import sessionmaker, DeclarativeBase
from .config import settings


class Base(DeclarativeBase):
    pass


_engine: AsyncEngine | None = None
_async_sessionmaker: sessionmaker | None = None


def get_engine() -> AsyncEngine:
    global _engine
    if _engine is None:
        _engine = create_async_engine(settings.db_url, echo=False, future=True)
    return _engine


def get_sessionmaker() -> sessionmaker:
    global _async_sessionmaker
    if _async_sessionmaker is None:
        _async_sessionmaker = sessionmaker(
            bind=get_engine(), class_=AsyncSession, expire_on_commit=False
        )
    return _async_sessionmaker


async def get_db() -> AsyncSession:
    async_session = get_sessionmaker()
    async with async_session() as session:  # type: ignore[call-arg]
        yield session
