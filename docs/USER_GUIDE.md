# SNTC User & Integration Guide

How a member gets a key, how Google Authenticator is enrolled, and what the ESP32
firmware has to implement to make it work.

---

## 0. The three factors (read this first)

The most common misconception: **Google Authenticator never talks to the ESP32.**
They are unrelated checks that happen to run in the same flow.

| # | Factor | Proves | Where it's checked |
|---|---|---|---|
| 1 | Password | Something you **know** | Backend, bcrypt hash |
| 2 | TOTP 6-digit code | Something you **have** (your phone) | Backend, computed offline from a shared secret + clock |
| 3 | Proximity code | Somewhere you **are** (at the enclosure) | Backend, code cached in Redis by the ESP32 over MQTT |

TOTP is offline maths. Your phone and the server hold the same secret and both
run RFC 6238 against the wall clock, so a code works with no network on either
side. The ESP32 has no idea the phone exists.

Proximity is the opposite: it proves nothing about identity. It only proves the
person holding the code stood inside the enclosure's WiFi range within the last
2 minutes. That is why all three are required before a key is dispensed.

---

## 0.5 Demo account (local testing)

`backend/seed.py` creates a working dataset — two users, a device with a fixed
UUID, two rooms, eight slots, and permissions.

```bash
docker compose up -d postgres redis mosquitto
make migrate
make seed
```

| Account | Password | Role |
|---|---|---|
| `admin@iitmandi.ac.in` | `Demo@1234` | admin |
| `demo@iitmandi.ac.in` | `Demo@1234` | member |

Both are seeded **already enrolled** with a known TOTP secret, so you can add
them to Google Authenticator by hand and skip the QR flow:

```
JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP
```

Google Authenticator → **+** → **Enter a setup key** → account
`demo@iitmandi.ac.in`, key above, type **Time based**. The 6-digit code it shows
is what the login page wants.

The device UUID is fixed at `11111111-2222-3333-4444-555555555555` so the
firmware config never drifts out of sync with the database.

Re-running `make seed` is safe — it skips anything that exists. If an account
already existed with different credentials, `python seed.py --reset` restores
the demo password and secret on it.

**This secret is published in this repo.** Anyone who reads the guide can
generate valid codes for these accounts. Seed a real deployment with real users
and let them enroll their own phones.

---



Do this once, per user. Any network works; you don't need to be at the enclosure.

1. Install **Google Authenticator** (or Authy, 1Password, Aegis — any RFC 6238 app).
2. Open the portal → **Sign In** → enter your IIT Mandi email and password.
3. You land on the TOTP setup page with a QR code.
4. In Authenticator: **+** → **Scan a QR code** → point at the screen.
   The entry appears as `SNTC IIT Mandi: you@iitmandi.ac.in`.
5. **Copy the text secret shown under the QR and store it somewhere safe.**
   It is displayed once and never again. Lose it *and* your phone, and an admin
   must re-enroll you.
6. Click **I've Added It → Continue**, then type the current 6-digit code.
7. You're in.

**Clock skew is the #1 enrollment failure.** TOTP codes are derived from the
current time. The backend accepts ±1 step (±30 seconds). If your phone's clock
drifts, every code reads as wrong. In Google Authenticator: `⋮` → Settings →
Time correction for codes → Sync now.

**Lost your phone:** an admin runs re-enroll (`POST /admin/users/{id}/totp/reenroll`)
which clears the old secret and issues a new QR. The self-service setup endpoint
refuses to re-enroll an account that already has a secret, so a stolen login
token cannot silently swap your second factor.

---

## 2. Getting a key (the daily procedure)

### Step 1 — Join the enclosure WiFi

At the enclosure, open WiFi settings on your phone and join:

```
SSID:     SNTC-Enclosure
Password: (open network, or the printed passphrase on the enclosure)
```

A captive portal opens automatically. If it doesn't, browse to `http://4.3.2.1`.

The portal shows a short code:

```
   Proximity code

      4F2A91

   Valid for 2 minutes
```

### Step 2 — Leave the enclosure WiFi

**Rejoin campus WiFi or switch to mobile data before continuing.**

This is not optional and not a bug. The ESP32 SoftAP is a local radio with no
route to the internet, so while you are joined to it you cannot reach the portal
or the backend. Both iOS and Android also detect the dead-end and will either nag
you or silently bounce you to mobile data anyway.

The code in your hand is the proof of presence. You do not need to stay connected
for it to count.

### Step 3 — Sign in

1. Open the portal → **Sign In**
2. Email + password
3. 6-digit code from Google Authenticator

You now have a session cookie, valid **1 hour**.

### Step 4 — Submit the proximity code

Go to **/connect** and type the code from Step 1.

On success you get a proximity flag valid **5 minutes**. That's the window in
which you can actually operate the enclosure.

### Step 5 — Take the key

1. **/keys** shows the slot grid — available, taken, or under maintenance
2. Pick your room and click **Retrieve**
3. The door unlocks and the slot dispenses your key
4. Default possession window is **6 hours**; you can extend from the same screen

### Step 6 — Return it

Repeat Steps 1–4 (fresh proximity code — they're single-use), then **/keys** →
**Return**. The slot unlocks, you drop the key in, it logs as returned.

### Timing reality check

| Thing | Lifetime |
|---|---|
| Proximity code (from the ESP32) | 120 s |
| Proximity flag (after submitting the code) | 300 s |
| Session cookie | 3600 s |
| TOTP code | 30 s (±1 step accepted) |

**120 seconds is tight for this order of operations.** Reading the code, leaving
the AP, rejoining campus WiFi, and completing a full login is often 90+ seconds,
and a first-time enrollment will never fit.

Two ways to deal with it:

- **Sign in first, then walk over.** Your session lasts an hour. Do Step 3 at your
  desk, then Steps 1, 2, 4 at the enclosure — the code only has to survive a
  network switch and one form submit, comfortably inside 120 s.
- **Raise the ceiling.** `proximity_code_ttl_seconds` in `backend/app/core/config.py`
  is the knob. Raising it to 300 makes the walk-up order comfortable, at the cost
  of a longer window in which a code copied off someone's screen stays valid.

Pick one before rollout. The current 120 s assumes the phone stays on the
enclosure network, which this flow deliberately does not do.

---

## 3. ESP32 firmware contract

**A working implementation of everything below lives in
`firmware/kms_enclosure/`** — flash it, edit `config.h`, done. See
`firmware/README.md` for wiring and flashing steps. This section is the contract
it satisfies, kept here so you can port it to ESP-IDF or another board.

Board is an ESP32-S3; MQTT broker is HiveMQ Cloud or a local Mosquitto.

### 3.1 Radio mode

Run `WIFI_AP_STA` — SoftAP and station simultaneously.

- **STA** joins campus WiFi. This is the uplink for MQTT.
- **AP** serves `SNTC-Enclosure` for phones walking up.

Do **not** enable NAT (`esp_netif_napt_enable`). Members are supposed to leave the
AP, and routing their traffic through a microcontroller is a liability you don't
want. The AP exists to hand out one short string.

### 3.2 Proximity code

Every 60 seconds, and on each new AP client association:

1. Generate 6 characters from a non-ambiguous alphabet — `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`
   (no `O`/`0`, no `I`/`1`/`l`). Use `esp_random()`, not `rand()`.
2. Publish it, QoS 1:

```
topic:   device/{device_uuid}/access/proximity_code
payload: {"code": "4F2A91"}
```

3. Display the same code on the captive portal page.

The backend caches it in Redis at `proximity:code:{code}` → `device_uuid` with a
120 s TTL, and deletes it on first use. Single-use is enforced server-side, so
firmware does not need to track redemption.

### 3.3 Captive portal

Serve on the AP interface:

- A DNS responder answering **every** query with the AP IP (`4.3.2.1`)
- `GET /` → the code page
- OS probe endpoints, so the "Sign in to network" banner fires:
  - `/generate_204` (Android)
  - `/hotspot-detect.html` (iOS/macOS)
  - `/ncsi.txt` (Windows)

Return HTTP 302 to `/` from the probe endpoints. Returning 204 tells the OS the
network is fine and suppresses the portal.

Keep the page one screen, no JS, no external assets — nothing on that AP can load
from the internet.

### 3.4 Commands from the backend

Subscribe to these and act on them:

```
device/{device_uuid}/access/command
device/{device_uuid}/rack/command
```

Payloads:

```jsonc
// unlock the enclosure door
{"action":"unlock_door","session_id":"...","nonce":"...","ttl_s":30,"ts":"2026-08-09T10:15:00Z"}

// push a key out of a slot
{"action":"dispense","slot_number":7,"nonce":"...","ts":"..."}

// open a slot to accept a returned key
{"action":"unlock","slot_number":7,"nonce":"...","ts":"..."}
```

**Nonce handling is mandatory.** Keep the last ~64 nonces in RAM. If a nonce
repeats, drop the message and do nothing. Also reject anything whose `ts` is more
than 60 s old. Without this, anyone who can replay a captured MQTT frame opens the
door at will.

### 3.5 Reporting back

```
device/{uuid}/access/event        {"event":"door_opened"}  →  logged as door_opened
device/{uuid}/access/tamper_event {"reason":"..."}         →  alerts every admin immediately
device/{uuid}/rack/event          slot state changes
device/{uuid}/power/telemetry     {"battery_pct":87,"on_backup_power":false}
device/{uuid}/{component}/heartbeat                        →  send every 30 s
```

Miss enough heartbeats and the device shows offline in the admin panel.

### 3.6 Provisioning a device

1. Admin panel → **Devices** → **Add Device** → note the generated UUID
2. Flash that UUID into `firmware/kms_enclosure/config.h` as `DEVICE_UUID`
3. Also set: campus WiFi credentials, MQTT host/port/user/pass, AP SSID
4. Power on, confirm heartbeats land in **Admin → Devices**

The seeded demo device is `11111111-2222-3333-4444-555555555555`, which is what
ships in `config.h` — for local testing you can skip steps 1–2 entirely.

### 3.7 Testing without a board

Every MQTT interaction can be driven from a laptop:

```bash
# publish a proximity code exactly as the ESP32 would
mosquitto_pub -h localhost \
  -t 'device/11111111-2222-3333-4444-555555555555/access/proximity_code' \
  -m '{"code":"TEST42"}'

# watch commands the backend sends back
mosquitto_sub -h localhost -t 'device/#' -v
```

Then sign in and enter `TEST42` at `/connect`. The code is single-use — a second
submit returns `400 Proximity code invalid or expired`.

---

## 4. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| "Invalid email or password" | Wrong credentials, or 5 failures in 15 min | Wait 15 min; the lockout is per-account |
| TOTP always rejected | Phone clock drift | Authenticator → Settings → Time correction → Sync now |
| "TOTP already enrolled" on setup | Secret exists; self-service enroll is one-shot | Ask an admin to re-enroll |
| "Sign in first, then enter the code" at /connect | No session yet | Complete login + TOTP, then resubmit |
| "Code invalid or expired" | Past 120 s, or already used | Get a fresh code from the portal |
| No captive portal on joining | OS cached the network as "no portal" | Browse to `http://4.3.2.1` directly |
| Retrieve returns 403 | Proximity flag expired (5 min) or no permission for that room | Re-verify proximity; check with your coordinator |
| Device shows offline | Heartbeats missing | Check STA WiFi and MQTT credentials on the board |

---

## 5. Why the flow is shaped this way

Physical presence is the hard part of key management. A password and a TOTP code
both travel over the network, so on their own they let anyone with stolen
credentials open a door from a hostel room. The proximity code cannot be phished
remotely: it exists only on a display that requires standing within a few metres
of the enclosure, and it dies after two minutes.

The trade-off is the network shuffle in Step 2. That's the cost of the ESP32
being an isolated radio rather than a router, and isolation is the right call for
a device bolted to a wall holding every key in the building.
