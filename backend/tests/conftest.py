"""Pytest conftest — set dummy env vars so Settings validates without a real .env."""
import os

# Set all required env vars before any app import happens
os.environ.setdefault("DATABASE_URL", "postgresql+asyncpg://test:test@localhost:5432/skss_test")
os.environ.setdefault("REDIS_URL", "redis://localhost:6379/0")
os.environ.setdefault("EMAIL_ADDRESS", "test@example.com")
os.environ.setdefault("EMAIL_APP_PASSWORD", "test-password")
os.environ.setdefault("MQTT_HOST", "localhost")
os.environ.setdefault("TOTP_ENCRYPTION_KEY", "ZMJX4m41EmrY1gq3LtwEBGHmg4iPVc6PuQKkx1NtF4Y=")

# Pydantic still reads backend/.env when it exists, and a real dev .env carries
# DEBUG=true with the TOTP bypass armed — which makes verify_totp() accept any
# code and fails test_invalid_totp_code. Environment variables outrank the .env
# file, so pin both here rather than depending on what the developer has locally.
os.environ["DEBUG"] = "false"
os.environ["TOTP_DEMO_BYPASS_CODE"] = ""
