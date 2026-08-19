#ifndef CONFIG_H
#define CONFIG_H

/*
  High-level (cabinet) ESP32 — SoftAP, captive portal, HTTP API.

  Board: ESP32 DevKit V1 / WROOM-32 (Arduino FQBN esp32:esp32:esp32).
*/

// --- Access point -----------------------------------------------------------
#define AP_SSID_PREFIX  "ESP-KMS-"
#define DNS_PORT        53
#define HTTP_PORT       80

// --- UART to the low-level (electronics) ESP32 ------------------------------
// Must match firmware/electronics_esp/config.h exactly, pins crossed.
#define COMM_RX_PIN     16
#define COMM_TX_PIN     17
#define COMM_BAUD_RATE  115200

// How often to ask the low level for STATUS and BATT so /api/status has fresh
// numbers without the UI polling the UART itself.
#define POLL_INTERVAL_MS 2000UL

// Session lifetime for a successful /api/login.
#define SESSION_TTL_MS  (30UL * 60UL * 1000UL)

// --- Secrets ----------------------------------------------------------------
// All three values below are the placeholders committed to the public upstream
// repo. Anyone who has read that repo can join the AP, log in, and drive the
// cabinet. Replace all three, set KMS_SECRETS_SET to 1, and keep the real
// values out of git.
//
//   openssl rand -hex 32 | sed 's/../0x&,/g'     # SHARED_KEY
//   openssl rand -base64 18                      # the two passwords
//
// -DKMS_SECRETS_SET=1 on the build command line also satisfies the guard,
// which is how CI compile-checks this sketch without real secrets in the tree.
#ifndef KMS_SECRETS_SET
#define KMS_SECRETS_SET 0
#endif

#if !KMS_SECRETS_SET
#error "Set AP_PASSWORD, ADMIN_PASSWORD and SHARED_KEY in config.h, then #define KMS_SECRETS_SET 1. The committed placeholders are public."
#endif

#define AP_PASSWORD     "ReplaceWithStrongPassword!"   // WPA2, min 8 chars

// Compared in full, in constant time, against the "password" field of the
// /api/login JSON body. The original tested
// body.indexOf(expectedPass) != -1 — a substring test against the raw body,
// so any request whose body merely *contained* the string authenticated,
// including one that carried it in an unrelated field.
#define ADMIN_PASSWORD  "kmsadminpw"

static const uint8_t SHARED_KEY[32] = {
  0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,
  0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
  0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,
  0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x11,0x22
};

#endif // CONFIG_H
