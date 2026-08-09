# PRD — Smart Key Storage System Web App

**Companion docs:** `architecture.md` (system design), `TRD.md` (technical/API spec)

## 1. Problem Statement

Retrieving a clubroom key at the IIT Mandi Student Activity Center currently requires the club coordinator to be physically present with the master keycard. When the coordinator is unavailable, members are locked out — even for permitted, routine access. This creates scheduling friction for every club sharing the space and puts an unnecessary availability burden on coordinators.

## 2. Goals

- Let any permitted club member retrieve their room's key without coordinator presence, in under 30 seconds end-to-end
- Produce a complete, tamper-evident audit trail — including emergency physical access, not just digital retrievals
- Give SNTC admin a single dashboard for granting/revoking access and monitoring device + key status
- Support the full 8-slot enclosure at IIT Mandi SNTC A-19 for the pilot deployment

## 3. Non-Goals (v1)

- **Fingerprint / RFID / face-recognition auth** — hardware supports these later; v1 ships credential + TOTP only
- **Multi-enclosure fleet management UI** — architecture supports multiple devices, but a single-enclosure pilot doesn't need fleet UI yet
- **Native mobile app** — a responsive web app covers the captive-portal flow; native app adds build/deploy overhead not justified at pilot scale

## 4. Personas

- **Club Member** — wants to grab or return a key fast, minimal friction, no coordinator dependency
- **Club Coordinator** — wants visibility and control over their own club's key access, and overdue alerts
- **SAC Admin / Warden** — owns the system end-to-end: users, all rooms, device health, full audit trail

## 5. User Stories

**Club Member**
- As a club member, I want to unlock the enclosure with my institute credentials + TOTP, so that I don't need the coordinator present.
- As a club member, I want to see real-time key status from anywhere, so that I don't make a wasted trip.
- As a club member, I want a reminder before my 6-hour possession window ends, so that I don't accidentally go overdue.
- As a club member, I want to request an extension from the app, so that I don't have to return a key mid-use.
- As a club member with no active permission, I want a clear "not authorized" message, so that I understand why I can't retrieve a key.

**Club Coordinator**
- As a coordinator, I want to grant or revoke key access for my own club's members directly, so that I don't need to route every membership change through admin.
- As a coordinator, I want to see who on my team currently holds our room key, so that I can plan around it.
- As a coordinator, I want to be notified if a member goes overdue, so that I can follow up directly.

**SAC Admin**
- As an admin, I want to bulk-import a club roster, so that I don't add 30 members one at a time each semester.
- As an admin, I want to see device battery/WiFi health, so that I catch problems before the enclosure goes offline.
- As an admin, I want every outer-box emergency access logged and flagged for review, so that the audit trail stays complete even when the digital path is bypassed.

**Staff (emergency access)**
- As staff opening the outer box during a mishap, I want that access logged automatically, so that there's no blind spot in the audit trail even though I didn't log in.

## 6. Requirements

### P0 — Must-Have

**Authentication & presence**
- Credential login + TOTP 2FA, compatible with standard authenticator apps
  - *Given* a user with valid credentials and an enrolled authenticator, *when* they enter a valid TOTP code, *then* they receive a session token.
  - *Given* an invalid TOTP code, *when* submitted, *then* access is denied and the attempt is logged.
- Login and key-status browsing work from any network (no proximity requirement)
- **Proximity-gated key actions** — starting a session, retrieving, and returning a key additionally require a fresh connection-proof from the enclosure's local WiFi within the last few minutes
  - *Given* a user who hasn't recently connected to the enclosure WiFi, *when* they try to retrieve a key, *then* the request is rejected with a "connect to enclosure WiFi" prompt.

**Key retrieval & return**
- Real-time status grid (Available / Retrieved / Maintenance) per slot, visible to anyone with permission from anywhere
- Permission-checked one-tap retrieve; permission-checked one-tap return
  - *Given* a user without permission for a room, *when* they attempt retrieval, *then* the request is rejected with a clear reason and logged.
- 6-hour possession timer with due-time display

**Notifications (all P0)**
- Retrieval confirmation email, sent immediately on successful dispense
- Return/renew reminder email, sent before the due time
- Overdue warning email to the member, sent at due time if not returned/renewed (failure 1)
- Escalation email to the room's coordinator if still unresolved after a further grace period (failure 2)

**Admin panel — core**
- User management: create/deactivate, role assignment (admin-only)
- Permission management: coordinators grant/revoke for their own room(s); admin for any room
- Live monitoring: current holder per key, time remaining, scoped by role
- Audit log viewer: access, retrieval/return, and outer-box emergency-access events (visually flagged, requires resolution note)
- Device health: battery %, WiFi signal, last heartbeat, offline alerting

**Audit**
- Every login attempt, TOTP attempt, door event, key event, and outer-box tamper event logged with timestamp and actor (or "physical/unauthenticated" for tamper events)

### P1 — Should-Have

- CSV bulk import for club rosters
- CSV export of logs
- Usage reports (retrievals per room, peak hours, avg possession duration)
- Maintenance mode per key slot (excludes it from availability without deleting it)

### P2 — Future Considerations

- RFID / fingerprint / face-recognition as additional auth factors
- Multi-enclosure fleet dashboard
- Read-only "auditor" role, if a use case for it shows up later
- SMS notifications alongside email
- Native mobile app / PWA push

## 7. Admin Panel — Feature Detail

1. **Dashboard** — keys currently out, overdue count, device health summary, today's activity, any unresolved tamper flags
2. **User management** (admin-only) — create/edit/deactivate accounts, role assignment, CSV bulk import
3. **Permission management** — coordinators: grant/revoke for their own room only; admin: any room
4. **Room & key management** (admin-only) — add/edit rooms, assign a `coordinator_id` per room, map slot↔room, per-slot maintenance mode
5. **Live monitoring** — real-time "who holds what" board, scoped to the viewer's role
6. **Audit logs** — access, retrieval/return, and outer-box tamper events (tamper entries require a resolution note before they can be dismissed); CSV export
7. **Device health** — battery %, WiFi RSSI, last heartbeat, firmware version, offline alert threshold
8. **Notification config** — reminder/escalation timing, recipient rules
9. **Reports** — usage per club/room, peak hours, average possession duration
10. **Scope enforcement** — coordinators see and act on their own room(s) only; admin sees everything

## 8. Workflows

**8.1 First-time enclosure access**
1. User connects to enclosure WiFi AP
2. Access Controller mints a proximity code, sends it to the backend, redirects the browser with it
3. Web app exchanges the code for a proximity-verified flag
4. User logs in with institute credentials, then TOTP
5. On success: door unlocks, session opens, event logged

**8.2 Key retrieval**
1. User views key status grid (works from anywhere), selects an available, permitted room
2. Taps "Retrieve" → backend checks permission + proximity flag → dispenses via rack controller
3. Slot status → Retrieved; 6-hour timer starts; retrieval confirmation email sent; event logged

**8.3 Key return**
1. User reconnects to enclosure WiFi (fresh proximity check required)
2. Re-authenticates, selects "Return" for their held key
3. Slot unlocks for reinsertion → user places key back
4. User confirms return → status → Available; session closes; event logged

**8.4 Overdue handling**
1. T-30min before due: reminder email to member with an extend option
2. At due time, if not returned/renewed: overdue warning email to member (failure 1)
3. T+2h past due, if still unresolved: escalation email to the room's coordinator (failure 2)
4. Coordinator or admin can force-extend or follow up manually

**8.5 Outer box emergency access**
1. Authorized staff open the outer box with the physical key during a mishap (power/network/hardware failure)
2. Tamper switch trips → Access Controller reports the event even with no network path to the backend at the time (buffered, flushed on reconnect)
3. Backend logs it to `override_logs`, flags it red on the admin dashboard, and notifies admin immediately once received
4. Admin adds a resolution note to clear the flag

**8.6 Onboarding a new club member**
1. Admin creates the base user account (individually or via CSV import)
2. The relevant coordinator grants that user permission for their room
3. User can now authenticate and retrieve that room's key on their next visit

## 9. Success Metrics

**Leading indicators**
- Median time from "connects to enclosure WiFi" to "key in hand": target < 30s
- TOTP first-attempt success rate: target > 90%
- Time from outer-box tamper event to admin notification: target < 1 minute

**Lagging indicators**
- Overdue rate: target < 10% of retrievals
- Device uptime: target > 99% excluding planned maintenance
- Unresolved tamper/override events older than 24h: target = 0

## 10. Constraints

- Hardware BOM ≈ ₹20,810 for the single-enclosure prototype — web/cloud layer should stay on free tiers for the pilot
- 3× ESP32-S3 fixed in the BOM — software must work within that edge compute budget
- Single enclosure, 8 slots max, for initial deployment at SAC A-19
- Notification delivery depends on the existing internal SMTP-based email module already used elsewhere — reuse it rather than building a new integration

## 11. Timeline Considerations

Mapped to the hardware build's August/September window:

- **August:** auth + dashboard + admin panel development in parallel with firmware bring-up
- **Early September:** integration testing against real hardware, including the proximity-code flow and tamper switch
- **Mid-September:** demo
- **Late September:** deployment at SAC A-19

## 12. Open Questions

- **[Non-blocking — ops]** Who holds the physical key to the outer box, and is that list tracked anywhere? Worth a short written policy so "authorized staff" in §8.5 has a real definition.
- **[Blocking — admin policy]** Who administers TOTP re-enrollment when a student loses their phone, and what's the identity-verification step for that admin action?
- **[Non-blocking — confirm defaults]** Reminder schedule (T-30min / due-time / +2h escalation) — confirm these numbers or adjust before building the notification service.
