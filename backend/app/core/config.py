"""Application configuration — loaded from environment / .env file."""
from functools import lru_cache
from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    # ── Application ──────────────────────────────────────────────
    debug: bool = False
    allowed_origins_raw: str = Field(default="http://localhost:3000", validation_alias="allowed_origins")

    @property
    def allowed_origins(self) -> list[str]:
        return [x.strip() for x in self.allowed_origins_raw.split(",") if x.strip()]

    # ── Database ─────────────────────────────────────────────────
    database_url: str  # postgresql+asyncpg://user:pass@host:port/db

    # ── Redis ────────────────────────────────────────────────────
    redis_url: str  # redis://...  or rediss://...

    # ── MQTT ─────────────────────────────────────────────────────
    mqtt_host: str = "localhost"
    mqtt_port: int = 1883
    mqtt_username: str = ""
    mqtt_password: str = ""
    mqtt_tls: bool = False

    # ── Email ────────────────────────────────────────────────────
    email_provider: str = "smtp"  # smtp | sendgrid | ses | resend
    email_address: str = ""
    email_app_password: str = ""
    smtp_server: str = "smtp.gmail.com"
    smtp_port: int = 587

    # ── Auth ──────────────────────────────────────────────────────
    session_ttl_seconds: int = 3600          # 1 hour
    proximity_code_ttl_seconds: int = 120    # 2 minutes
    proximity_flag_ttl_seconds: int = 300    # 5 minutes
    login_max_attempts: int = 5
    login_lockout_seconds: int = 900         # 15 minutes
    totp_max_attempts: int = 5
    totp_lockout_seconds: int = 300          # 5 minutes

    # ── Notification schedule ─────────────────────────────────────
    reminder_before_due_minutes: int = 30
    escalation_after_due_hours: int = 2

    # ── Possession window ─────────────────────────────────────────
    default_possession_hours: int = 6

    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8", extra="ignore")


@lru_cache
def get_settings() -> Settings:
    return Settings()
