from pathlib import Path

from sqlalchemy import create_engine, event
from sqlalchemy.engine import make_url
from sqlalchemy.orm import DeclarativeBase, sessionmaker

from .config import BASE_DIR, settings


class Base(DeclarativeBase):
    pass


def build_engine():
    url = make_url(settings.database_url)

    if url.drivername in ("postgres", "postgresql"):
        url = url.set(drivername="postgresql+psycopg")

    if url.get_backend_name() == "sqlite":
        database = url.database
        if database and database != ":memory:":
            path = Path(database)
            if not path.is_absolute():
                path = (BASE_DIR / path).resolve()
            path.parent.mkdir(parents=True, exist_ok=True)
            url = url.set(database=str(path))

        engine = create_engine(
            url,
            connect_args={"check_same_thread": False, "timeout": 30},
            pool_pre_ping=True,
        )

        @event.listens_for(engine, "connect")
        def set_sqlite_pragmas(dbapi_connection, _connection_record):
            cursor = dbapi_connection.cursor()
            cursor.execute("PRAGMA journal_mode=WAL")
            cursor.execute("PRAGMA busy_timeout=5000")
            cursor.execute("PRAGMA foreign_keys=ON")
            cursor.close()

        return engine

    return create_engine(url, pool_pre_ping=True)


engine = build_engine()
SessionLocal = sessionmaker(
    bind=engine,
    autoflush=False,
    autocommit=False,
    expire_on_commit=False,
)


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def init_db():
    import importlib

    importlib.import_module(".models", __package__)
    Base.metadata.create_all(bind=engine)
