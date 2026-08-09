# Production Deployment Design — SNTC KMS

**Date:** 2026-08-10
**Status:** Approved for planning
**Scope:** Backend → Render, Frontend → Vercel, Postgres → Neon, Redis → Upstash. Remove hardcoded values, close the `/ws/keys` auth hole, fix `.gitignore`, ship to GitHub.

`<app>.vercel.app` and `<api>.onrender.com` below are placeholders for hostnames
that do not exist until the services are provisioned. They are set as
environment variables at deploy time and are never committed.

---

## 1. Target topology

```
Browser
  │  https://<app>.vercel.app
  ▼
Vercel (Next.js 14 App Router)
  │  /api/:path*  ──rewrite (server-side)──►  https://<api>.onrender.com/:path*
  │  wss://<api>.onrender.com/ws/keys?ticket=…  (direct, ticket-authed)
  ▼
Render (FastAPI, Docker)
  ├── Neon Postgres   (postgresql+asyncpg://…?ssl=require)
  └── Upstash Redis   (rediss://…)
```

The `/api/:path*` rewrite in `frontend/next.config.js` is load-bearing. It runs
server-side, so the browser sees the backend's `Set-Cookie` as coming from the
Vercel origin. `SameSite=Lax` therefore works unchanged across split hosting.
Do not remove it.

Render free tier spins down when idle. An external cron pings `GET /health` to
keep the instance warm; APScheduler jobs (return reminders, T+2h escalation)
depend on that ping. No code change required.

---

## 2. WebSocket authentication

### The problem

`/ws/keys` currently accepts every connection with no authentication
(`backend/app/main.py:68`). It also cannot use the session cookie:

1. The cookie is set through the Vercel-side proxy, so it belongs to the Vercel
   host. `*.onrender.com` is a different registrable domain — the browser will
   never attach that cookie to the WS handshake, at any `SameSite` value. This
   is a domain mismatch, not a `SameSite` problem.
2. Vercel's Next.js rewrites do not proxy WebSocket upgrades, so the socket
   cannot be routed same-origin the way HTTP is.
3. The cookie is `HttpOnly`, so client JS cannot read and forward it.

### The design

A short-lived, single-use ticket, reusing the existing nonce helpers in
`backend/app/core/security.py:155`.

```
POST /ws/ticket           (cookie-authed, goes through the /api proxy)
  → generate_nonce()
  → SETEX ws_ticket:<nonce> 30 <session_id>
  → { "ticket": "<nonce>" }

WS /ws/keys?ticket=<nonce>
  → GETDEL ws_ticket:<nonce>        (atomic, single-use)
  → miss        → close(1008)
  → hit         → get_session(session_id)
  → expired     → close(1008)
  → valid       → accept, subscribe to live_status_channel()
```

`GETDEL` needs Redis 6.2+; Upstash is 6.2+ compatible, so this is available.
Verify during step 6 of the checks below — if it is not, fall back to a
`GET` + `DELETE` pipeline in a `MULTI`.

Properties:

- 30-second TTL, single-use — a leaked ticket in a URL or proxy log is dead
  almost immediately and cannot be replayed.
- No new session store. The ticket maps to the existing Redis session.
- Ticket issuance is cookie-authed through the proxy, so it inherits the
  existing `get_current_session` dependency with no new auth surface.
- `close(1008)` (policy violation) on every failure path — no distinction
  between "bad ticket" and "expired session".

Client (`frontend/app/keys/page.tsx`): `POST /api/ws/ticket`, then open
`wss://<api-host>/ws/keys?ticket=…`. On socket close with 1008, fall back to
the existing polling refresh rather than reconnecting in a loop.

### Rejected alternatives

- **`SameSite=None; Secure`** — does not fix a cross-domain cookie, and widens
  CSRF surface on every HTTP route for no gain.
- **Proxy the WS through Vercel** — not supported by Vercel rewrites.
- **Drop the WebSocket, poll only** — loses live status; the user chose to keep it.

---

## 3. De-hardcoding

| Location | Now | Change |
|---|---|---|
| `backend/app/api/auth.py:41` | `max_age=3600` | `settings.session_ttl_seconds` — one source of truth with the Redis TTL |
| `frontend/app/login/page.tsx:41` | `setPassword("Demo@1234")` | Remove. Gate the whole demo-login control on `NEXT_PUBLIC_DEMO_MODE` |
| `backend/.env.example` | `ALLOWED_ORIGINS=…,https://sntc-kms.workers.dev` | Vercel origin |
| `backend/app/core/config.py:23` | `mqtt_host = "localhost"` | Keep default, but gate the listener on a new `mqtt_enabled: bool = False` |
| `frontend/wrangler.jsonc` | `vars.INTERNAL_API_URL` pinned to a Render URL | Deleted with the file; becomes a Vercel env var |

`backend/seed.py`'s `DEMO_PASSWORD` stays — it is a seed script for a
throwaway demo dataset, not runtime config. The frontend must stop assuming it.

---

## 4. Security guards

### TOTP bypass

`backend/app/core/security.py:51` accepts `totp_demo_bypass_code` as a valid
code for every user. There is no guard: setting that env var in production
silently disables the second factor for the entire system.

Add a model validator on `Settings` (not on `get_settings`, which is
`@lru_cache`d) that raises on boot when `totp_demo_bypass_code` is non-empty
while `debug` is `False`. Fail closed, fail loud, at startup — not at the first
login attempt.

### Cookie flags

`secure=True` at `backend/app/api/auth.py:41` is already correct and needs no
change. Both deploy targets are HTTPS-only, and browsers treat `http://localhost`
as a secure context, so a `Secure` cookie is still accepted in local development.
No `cookie_secure` setting is added — it would be config for a value that never
varies.

---

## 5. MQTT deferred

`backend/app/main.py:54` starts the listener unconditionally. With no broker
reachable it hits the reconnect path in `backend/app/workers/mqtt_listener.py`
and retries every 5 seconds forever — a permanent log flood on Render, and
noise that hides real errors.

Gate `asyncio.create_task(run_mqtt_listener())` on `settings.mqtt_enabled`
(default `False`). Log a single line when skipped. Hardware wires in later by
flipping one env var — no redeploy of code, no schema change.

---

## 6. Cloudflare removal

Delete (user-approved):

- `frontend/wrangler.jsonc`
- `frontend/open-next.config.ts`
- `cf:build`, `cf:preview`, `cf:deploy` scripts in `frontend/package.json`
- `@opennextjs/cloudflare@1.15.0` and `wrangler@^4.59.2` devDependencies

Keep `output: "standalone"` in `next.config.js` — Vercel ignores it, and the
Docker runner stage in `frontend/Dockerfile` depends on it.

Fix `frontend/Dockerfile:5`. The line currently reads:

```dockerfile
COPY . .# NEXT_PUBLIC_* is inlined into the browser bundle at build time, so it has to be a
```

Docker does not treat `#` mid-line as a comment, so the comment text is parsed
as extra COPY arguments. Split the comment onto its own line.

---

## 7. Dependencies

`backend/requirements.txt`:

- Remove `uuid==1.30`. It is an abandoned Python 2 backport that installs a
  top-level `uuid.py`, shadowing the stdlib module of the same name. Nothing
  imports it deliberately; `security.py:2` imports the stdlib `uuid`.
- Move `pytest`, `pytest-asyncio`, `pytest-httpx` to `backend/requirements-dev.txt`.
  Test frameworks should not ship in the production image.

---

## 8. Deployment configuration

### `render.yaml` (new)

Single Docker web service. Every secret declared `sync: false` so Render
prompts rather than storing values in the repo.

| Variable | Source |
|---|---|
| `DATABASE_URL` | Neon, `postgresql+asyncpg://…?ssl=require` |
| `REDIS_URL` | Upstash, `rediss://…` |
| `ALLOWED_ORIGINS` | Vercel production origin |
| `DEBUG` | `false` |
| `EMAIL_PROVIDER`, `EMAIL_ADDRESS`, `EMAIL_APP_PASSWORD` | Gmail app password |
| `MQTT_ENABLED` | `false` |

`backend/Dockerfile` needs no change — it already respects Render's injected
`$PORT`, runs `alembic upgrade head` before `exec uvicorn`, and runs non-root.

Known limitation, documented not fixed: migrations run on every boot and both
APScheduler and the MQTT listener run in-process. Scaling past one instance
would duplicate scheduled notifications and race the migration. Single instance
is correct for this deployment; moving migrations to a Render release command
is the upgrade path.

### Vercel

Root directory `frontend`. Framework preset Next.js, no custom build command.

| Variable | Value | Notes |
|---|---|---|
| `INTERNAL_API_URL` | `https://<api>.onrender.com` | Server-side only. `next.config.js:46` fails the production build if unset. |
| `NEXT_PUBLIC_API_URL` | `https://<api>.onrender.com` | Inlined at build time. Used only for the WS URL. |
| `NEXT_PUBLIC_DEMO_MODE` | unset in production | Gates the demo-login control. |

New `frontend/.env.example` documents all three.

---

## 9. `.gitignore` and docs

Stop ignoring: `docs/`, `backend/tests/`, `backend/pytest.ini`.
Start ignoring: `.vercel/`.
Remove: `.open-next/`, `.wrangler/` (no longer produced).
Keep ignoring: `*.pdf`, `.agents/`, `.claude/`, `.playwright-mcp/`, `skills-lock.json`.

`README.md` corrections:

- Deployment table: Railway/Fly.io/Supabase → Render / Vercel / Neon / Upstash
- Drop the `SECRET_KEY` row — no such setting exists in `config.py`
- Fix `TRD.md` → `docs/TRD.md` and `architecture.md` → `docs/architecture.md`
  (both 404 today; they resolve once `docs/` is tracked)

---

## 10. Verification

Success criteria, in order:

1. `docker compose up` builds clean — proves the `Dockerfile:5` fix.
2. Backend boots against Neon + Upstash with `DEBUG=false`, `MQTT_ENABLED=false`:
   no MQTT reconnect spam, scheduler starts, `GET /health` returns 200.
3. Boot with `DEBUG=false` and `TOTP_DEMO_BYPASS_CODE` set → startup fails with
   a clear message. Unset → boots.
4. Full auth flow through the Vercel proxy: login → TOTP → cookie set →
   authenticated request succeeds.
5. All 26 HTTP routes exercised with an admin session; each returns its expected
   2xx, or the expected 4xx for the negative case (unauthenticated, wrong role,
   proximity not satisfied).
6. WS: `POST /api/ws/ticket` returns a ticket; the socket connects and receives
   `{"type":"connected"}`; the same ticket reused → `close(1008)`; no ticket →
   `close(1008)`.
7. `git status` clean, `docs/` and `backend/tests/` tracked, no `.env` staged.

Commit straight to `main` and push — no feature branch, no PR (decided during
implementation; a two-person team reviewing its own PR buys nothing here).

---

## Out of scope

- MQTT hardware integration (deferred by decision; `MQTT_ENABLED` is the switch)
- Multi-instance scaling for the backend
- The dead `req.session_id` field in the retrieve/return/extend request bodies —
  routers already resolve identity from the cookie (`backend/app/api/keys.py:39`),
  and the frontend sends the literal string `"browser-session"`. Harmless, left
  alone.
- Unused `NEXT_PUBLIC_SUPABASE_*` entries in the untracked `frontend/.env.local`.
  Never committed, nothing imports Supabase. Recommend deleting the lines.
