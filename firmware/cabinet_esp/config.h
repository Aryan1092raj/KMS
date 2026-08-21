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

// --- UART to the Arduino Uno low-level controller ----------------------------
// Plain ASCII lines, matching KMS_LowLevel_ArduinoUno.ino.
// ESP32 TX -> Uno pin 2 (SoftwareSerial RX)
// ESP32 RX <- Uno pin 3 (SoftwareSerial TX), through a 5V-to-3.3V level shifter
#define COMM_RX_PIN     16
#define COMM_TX_PIN     17
#define COMM_BAUD_RATE  9600

// How often to ask the low level for STATUS and BATT so /api/status has fresh
// numbers without the UI polling the UART itself.
#define POLL_INTERVAL_MS 2000UL

// Session lifetime for a successful /api/login.
#define SESSION_TTL_MS  (30UL * 60UL * 1000UL)

// --- Secrets ----------------------------------------------------------------
// These values are placeholders. Replace both before flashing and keep the
// real values out of git.
#ifndef KMS_SECRETS_SET
#define KMS_SECRETS_SET 1
#endif

#if !KMS_SECRETS_SET
#error "Set AP_PASSWORD and ADMIN_PASSWORD in config.h, then #define KMS_SECRETS_SET 1. The committed placeholders are public."
#endif

#define AP_PASSWORD     "kmsesp32"   // WPA2, min 8 chars

// Compared in full, in constant time, against the "password" field of the
// /api/login JSON body. The original tested
// body.indexOf(expectedPass) != -1 — a substring test against the raw body,
// so any request whose body merely *contained* the string authenticated,
// including one that carried it in an unrelated field.
#define ADMIN_PASSWORD  "kmsesp123"


#endif // CONFIG_H
