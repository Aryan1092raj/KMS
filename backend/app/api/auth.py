"""Auth router — login, TOTP, setup, logout."""
from typing import Annotated

from fastapi import APIRouter, Cookie, Depends, HTTPException, Response, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user
from app.core.database import get_db
from app.models.user import User
from app.schemas import (
    LoginRequest,
    SessionResponse,
    TOTPSetupResponse,
    TOTPVerifyRequest,
)
from app.services.auth_service import AuthService

router = APIRouter(prefix="/auth", tags=["auth"])


@router.post("/login")
async def login(req: LoginRequest, db: AsyncSession = Depends(get_db)) -> dict:
    """Step 1: credentials → returns temp_token for TOTP challenge."""
    try:
        return await AuthService(db).login(req)
    except (ValueError, PermissionError) as e:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail=str(e))


@router.post("/totp/verify", response_model=SessionResponse)
async def verify_totp(
    req: TOTPVerifyRequest, response: Response, db: AsyncSession = Depends(get_db)
) -> SessionResponse:
    """Step 2: TOTP → issues session_id cookie."""
    try:
        result = await AuthService(db).verify_totp(req)
        # Set HttpOnly session cookie
        response.set_cookie(
            "session_id",
            result.session_id,
            httponly=True,
            samesite="lax",
            secure=True,
            max_age=3600,
        )
        return result
    except (ValueError, PermissionError) as e:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail=str(e))


@router.post("/totp/setup", response_model=TOTPSetupResponse)
async def setup_totp(
    user: Annotated[User, Depends(get_current_user)],
    db: AsyncSession = Depends(get_db),
) -> TOTPSetupResponse:
    """Enroll TOTP — returns QR URI and secret (shown once)."""
    return await AuthService(db).setup_totp(user.id)


@router.post("/logout", status_code=status.HTTP_204_NO_CONTENT)
async def logout(
    response: Response,
    session_id: str | None = Cookie(default=None),
    db: AsyncSession = Depends(get_db),
) -> None:
    if session_id:
        await AuthService(db).logout(session_id)
    response.delete_cookie("session_id")
