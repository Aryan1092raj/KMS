// ─────────────────────────────────────────────────────────────────────────────
// config.h — per-installation configuration. Edit these, not the .ino.
//
// DEVICE_UUID MUST match the device the admin created in the portal
// (Admin -> Devices -> Add Device). The seed script uses the value below,
// so the stock demo works without changes. If you re-pair a board, flash a
// fresh UUID here.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

// Identity
#define DEVICE_UUID     "11111111-2222-3333-4444-555555555555"
#define AP_SSID         "SNTC-Enclosure"
#define AP_PASSWORD     "1234"                    // "" = open network
#define AP_IP           4, 3, 2, 1

// Uplink (STA) — joins campus WiFi, carries MQTT
#define STA_SSID        "IIT-Mandi-WiFi"      // WPA2-PSK, not Enterprise
#define STA_PASSWORD    "your-password"       // <- your campus WiFi password

// MQTT broker. Local Mosquitto on the dev laptop: use the LAN IP, never
// "localhost" — the ESP32 would resolve that to itself.
#define MQTT_HOST       "172.18.33.52"
#define MQTT_PORT       1883
#define MQTT_USERNAME   ""                    // set if the broker requires auth
#define MQTT_PASSWORD   ""

// Topics. Leave as-is; the backend subscribes to these exact patterns.
#define TOPIC_CODE      "device/" DEVICE_UUID "/access/proximity_code"
#define TOPIC_EVENT     "device/" DEVICE_UUID "/access/event"
#define TOPIC_TAMPER    "device/" DEVICE_UUID "/access/tamper_event"
#define TOPIC_TELEM     "device/" DEVICE_UUID "/power/telemetry"
#define TOPIC_HEARTBEAT "device/" DEVICE_UUID "/esp32/heartbeat"
#define TOPIC_CMD       "device/" DEVICE_UUID "/access/command"

// Behavior
#define CODE_ALPHABET   "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  // no O/0, I/1/l
#define CODE_LENGTH     6
#define CODE_ROTATE_MS  60000          // new code every 60 s
#define HEARTBEAT_MS    30000          // every 30 s
#define NONCE_WINDOW_S  60             // drop commands older than this
#define NONCE_CACHE     64             // replay-protection history size
