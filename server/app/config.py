from pydantic_settings import BaseSettings, SettingsConfigDict
from pydantic import Field


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env", env_file_encoding="utf-8", extra="ignore"
    )

    server_base_url: str = Field("http://localhost:8000", alias="SERVER_BASE_URL")
    db_url: str = Field("sqlite+aiosqlite:///./data/epd.db", alias="DB_URL")
    default_wake_time: str = Field("03:00", alias="DEFAULT_WAKE_TIME")
    panel_width: int = Field(1600, alias="PANEL_WIDTH")
    panel_height: int = Field(1200, alias="PANEL_HEIGHT")
    min_days_before_repeat: int = Field(7, alias="MIN_DAYS_BEFORE_REPEAT")

    immich_base_url: str = Field(..., alias="IMMICH_BASE_URL")
    immich_api_key: str = Field(..., alias="IMMICH_API_KEY")
    immich_album_id: str = Field(..., alias="IMMICH_ALBUM_ID")


settings = Settings()  # type: ignore[arg-type]
