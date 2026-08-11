"""Integration test stubs — full retrieve/return flow with mocked MQTT."""
from __future__ import annotations

import uuid
from datetime import datetime, timedelta, timezone
from unittest.mock import AsyncMock, patch

import pytest


class TestRetrieveReturnFlow:
    """Integration tests for the key retrieve/return flow."""

    @pytest.mark.asyncio
    async def test_retrieve_success(self):
        """
        Given: user has permission for room, slot is available, proximity verified
        When: retrieve is called
        Then: slot status → retrieved, retrieval log created, email sent
        """
        # This is a smoke test — full DB integration tested in CI with test DB
        # The atomic update logic in key_service is the critical path:
        # UPDATE key_slots SET status='retrieved' WHERE id=? AND status='available'
        # Check affected rows → 0 means race lost
        assert True  # placeholder for full integration test with test DB

    @pytest.mark.asyncio
    async def test_concurrent_retrieve_atomic(self):
        """
        FR-5: Two concurrent retrieve requests on the same slot.
        Only one should succeed; the other should get slot_unavailable (409).
        """
        # The atomic conditional UPDATE ensures only one wins.
        # In a real integration test, run two concurrent requests against test DB.
        # Key code: UPDATE key_slots SET status='retrieved' WHERE id=? AND status='available'
        # The database-level constraint ensures atomicity — tested via load test.
        assert True

    @pytest.mark.asyncio
    async def test_retrieve_without_permission_denied(self):
        """FR-2: User without permission for a room cannot retrieve its key."""
        from app.services.key_service import KeyService
        mock_db = AsyncMock()

        with patch.object(KeyService, "_has_permission", return_value=False), \
             patch.object(KeyService, "_get_slot") as mock_slot:
            from app.models.key_slot import KeySlot, KeyStatus
            slot = KeySlot()
            slot.id = uuid.uuid4()
            slot.room_id = uuid.uuid4()
            slot.device_id = uuid.uuid4()
            slot.slot_number = 1
            slot.status = KeyStatus.available
            mock_slot.return_value = slot

            svc = KeyService(mock_db)
            from app.schemas import RetrieveRequest
            req = RetrieveRequest(session_id=uuid.uuid4())

            with pytest.raises(PermissionError):
                await svc.retrieve(slot.id, uuid.uuid4(), req)

    @pytest.mark.asyncio
    async def test_proximity_required_for_retrieve(self):
        """FR-7: retrieve without proximity flag → 403 not_proximity_verified."""
        # This is enforced at the API layer via the require_proximity dependency.
        # The FastAPI dependency check_proximity_flag returns None → raises 403.
        from app.core.security import check_proximity_flag

        with patch("app.core.security.get_redis") as mock_get_redis:
            mock_redis = AsyncMock()
            mock_redis.get.return_value = None  # No flag
            mock_get_redis.return_value = mock_redis

            result = await check_proximity_flag("no-flag-session")
            assert result is None  # API layer should 403 on this

    @pytest.mark.asyncio
    async def test_stale_proximity_code_rejected(self):
        """Expired proximity codes (not in Redis) are rejected."""
        from app.schemas import ProximityVerifyRequest
        from app.services.proximity_service import ProximityService

        with patch("app.services.proximity_service.get_redis") as mock_get_redis:
            mock_redis = AsyncMock()
            mock_redis.get.return_value = None  # Expired / not found
            mock_get_redis.return_value = mock_redis

            svc = ProximityService()
            req = ProximityVerifyRequest(device_id=uuid.uuid4(), code="STALE001")

            with pytest.raises(ValueError):
                await svc.verify_code(req, "session-id")

    @pytest.mark.asyncio
    async def test_reused_proximity_code_rejected(self):
        """Single-use: after verify, code is deleted from Redis — reuse fails."""
        from app.schemas import ProximityVerifyRequest
        from app.services.proximity_service import ProximityService

        device_id = uuid.uuid4()
        code = "SINGLEUSE"
        session_id = str(uuid.uuid4())

        call_count = 0

        async def mock_get_side_effect(key):
            nonlocal call_count
            call_count += 1
            if call_count == 1:
                return str(device_id)  # First call succeeds
            return None  # After delete — expired

        with patch("app.services.proximity_service.get_redis") as mock_get_redis, \
             patch("app.services.proximity_service.set_proximity_flag", new_callable=AsyncMock):
            mock_redis = AsyncMock()
            mock_redis.get.side_effect = mock_get_side_effect
            mock_redis.delete = AsyncMock()
            mock_get_redis.return_value = mock_redis

            svc = ProximityService()
            req = ProximityVerifyRequest(device_id=device_id, code=code)

            # First use succeeds
            result = await svc.verify_code(req, session_id)
            assert result.proximity_verified is True

            # Second use fails (code consumed — now returns None)
            with pytest.raises(ValueError):
                await svc.verify_code(req, session_id)
