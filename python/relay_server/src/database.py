import asyncpg
from datetime import datetime, timezone

from config import settings

_pool: asyncpg.Pool | None = None


async def init_db() -> None:
    global _pool
    _pool = await asyncpg.create_pool(settings.db_url, min_size=2, max_size=10)
    await _create_tables()


async def close_db() -> None:
    if _pool:
        await _pool.close()


async def _create_tables() -> None:
    async with _pool.acquire() as conn:
        # TimescaleDB 확장 활성화
        await conn.execute("CREATE EXTENSION IF NOT EXISTS timescaledb;")

        # 엔티티 상태 테이블
        await conn.execute("""
            CREATE TABLE IF NOT EXISTS entity_states (
                time        TIMESTAMPTZ     NOT NULL,
                entity_id   INTEGER         NOT NULL,
                lat         DOUBLE PRECISION NOT NULL,
                lon         DOUBLE PRECISION NOT NULL,
                alt         DOUBLE PRECISION NOT NULL,
                heading     REAL            NOT NULL,
                pitch       REAL            NOT NULL,
                roll        REAL            NOT NULL,
                speed       REAL            NOT NULL,
                accel       REAL            NOT NULL,
                fuel        REAL            NOT NULL,
                rpm         REAL            NOT NULL
            );
        """)

        # TimescaleDB 하이퍼테이블 (이미 존재하면 무시)
        await conn.execute("""
            SELECT create_hypertable(
                'entity_states', 'time',
                if_not_exists => TRUE
            );
        """)

        # entity_id + time 인덱스 (엔티티별 시계열 조회 최적화)
        await conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_entity_states_entity_time
            ON entity_states (entity_id, time DESC);
        """)


async def insert_entity_state(state) -> None:
    """단일 EntityState 저장"""
    async with _pool.acquire() as conn:
        await conn.execute(
            """
            INSERT INTO entity_states
                (time, entity_id, lat, lon, alt, heading, pitch, roll, speed, accel, fuel, rpm)
            VALUES
                ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)
            """,
            datetime.fromtimestamp(state.timestamp, tz=timezone.utc),
            state.entity_id,
            state.lat,
            state.lon,
            state.alt,
            state.heading,
            state.pitch,
            state.roll,
            state.speed,
            state.accel,
            state.fuel,
            state.rpm,
        )


async def insert_entity_states_batch(states: list) -> None:
    """여러 EntityState 배치 저장 (멀티 엔티티 패킷용)"""
    records = [
        (
            datetime.fromtimestamp(s.timestamp, tz=timezone.utc),
            s.entity_id,
            s.lat, s.lon, s.alt,
            s.heading, s.pitch, s.roll,
            s.speed, s.accel,
            s.fuel, s.rpm,
        )
        for s in states
    ]
    async with _pool.acquire() as conn:
        await conn.executemany(
            """
            INSERT INTO entity_states
                (time, entity_id, lat, lon, alt, heading, pitch, roll, speed, accel, fuel, rpm)
            VALUES
                ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)
            """,
            records,
        )
