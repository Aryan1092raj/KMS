# TRD — Smart Key Storage System

**Companion docs:** `architecture.md` (system design), `PRD.md` (product requirements)

## 1. Tech Stack

| Layer | Choice | Notes |
|---|---|---|
| Backend framework | FastAPI (Python 3.12) | async, good fit for MQTT worker + REST together |
| Database | PostgreSQL via Supabase | managed, free tier, PITR available if needed later |
| Cache / pub-sub | Redis via Upstash | sessions, live status push, proximity codes |
| Message broker | MQTT (HiveMQ Cloud free tier, or self-hosted Mosquitto) | device command/telemetry channel |
| Frontend | Next.js 14 | user web app + admin panel (role-gated routes) |
| Firmware | C++ (Arduino framework) or ESP-IDF | on all 3× ESP32-S3 controllers |
| Email delivery | Internal provider-pattern email module, `EMAIL_PROVIDER=smtp` | reuses the existing Factory-based module (same interface style as `send_notes_email()`) |

## 2. Functional Requirements (implementation framing)

- **FR-1:** System MUST verify TOTP within a ±1 step (30s) window before issuing a session token.
- **FR-2:** System MUST reject a retrieve/return request for a slot the requesting user has no active permission for.
- **FR-3:** System MUST issue unlock/dispense MQTT commands with a single-use nonce; a repeated nonce MUST be rejected.
- **FR-4:** System MUST log every login attempt, TOTP attempt, door event, and key event with actor, timestamp, and device_id.
- **FR-5:** System MUST prevent two concurrent retrieve requests from both succeeding on the same slot.
- **FR-6:** System MUST flag outer-box tamper events distinctly and require an admin resolution note before the flag clears.
- **FR-7:** System MUST require a valid, unexpired proximity verification before permitting session-start, retrieve, or return actions.
- **FR-8:** System MUST send a retrieval confirmation email immediately on successful key dispense.
- **FR-9:** System MUST escalate to the room's coordinator after two consecutive missed member-facing notices (reminder + overdue warning) without a return or renewal.
- **FR-10:** System MUST log outer-box tamper events even when no digital authentication occurred, buffering locally if the device is offline at the time.

## 3. Non-Functional Requirements

- Auth: TOTP verification < 500ms p95
- Unlock latency (backend publish → door actuation): < 2s p95 over campus WiFi
- API uptime target: 99.5%
- Proximity codes: 8 characters, single-use, 120s TTL in Redis
- TOTP secrets encrypted at rest, never written to logs
- All admin/coordinator actions on users/permissions are themselves logged (who changed what)

## 4. API Design

| Method | Path | Auth | Description |
|---|---|---|---|
| POST | `/proximity/verify` | device-issued code | validate a code from the enclosure, mark session proximity-verified for 5 min |
| POST | `/auth/login` | none | credentials → triggers TOTP challenge |
| POST | `/auth/totp/verify` | partial | verify TOTP → issues session token |
| POST | `/auth/totp/setup` | user | enroll TOTP, returns QR/secret once |
| POST | `/auth/logout` | user | revoke session |
| POST | `/sessions/start` | user + proximity-verified | begin enclosure session, publishes unlock command |
| POST | `/sessions/{id}/close` | device (internal) | proximity sensor confirms door closed |
| GET | `/keys` | user | list slots + status, permission-filtered (no proximity needed — browsing only) |
| POST | `/keys/{slot_id}/retrieve` | user + proximity-verified | permission check → dispense |
| POST | `/keys/{slot_id}/return` | user + proximity-verified | unlock slot for reinsertion |
| POST | `/keys/{slot_id}/extend` | user | extend due_at (no proximity needed) |
| GET | `/admin/users` | admin | list/search users |
| POST | `/admin/users` | admin | create user |
| PATCH | `/admin/users/{id}` | admin | edit/deactivate |
| POST | `/admin/permissions` | admin or coordinator (own room) | grant room access |
| DELETE | `/admin/permissions/{id}` | admin or coordinator (own room) | revoke |
| GET | `/admin/logs/access` | admin or coordinator (own room) | access log query |
| GET | `/admin/logs/retrieval` | admin or coordinator (own room) | retrieval log query |
| GET | `/admin/logs/override` | admin or coordinator (own room) | tamper/override log query |
| GET | `/admin/devices` | admin | device health list |
| POST | `/admin/devices/{id}/maintenance` | admin | toggle maintenance mode |
| GET | `/admin/reports/usage` | admin or coordinator (own room) | usage analytics |

**Sample — retrieve**

Request `POST /keys/{slot_id}/retrieve`
```json
{ "session_id": "b1e7..." }
```

Response `200`
```json
{
  "slot_id": "3f2a...",
  "status": "retrieved",
  "due_at": "2026-08-07T18:30:00Z",
  "retrieval_log_id": "9c11..."
}
```

Response `403` (no recent proximity verification)
```json
{ "error": "not_proximity_verified" }
```

Response `409` (lost the race / already taken)
```json
{ "error": "slot_unavailable" }
```

## 5. Database Schema

```sql
CREATE TYPE user_role AS ENUM ('member', 'coordinator', 'admin');
CREATE TYPE key_status AS ENUM ('available', 'retrieved', 'maintenance');
CREATE TYPE retrieval_status AS ENUM ('active', 'returned', 'overdue');
CREATE TYPE override_trigger AS ENUM ('physical', 'admin');

CREATE TABLE users (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name text NOT NULL,
    email text UNIQUE NOT NULL,
    roll_no text UNIQUE,
    role user_role NOT NULL DEFAULT 'member',
    password_hash text NOT NULL,
    totp_secret text,               -- encrypted at rest
    totp_enrolled_at timestamptz,
    is_active boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE rooms (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name text NOT NULL,
    block text,
    description text,
    coordinator_id uuid REFERENCES users(id)   -- who "owns" this room's core members
);

CREATE TABLE devices (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    name text NOT NULL,
    location text,
    firmware_version text,
    last_heartbeat_at timestamptz,
    battery_pct smallint,
    on_backup_power boolean DEFAULT false,
    wifi_rssi smallint,
    status text DEFAULT 'offline'
);

CREATE TABLE key_slots (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id uuid NOT NULL REFERENCES devices(id),
    slot_number smallint NOT NULL CHECK (slot_number BETWEEN 1 AND 8),
    room_id uuid REFERENCES rooms(id),
    status key_status NOT NULL DEFAULT 'available',
    UNIQUE (device_id, slot_number)
);

CREATE TABLE permissions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id uuid NOT NULL REFERENCES users(id),
    room_id uuid NOT NULL REFERENCES rooms(id),
    granted_by uuid REFERENCES users(id),   -- coordinator or admin
    granted_at timestamptz NOT NULL DEFAULT now(),
    expires_at timestamptz,
    UNIQUE (user_id, room_id)
);

CREATE TABLE sessions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id uuid NOT NULL REFERENCES users(id),
    device_id uuid NOT NULL REFERENCES devices(id),
    status text NOT NULL DEFAULT 'open',
    opened_at timestamptz NOT NULL DEFAULT now(),
    closed_at timestamptz
);

CREATE TABLE retrieval_logs (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    key_slot_id uuid NOT NULL REFERENCES key_slots(id),
    user_id uuid NOT NULL REFERENCES users(id),
    session_id uuid REFERENCES sessions(id),
    retrieved_at timestamptz NOT NULL DEFAULT now(),
    due_at timestamptz NOT NULL,
    returned_at timestamptz,
    extension_count smallint NOT NULL DEFAULT 0,
    reminder_count smallint NOT NULL DEFAULT 0,   -- tracks reminder/warning notices sent; escalate at 2
    status retrieval_status NOT NULL DEFAULT 'active'
);

CREATE TABLE access_logs (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id uuid REFERENCES users(id),
    device_id uuid REFERENCES devices(id),
    event_type text NOT NULL,   -- login_success, login_fail, totp_success, totp_fail, door_open, door_close
    ts timestamptz NOT NULL DEFAULT now(),
    metadata jsonb
);

CREATE TABLE override_logs (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id uuid NOT NULL REFERENCES devices(id),
    triggered_by override_trigger NOT NULL,   -- 'physical' = outer-box tamper switch
    reason text,
    ts timestamptz NOT NULL DEFAULT now(),
    resolved_by uuid REFERENCES users(id),
    resolved_at timestamptz
);

CREATE TABLE notifications (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id uuid NOT NULL REFERENCES users(id),
    type text NOT NULL,   -- retrieval_confirmation, return_reminder, overdue_warning, coordinator_escalation
    message text,
    channel text NOT NULL DEFAULT 'email',
    sent_at timestamptz
);
```

**Concurrency note (FR-5):** the retrieve endpoint must use an atomic conditional update, not a read-then-write check:

```sql
UPDATE key_slots SET status = 'retrieved'
WHERE id = $1 AND status = 'available';
```

Check the affected row count to detect a lost race.

**Proximity codes are Redis-only** — not persisted to Postgres. They're single-use, 120s TTL, and have no audit value once consumed, so a DB table would just be noise.

## 6. Device Communication Protocol

Topic structure defined in `architecture.md` §4.3. QoS recommendation: **QoS 1** for commands and the proximity-code/tamper-event topics (at-least-once — these shouldn't be silently dropped), **QoS 0** for routine telemetry (battery/heartbeat).

## 7. Proximity Verification Spec

**Protocol:**
1. Client connects to the Access Controller's WiFi AP
2. Access Controller generates a random 8-character code, publishes `device/{id}/access/proximity_code {code, ts}` over MQTT
3. Backend caches `code → device_id` in Redis, TTL 120s
4. Access Controller's captive portal DNS-redirects the client to `<web_app_url>/connect?device_id=X&code=Y`
5. Web app calls `POST /proximity/verify {device_id, code}`; backend checks Redis, deletes the code on success (single-use), and sets a proximity-verified flag on the browser session, valid 5 minutes
6. `/sessions/start`, `/keys/{id}/retrieve`, `/keys/{id}/return` all check this flag server-side; missing or expired → `403 {"error":"not_proximity_verified"}` with a UI prompt to reconnect to the enclosure WiFi

**Why not host the whole site on the ESP32-S3 — direct answer: no.** The ESP32-S3 can serve the captive portal fine — that's a solved, lightweight problem (DNS redirect + a couple of static routes). It can't reasonably run bcrypt/TOTP-grade auth at scale, a relational DB with logs accumulating over a semester, SMTP delivery, or a real-time dashboard that coordinators and admin need to check without walking to A-19. Hosting the dashboard on-device would also mean remote monitoring stops working entirely — the opposite of what an admin panel is for, and it breaks the "check key status before walking over" feature outright. The proximity-code approach gets the actual requirement (retrieval requires being at the box) without those costs, using infrastructure (MQTT, Redis) already in the design.

## 8. Authentication & Security Spec

- Password hashing: bcrypt (via passlib) — simplest solid option in the FastAPI ecosystem; argon2id acceptable if preferred
- TOTP: RFC 6238, 30s step, ±1 step tolerance; secret generated at enrollment, shown once as QR, never re-displayed
- Sessions: server-side, Redis-backed, short TTL — chosen over pure stateless JWT specifically so a stolen-phone report lets an admin kill a session immediately
- Device credentials: unique MQTT username/password (or client cert) per controller, rotated periodically
- Rate limiting: 5 failed logins / 15 min lockout; TOTP 5 attempts / 5 min
- Nonce-based replay protection on all unlock/dispense commands (FR-3)

## 9. Admin Panel Technical Spec

**RBAC matrix**

| Action | Coordinator | Admin |
|---|---|---|
| View dashboard | Yes, own room(s) only | Yes, all |
| View logs | Yes, own room(s) only | Yes, all |
| Manage user accounts (create/deactivate) | No | Yes |
| Grant/revoke permissions | Yes, own room(s) only | Yes, all |
| Manage rooms/keys/coordinator assignment | No | Yes |
| Toggle device maintenance | No | Yes |
| Export reports | Yes, own room(s) only | Yes, all |
| Receive overdue escalation emails | Yes, own room(s) only | No (unless repeat/unresolved) |

Scope enforcement: every coordinator-facing query filters `WHERE room_id IN (SELECT id FROM rooms WHERE coordinator_id = current_user.id)`. Members never see the admin panel at all.

## 10. Infrastructure & Deployment

Hosting per `architecture.md` §8. Recommend a staging/prod split even at student-project scale, so hardware bring-up testing doesn't run against the same DB as real pilot data. The email module ships as an internal package inside the FastAPI project — `EMAIL_PROVIDER=smtp` plus `EMAIL_ADDRESS` / `EMAIL_APP_PASSWORD` / `SMTP_SERVER` / `SMTP_PORT` in `.env`. CI/CD: GitHub Actions → deploy on push to `main` (prod) and a `staging` branch (staging).

## 11. Testing Requirements

- Unit tests: permission logic, TOTP verification, overdue-time calculation, reminder-count escalation trigger
- Integration tests: full retrieve/return flow against a mocked MQTT broker
- Proximity flow: verify a stale, expired, or reused code is rejected; verify `/keys/{id}/retrieve` returns 403 without a valid proximity flag
- Hardware-in-the-loop pass required before demo — real ESP32 + relay + tamper switch, not simulated
- Load test: concurrent retrieval requests on the same slot (validates §5's atomic update)

## 12. Monitoring & Observability

- Heartbeat miss > 5 min → alert admin (device offline)
- Battery < 20% → alert
- Unlock command with no ack within timeout → alert + one automatic retry
- Outer-box tamper event → immediate admin notification (email + dashboard banner), not just a log line
- Structured (JSON) backend logs, correlation ID per session for end-to-end tracing of a single retrieval

## 13. Notification / Email Integration

Reuses the existing internal email module (Provider Pattern, Factory-based dispatch — same interface style as `send_notes_email()`). Config: `EMAIL_PROVIDER=smtp` with `EMAIL_ADDRESS` / `EMAIL_APP_PASSWORD` / `SMTP_SERVER` / `SMTP_PORT` in `.env`, matching the module's existing setup. Switching providers later (SendGrid, SES, Resend, etc.) is a config change only, per the module's Factory design.

**Functions needed** (same provider abstraction underneath, new templates):

- `send_key_retrieved_email(user, room, due_at)` — fires on successful retrieve
- `send_return_reminder_email(user, room, due_at)` — fires ~30 min before due
- `send_overdue_warning_email(user, room)` — fires at due time if not returned/renewed (failure 1, increments `reminder_count`)
- `send_coordinator_escalation_email(coordinator, user, room, retrieval_log)` — fires after a further grace period if still unresolved (failure 2)

**Default schedule** (admin-configurable via Notification Config in the admin panel):

| Event | Timing | Recipient |
|---|---|---|
| Retrieval confirmation | Immediate | Member |
| Return reminder | T-30min before due | Member |
| Overdue warning | At due time | Member |
| Coordinator escalation | T+2h past due | Coordinator, via `rooms.coordinator_id` |

