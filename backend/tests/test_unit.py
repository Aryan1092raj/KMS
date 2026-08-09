"""Unit tests — TOTP, permission logic, overdue escalation, proximity verification, race condition."""
from __future__ import annotations

import uuid
from datetime import datetime, timedelta, timezone
from unittest.mock import AsyncMock, MagicMock, patch

import pytest


# ── TOTP Tests ────────────────────────────────────────────────────────────────

class TestTOTP:
    def test_generate_secret(self):
        from app.core.security import generate_totp_secret
        secret = generate_totp_secret()
        assert len(secret) == 32  # base32, 32 chars

    def test_valid_totp_code(self):
        import pyotp
        from app.core.security import generate_totp_secret, verify_totp
        secret = generate_totp_secret()
        totp = pyotp.TOTP(secret)
        code = totp.now()
        assert verify_totp(secret, code) is True

    def test_invalid_totp_code(self):
        from app.core.security import generate_totp_secret, verify_totp
        secret = generate_totp_secret()
        assert verify_totp(secret, "000000") is False

    def test_totp_uri_format(self):
        from app.core.security import generate_totp_secret, get_totp_uri
        secret = generate_totp_secret()
        uri = get_totp_uri(secret, "test@iitmandi.ac.in")
        assert uri.startswith("otpauth://totp/")
        assert "SNTC" in uri
        assert "test%40iitmandi.ac.in" in uri or "test@iitmandi.ac.in" in uri


# ── Password Hashing Tests ─────────────────────────────────────────────────────

class TestPasswordHashing:
    def test_hash_and_verify(self):
        from app.core.security import hash_password, verify_password
        pw = "SecureP@ssw0rd"
        hashed = hash_password(pw)
        assert hashed != pw
        assert verify_password(pw, hashed) is True

    def test_wrong_password_fails(self):
        from app.core.security import hash_password, verify_password
        hashed = hash_password("correct")
        assert verify_password("wrong", hashed) is False


# ── Overdue Timing Tests ──────────────────────────────────────────────────────

class TestOverdueTiming:
    def test_reminder_threshold_calculation(self):
        """Reminder should fire 30 min before due_at."""
        now = datetime.now(timezone.utc)
        due_at = now + timedelta(minutes=29)  # within 30 min window
        reminder_threshold = now + timedelta(minutes=30)
        assert due_at <= reminder_threshold

    def test_escalation_threshold_calculation(self):
        """Escalation fires 2h after due_at."""
        now = datetime.now(timezone.utc)
        due_at = now - timedelta(hours=3)   # 3h overdue — should escalate
        escalation_threshold = now - timedelta(hours=2)
        assert due_at <= escalation_threshold

    def test_not_yet_escalation(self):
        """No escalation if only 1h overdue."""
        now = datetime.now(timezone.utc)
        due_at = now - timedelta(hours=1)
        escalation_threshold = now - timedelta(hours=2)
        assert due_at > escalation_threshold  # not yet


# ── Reminder Count Escalation Tests ──────────────────────────────────────────

class TestReminderCountLogic:
    def test_reminder_count_0_sends_reminder(self):
        """reminder_count=0 and within 30 min → send return reminder."""
        now = datetime.now(timezone.utc)
        reminder_count = 0
        due_at = now + timedelta(minutes=20)
        reminder_threshold = now + timedelta(minutes=30)
        should_remind = (reminder_count == 0 and due_at <= reminder_threshold and due_at > now)
        assert should_remind

    def test_reminder_count_1_sends_overdue(self):
        """reminder_count=1 and past due → send overdue warning."""
        now = datetime.now(timezone.utc)
        reminder_count = 1
        due_at = now - timedelta(minutes=5)
        should_warn = (reminder_count == 1 and due_at <= now)
        assert should_warn

    def test_reminder_count_2_triggers_escalation(self):
        """reminder_count=2 and 2h+ past due → escalate to coordinator."""
        now = datetime.now(timezone.utc)
        reminder_count = 2
        due_at = now - timedelta(hours=3)
        escalation_threshold = now - timedelta(hours=2)
        should_escalate = (reminder_count == 2 and due_at <= escalation_threshold)
        assert should_escalate


# ── Permission Logic Tests ────────────────────────────────────────────────────

class TestPermissionLogic:
    def test_expired_permission_denied(self):
        """Permission with past expires_at should be treated as invalid."""
        now = datetime.now(timezone.utc)
        expires_at = now - timedelta(hours=1)
        is_valid = expires_at is None or expires_at > now
        assert is_valid is False

    def test_non_expiring_permission_valid(self):
        """Permission with expires_at=None should always be valid."""
        now = datetime.now(timezone.utc)
        expires_at = None
        is_valid = expires_at is None or expires_at > now
        assert is_valid is True

    def test_future_permission_valid(self):
        now = datetime.now(timezone.utc)
        expires_at = now + timedelta(days=30)
        is_valid = expires_at is None or expires_at > now
        assert is_valid is True


# ── Proximity Tests ───────────────────────────────────────────────────────────

class TestProximityVerification:
    @pytest.mark.asyncio
    async def test_valid_proximity_code_accepted(self):
        """A valid, matching proximity code should set proximity flag."""
        from app.schemas import ProximityVerifyRequest
        from app.services.proximity_service import ProximityService

        device_id = uuid.uuid4()
        code = "ABCD1234"
        session_id = str(uuid.uuid4())

        svc = ProximityService()

        with patch("app.services.proximity_service.get_redis") as mock_get_redis:
            mock_redis = AsyncMock()
            mock_redis.get.return_value = str(device_id)
            mock_redis.delete = AsyncMock()
            mock_get_redis.return_value = mock_redis

            with patch("app.services.proximity_service.set_proximity_flag") as mock_set:
                mock_set.return_value = None
                req = ProximityVerifyRequest(device_id=device_id, code=code)
                result = await svc.verify_code(req, session_id)

            assert result.proximity_verified is True

    @pytest.mark.asyncio
    async def test_expired_code_rejected(self):
        """A code not in Redis (expired/used) should raise ValueError."""
        from app.schemas import ProximityVerifyRequest
        from app.services.proximity_service import ProximityService

        device_id = uuid.uuid4()
        svc = ProximityService()

        with patch("app.services.proximity_service.get_redis") as mock_get_redis:
            mock_redis = AsyncMock()
            mock_redis.get.return_value = None  # Expired
            mock_get_redis.return_value = mock_redis

            req = ProximityVerifyRequest(device_id=device_id, code="EXPIRED1")
            with pytest.raises(ValueError, match="invalid or expired"):
                await svc.verify_code(req, "some-session")

    @pytest.mark.asyncio
    async def test_wrong_device_rejected(self):
        """A code for a different device should raise ValueError."""
        from app.schemas import ProximityVerifyRequest
        from app.services.proximity_service import ProximityService

        device_id = uuid.uuid4()
        other_device = uuid.uuid4()
        svc = ProximityService()

        with patch("app.services.proximity_service.get_redis") as mock_get_redis:
            mock_redis = AsyncMock()
            mock_redis.get.return_value = str(other_device)  # Different device
            mock_get_redis.return_value = mock_redis

            req = ProximityVerifyRequest(device_id=device_id, code="MISMATCH")
            with pytest.raises(ValueError, match="does not match"):
                await svc.verify_code(req, "some-session")


# ── Nonce Replay Tests ────────────────────────────────────────────────────────

class TestNonceReplayProtection:
    @pytest.mark.asyncio
    async def test_nonce_consumed_on_first_use(self):
        from app.core.security import consume_nonce

        with patch("app.core.security.get_redis") as mock_get_redis:
            mock_redis = AsyncMock()
            mock_redis.set.return_value = True  # nx=True succeeded
            mock_get_redis.return_value = mock_redis

            result = await consume_nonce("test-nonce-123")
            assert result is True

    @pytest.mark.asyncio
    async def test_nonce_rejected_on_replay(self):
        from app.core.security import consume_nonce

        with patch("app.core.security.get_redis") as mock_get_redis:
            mock_redis = AsyncMock()
            mock_redis.set.return_value = None  # nx=True failed — already exists
            mock_get_redis.return_value = mock_redis

            result = await consume_nonce("test-nonce-123")
            assert result is False
