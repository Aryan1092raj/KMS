"""Pytest conftest — set dummy env vars so Settings validates without a real .env."""
import os

# Set all required env vars before any app import happens
os.environ.setdefault("DATABASE_URL", "postgresql+asyncpg://test:test@localhost:5432/skss_test")
os.environ.setdefault("REDIS_URL", "redis://localhost:6379/0")
os.environ.setdefault("EMAIL_ADDRESS", "test@example.com")
os.environ.setdefault("EMAIL_APP_PASSWORD", "test-password")
os.environ.setdefault("MQTT_HOST", "localhost")
