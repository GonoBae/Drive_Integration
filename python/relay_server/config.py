from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    # ZMQ
    zmq_host: str = "127.0.0.1"
    zmq_port: int = 5555

    # PostgreSQL + TimescaleDB
    db_host: str = "localhost"
    db_port: int = 5432
    db_name: str = "simcore"
    db_user: str = "postgres"
    db_password: str = "postgres"

    @property
    def db_url(self) -> str:
        return f"postgresql://{self.db_user}:{self.db_password}@{self.db_host}:{self.db_port}/{self.db_name}"

    # DB 저장 여부
    db_enabled: bool = False

    # Server
    host: str = "0.0.0.0"
    port: int = 8000

    class Config:
        env_file = ".env"


settings = Settings()
