from pathlib import Path

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


_ENV_FILE = Path(__file__).resolve().with_name(".env")


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=_ENV_FILE,
        env_file_encoding="utf-8",
        extra="ignore",
    )

    # ZMQ
    zmq_host: str = "127.0.0.1"
    zmq_port: int = Field(default=5555, ge=1, le=65535)
    zmq_receive_hwm: int = Field(default=1, ge=1)
    zmq_conflate: bool = True
    zmq_linger_ms: int = Field(default=0, ge=0)
    zmq_error_backoff_seconds: float = Field(default=1.0, gt=0)

    # PostgreSQL + TimescaleDB
    db_host: str = "localhost"
    db_port: int = Field(default=5432, ge=1, le=65535)
    db_name: str = "simcore"
    db_user: str = "postgres"
    db_password: str = "postgres"
    db_write_timeout_seconds: float = Field(default=0.5, gt=0)
    db_error_backoff_seconds: float = Field(default=1.0, gt=0)

    # DB 저장 여부
    db_enabled: bool = False

    # Server
    host: str = "0.0.0.0"
    port: int = Field(default=8000, ge=1, le=65535)
    ws_send_timeout_seconds: float = Field(default=0.1, gt=0)


settings = Settings()
