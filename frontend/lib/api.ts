/**
 * SNTC API client — typed wrapper around all backend endpoints.
 * Uses Next.js rewrites to proxy /api/* → FastAPI backend.
 */

const API = "/api";

async function request<T>(
  path: string,
  options: RequestInit = {}
): Promise<T> {
  const res = await fetch(`${API}${path}`, {
    credentials: "include",
    headers: { "Content-Type": "application/json", ...options.headers },
    ...options,
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ error: res.statusText }));
    throw { status: res.status, ...err };
  }
  if (res.status === 204) return undefined as T;
  return res.json();
}

// ── Auth ──────────────────────────────────────────────────────────────────────

export const auth = {
  login: (email: string, password: string) =>
    request<{ requires_totp?: boolean; requires_totp_setup?: boolean; temp_token: string }>(
      "/auth/login",
      { method: "POST", body: JSON.stringify({ email, password }) }
    ),

  verifyTOTP: (temp_token: string, code: string) =>
    request<{ session_id: string; user_id: string; role: string }>(
      "/auth/totp/verify",
      { method: "POST", body: JSON.stringify({ temp_token, code }) }
    ),

  setupTOTP: (temp_token: string) =>
    request<{ totp_uri: string; secret: string }>("/auth/totp/setup", {
      method: "POST",
      body: JSON.stringify({ temp_token }),
    }),

  logout: () => request<void>("/auth/logout", { method: "POST" }),

  // Single-use ticket for the /ws/keys handshake. Goes through the same-origin
  // /api proxy so the session cookie applies; the websocket itself can't use it.
  wsTicket: () => request<{ ticket: string }>("/auth/ws/ticket", { method: "POST" }),
};

// ── Proximity ─────────────────────────────────────────────────────────────────

export const proximity = {
  verify: (code: string, device_id?: string) =>
    request<{ proximity_verified: boolean; expires_in_seconds: number }>(
      "/proximity/verify",
      { method: "POST", body: JSON.stringify({ code, device_id }) }
    ),
};

// ── Sessions ──────────────────────────────────────────────────────────────────

export const sessions = {
  start: (session_id: string, device_id: string) =>
    request<{ db_session_id: string; opened_at: string }>(
      "/sessions/start",
      { method: "POST", body: JSON.stringify({ session_id, device_id }) }
    ),
};

// ── Keys ──────────────────────────────────────────────────────────────────────

export interface KeySlot {
  slot_id: string;
  slot_number: number;
  room_id: string | null;
  room_name: string | null;
  status: "available" | "retrieved" | "maintenance";
  current_holder: string | null;
  due_at: string | null;
}

export const keys = {
  list: () => request<KeySlot[]>("/keys"),

  // Identity comes from the session cookie; the body carries nothing the
  // backend can't resolve itself.
  retrieve: (slot_id: string) =>
    request<{ slot_id: string; status: string; due_at: string; retrieval_log_id: string }>(
      `/keys/${slot_id}/retrieve`,
      { method: "POST", body: "{}" }
    ),

  returnKey: (slot_id: string) =>
    request<{ slot_id: string; status: string; returned_at: string }>(
      `/keys/${slot_id}/return`,
      { method: "POST", body: "{}" }
    ),

  extend: (slot_id: string, additional_hours = 6) =>
    request<{ slot_id: string; new_due_at: string; extension_count: number }>(
      `/keys/${slot_id}/extend`,
      { method: "POST", body: JSON.stringify({ additional_hours }) }
    ),
};

// ── Admin ─────────────────────────────────────────────────────────────────────

export interface User {
  id: string;
  name: string;
  email: string;
  roll_no: string | null;
  role: "member" | "coordinator" | "admin";
  is_active: boolean;
  totp_enrolled_at: string | null;
  created_at: string;
}

export interface Room {
  id: string;
  name: string;
  block: string | null;
  description: string | null;
  coordinator_id: string | null;
}

export interface Device {
  id: string;
  name: string;
  location: string | null;
  firmware_version: string | null;
  last_heartbeat_at: string | null;
  battery_pct: number | null;
  on_backup_power: boolean;
  wifi_rssi: number | null;
  status: string;
}

export interface AccessLog {
  id: string;
  user_id: string | null;
  device_id: string | null;
  event_type: string;
  ts: string;
  metadata_: Record<string, unknown> | null;
}

export interface RetrievalLog {
  id: string;
  key_slot_id: string;
  user_id: string;
  session_id: string | null;
  retrieved_at: string;
  due_at: string;
  returned_at: string | null;
  extension_count: number;
  reminder_count: number;
  status: "active" | "returned" | "overdue";
}

export interface OverrideLog {
  id: string;
  device_id: string;
  triggered_by: "physical" | "admin";
  reason: string | null;
  ts: string;
  resolved_by: string | null;
  resolved_at: string | null;
  resolution_note: string | null;
}

export interface DashboardSummary {
  keys_out: number;
  overdue_count: number;
  unresolved_tamper_count: number;
  device_summary: Device[];
  today_retrieval_count: number;
}

export const admin = {
  dashboard: () => request<DashboardSummary>("/admin/dashboard"),

  // Users
  listUsers: (skip = 0, limit = 50) =>
    request<User[]>(`/admin/users?skip=${skip}&limit=${limit}`),
  createUser: (data: { name: string; email: string; roll_no?: string; role?: string; password: string }) =>
    request<User>("/admin/users", { method: "POST", body: JSON.stringify(data) }),
  updateUser: (id: string, data: Partial<User & { is_active: boolean }>) =>
    request<User>(`/admin/users/${id}`, { method: "PATCH", body: JSON.stringify(data) }),
  bulkImport: (file: File) => {
    const form = new FormData();
    form.append("file", file);
    return fetch(`${API}/admin/users/bulk-import`, {
      method: "POST",
      credentials: "include",
      body: form,
    }).then((r) => r.json());
  },
  reenrollTOTP: (userId: string) =>
    request<{ totp_uri: string; secret: string }>(`/admin/users/${userId}/totp/reenroll`, { method: "POST" }),

  // Permissions
  grantPermission: (user_id: string, room_id: string, expires_at?: string) =>
    request("/admin/permissions", { method: "POST", body: JSON.stringify({ user_id, room_id, expires_at }) }),
  revokePermission: (perm_id: string) =>
    request(`/admin/permissions/${perm_id}`, { method: "DELETE" }),

  // Rooms
  listRooms: () => request<Room[]>("/admin/rooms"),
  createRoom: (data: { name: string; block?: string; description?: string; coordinator_id?: string }) =>
    request<Room>("/admin/rooms", { method: "POST", body: JSON.stringify(data) }),

  // Logs
  accessLogs: (skip = 0, limit = 100) =>
    request<AccessLog[]>(`/admin/logs/access?skip=${skip}&limit=${limit}`),
  retrievalLogs: (skip = 0, limit = 100) =>
    request<RetrievalLog[]>(`/admin/logs/retrieval?skip=${skip}&limit=${limit}`),
  overrideLogs: (skip = 0, limit = 100) =>
    request<OverrideLog[]>(`/admin/logs/override?skip=${skip}&limit=${limit}`),
  resolveOverride: (id: string, resolution_note: string) =>
    request(`/admin/logs/override/${id}/resolve`, { method: "POST", body: JSON.stringify({ resolution_note }) }),

  // Devices
  listDevices: () => request<Device[]>("/admin/devices"),
  createDevice: (data: { name: string; location?: string }) =>
    request<Device>("/admin/devices", { method: "POST", body: JSON.stringify(data) }),
  toggleMaintenance: (device_id: string) =>
    request(`/admin/devices/${device_id}/maintenance`, { method: "POST" }),

  // Reports
  usageReport: () =>
    request<{ rows: Array<{ room_id: string; room_name: string; total_retrievals: number; avg_possession_minutes: number; overdue_count: number }>; generated_at: string }>(
      "/admin/reports/usage"
    ),
};
