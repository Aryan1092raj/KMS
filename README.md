# Smart Key Storage System (SNTC)

**IIT Mandi SNTC — Self-service key management with 2FA, proximity gating, and full audit trail.**

## Architecture

```
frontend/     Next.js 14 — user portal + admin panel
backend/      FastAPI (Python 3.12) — REST API + MQTT worker + scheduler
```

**Infrastructure:**
- PostgreSQL (Supabase) — primary data store
- Redis (Upstash) — sessions, proximity codes, rate limiting, live pub/sub
- MQTT (HiveMQ Cloud / Mosquitto) — ESP32-S3 device communication
- SMTP — email notifications (provider-pattern, pluggable)

## Quick Start

### Local Development (Docker Compose)

```bash
# Clone and setup
git clone <repo>
cd KMS
make setup       # copies .env.example, installs deps

# Edit backend/.env with your credentials
nano backend/.env

# Start everything (Mosquitto + Redis + FastAPI + Next.js)
make dev
```

App runs at:
- **Frontend:** http://localhost:3000
- **Backend API:** http://localhost:8000
- **API Docs:** http://localhost:8000/docs

### Manual Setup (without Docker)

```bash
# Backend
cd backend
pip install -r requirements.txt
cp .env.example .env   # fill in your values
alembic upgrade head   # create DB tables
uvicorn app.main:app --reload

# Frontend (separate terminal)
cd frontend
npm install
npm run dev
```

## Database Migration

```bash
# Create a new migration (after model changes)
make migrate-new MSG="add_new_table"

# Apply all pending migrations
make migrate
```

## Running Tests

```bash
make test         # all tests
make test-unit    # unit tests only
```

## Environment Variables

See [`backend/.env.example`](backend/.env.example) for all required variables.

Key variables:
| Variable | Description |
|---|---|
| `DATABASE_URL` | `postgresql+asyncpg://...` (Supabase) |
| `REDIS_URL` | `rediss://...` (Upstash) |
| `MQTT_HOST` | MQTT broker hostname |
| `EMAIL_PROVIDER` | `smtp` |
| `EMAIL_ADDRESS` | Sender email address |
| `EMAIL_APP_PASSWORD` | SMTP app password |
| `SECRET_KEY` | Random secret for sessions |

## API Documentation

- **Interactive docs:** http://localhost:8000/docs (Swagger UI)
- **ReDoc:** http://localhost:8000/redoc

Full API reference in [`TRD.md`](TRD.md).

## Project Structure

```
backend/
  app/
    api/          FastAPI routers (auth, keys, proximity, sessions, admin)
    core/         Config, DB, Redis, security utils
    email/        Provider-pattern email (abstract + SMTP + factory + templates)
    models/       SQLAlchemy ORM models (10 tables)
    schemas/      Pydantic v2 request/response schemas
    services/     Business logic (auth, key, proximity, admin, notification, mqtt)
    workers/      MQTT listener + APScheduler notification jobs
  alembic/        DB migrations
  tests/          Unit + integration tests

frontend/
  app/            Next.js 14 App Router pages
    login/        Credential login + TOTP verification
    totp/         TOTP setup (QR code enrollment)
    connect/      Proximity code exchange (captive portal redirect)
    keys/         Key status grid with retrieve/return/extend
    admin/        Admin panel (dashboard, users, rooms, permissions, logs, devices, reports)
  components/     Reusable React components
  lib/api.ts      Typed API client
```

## Authentication Flow

Proximity verification needs a session, so login comes first. The code proves
physical presence because it is only obtainable inside the enclosure's radio range.

1. User joins the enclosure WiFi → captive portal shows a short-lived code
2. User rejoins their normal network (the enclosure AP has no internet route)
3. `POST /auth/login` → temp token
4. `POST /auth/totp/setup` (first time only, authorised by the temp token) → QR + secret
5. `POST /auth/totp/verify` → session cookie
6. `/connect` calls `POST /proximity/verify` with the code → sets 5-minute proximity flag
7. `POST /sessions/start` → unlocks door (proximity-gated)
8. `POST /keys/{slot}/retrieve` → dispenses key (proximity-gated + permission-checked)

An already-signed-in user on the enclosure WiFi can be redirected straight to
`/connect?device_id=X&code=Y`, which skips the manual code entry at step 6.

Step-by-step walkthrough, Google Authenticator enrollment, and the ESP32 firmware
contract: [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md).

## Notification Schedule

| Event | Timing | Recipient |
|---|---|---|
| Retrieval confirmation | Immediate | Member |
| Return reminder | T-30min before due | Member |
| Overdue warning | At due time | Member |
| Coordinator escalation | T+2h past due | Coordinator |
| Tamper alert | Immediate | All admins |

## Security

- 2FA: bcrypt password + TOTP (RFC 6238, ±1 step)
- Sessions: server-side, Redis-backed, immediately revocable
- Proximity gate: FR-7 — retrieve/return/start-session require fresh proximity flag
- Rate limiting: 5 login failures/15min, 5 TOTP failures/5min
- Nonce replay protection: all MQTT unlock commands use single-use nonces (FR-3)
- RBAC: admin sees all; coordinator sees own rooms; member sees only their keys

## Deployment

| Component | Platform |
|---|---|
| FastAPI backend | Railway / Fly.io |
| PostgreSQL | Supabase |
| Redis | Upstash |
| Next.js | Vercel |
| MQTT | HiveMQ Cloud (free tier) or self-hosted Mosquitto |

See [`architecture.md`](architecture.md) for full deployment details.
