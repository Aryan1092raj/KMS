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

// MQTT broker — HiveMQ Cloud. Credentials come from the cluster's Access
// Management tab and must match what the backend uses. Give the board its own
// credential, not the backend's, so one can be revoked without killing both.
//
// Local Mosquitto instead? Set MQTT_TLS to 0, MQTT_PORT to 1883, and use the
// laptop's LAN IP — never "localhost", the ESP32 resolves that to itself.
#define MQTT_HOST       "xxxxx.s1.eu.hivemq.cloud"
#define MQTT_PORT       8883
#define MQTT_USERNAME   "kms-device"
#define MQTT_PASSWORD   ""
#define MQTT_TLS        1                     // 0 = plaintext, local dev only

// Root CA for the broker's certificate chain. HiveMQ Cloud serves Let's Encrypt
// certs, which chain to ISRG Root X1 (self-signed, valid to 2035-06-04).
//
// Pinned rather than setInsecure() because the broker is now on the public
// internet: an unverified socket lets a MITM publish `dispense` and empty the
// rack. Verify against your own cluster before flashing a fleet:
//   openssl s_client -connect <host>:8883 -showcerts </dev/null
// If the chain ever changes, replace this block with the new root.
#define MQTT_ROOT_CA \
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"-----END CERTIFICATE-----\n"

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
