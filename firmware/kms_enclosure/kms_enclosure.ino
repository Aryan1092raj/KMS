// ─────────────────────────────────────────────────────────────────────────────
// KMS enclosure firmware — ESP32-S3
//
// Two jobs, nothing more:
//   1. Run a SoftAP + captive portal that shows a short proximity code, and
//      publish that code to MQTT so the backend can cache it.
//   2. Obey unlock/dispense commands from the backend, with replay protection.
//
// The AP has NO route to the internet on purpose. Members read the code, leave
// the AP, and submit it from a real network. See docs/USER_GUIDE.md §2.
//
// Libraries (Arduino IDE -> Library Manager):
//   - PubSubClient        (Nick O'Leary)
//   - ArduinoJson         (Benoit Blanchon)
//   DNSServer / WebServer / WiFi ship with the ESP32 core.
// ─────────────────────────────────────────────────────────────────────────────

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "config.h"

// Wired to the lock driver / rack solenoids. Adjust to your board.
//
// DevKit V1 (ESP32-WROOM-32) — GPIO 6..11 are strapped to the module's
// internal flash and must NEVER be driven; hence the lookup table below
// instead of PIN_SLOT_BASE + n. All pins here are safe outputs on a
// 30-pin DevKit V1 (avoid 0/2/12/15 strapping pins on other boards).
static const int PIN_DOOR_LOCK = 4;
static const int PIN_TAMPER    = 5;   // NC switch to GND, opens when prised
static const int PIN_SLOTS[8]  = { 13, 14, 16, 17, 18, 19, 21, 22 }; // slots 1..8

IPAddress apIP(AP_IP);
DNSServer dnsServer;
WebServer web(80);
WiFiClient net;
PubSubClient mqtt(net);

char proximityCode[CODE_LENGTH + 1] = {0};
uint32_t lastCodeRotate = 0;
uint32_t lastHeartbeat  = 0;
bool lastTamperState    = false;

// Replay protection: ring buffer of recently accepted nonces.
String nonceCache[NONCE_CACHE];
uint8_t nonceHead = 0;

// ── Proximity code ───────────────────────────────────────────────────────────

void generateCode() {
  const size_t n = strlen(CODE_ALPHABET);
  for (int i = 0; i < CODE_LENGTH; i++) {
    // esp_random() is the hardware RNG. rand() is seeded identically on every
    // boot, which would make codes predictable across power cycles.
    proximityCode[i] = CODE_ALPHABET[esp_random() % n];
  }
  proximityCode[CODE_LENGTH] = '\0';
}

void publishCode() {
  if (!mqtt.connected()) return;
  JsonDocument doc;
  doc["code"] = proximityCode;
  char buf[64];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_CODE, buf, true);   // QoS 0 retained; broker keeps latest
  Serial.printf("[code] %s -> %s\n", proximityCode, TOPIC_CODE);
}

void rotateCode() {
  generateCode();
  publishCode();
  lastCodeRotate = millis();
}

// ── Captive portal ───────────────────────────────────────────────────────────

String portalPage() {
  // Single screen, no JS, no external assets — nothing here can reach a CDN.
  String html = F(
    "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>SNTC Enclosure</title><style>"
    "body{margin:0;min-height:100vh;display:flex;align-items:center;"
    "justify-content:center;background:#0b0d10;color:#e8eaed;"
    "font-family:system-ui,-apple-system,sans-serif;text-align:center}"
    ".c{padding:2rem;max-width:22rem}"
    "h1{font-size:1rem;font-weight:500;letter-spacing:.08em;"
    "text-transform:uppercase;color:#9aa0a6;margin:0 0 1.5rem}"
    ".code{font-size:2.75rem;font-weight:700;letter-spacing:.28em;"
    "font-family:ui-monospace,monospace;color:#8ab4f8;margin:0 0 1.5rem}"
    "p{font-size:.9rem;line-height:1.55;color:#9aa0a6;margin:.5rem 0}"
    "b{color:#e8eaed}"
    "</style></head><body><div class='c'>"
    "<h1>Proximity code</h1><div class='code'>");
  html += proximityCode;
  html += F(
    "</div><p>Valid for <b>2 minutes</b>.</p>"
    "<p><b>Now rejoin campus WiFi or mobile data</b>, open the portal, sign in, "
    "and enter this code at <b>/connect</b>.</p>"
    "<p>This network has no internet access by design.</p>"
    "</div></body></html>");
  return html;
}

void handleRoot() {
  web.send(200, "text/html", portalPage());
}

void handleProbe() {
  // 302 to root makes iOS/Android/Windows raise the "Sign in to network"
  // banner. Returning 204 tells the OS all is well and suppresses the portal.
  web.sendHeader("Location", String("http://4.3.2.1/"), true);
  web.send(302, "text/plain", "");
}

// ── MQTT ─────────────────────────────────────────────────────────────────────

bool nonceSeen(const String &nonce) {
  for (uint8_t i = 0; i < NONCE_CACHE; i++) {
    if (nonceCache[i] == nonce) return true;
  }
  return false;
}

void rememberNonce(const String &nonce) {
  nonceCache[nonceHead] = nonce;
  nonceHead = (nonceHead + 1) % NONCE_CACHE;
}

void publishEvent(const char *topic, const char *key, const char *value) {
  JsonDocument doc;
  doc[key] = value;
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish(topic, buf);
}

void openDoor(uint32_t ttlSeconds) {
  digitalWrite(PIN_DOOR_LOCK, HIGH);
  publishEvent(TOPIC_EVENT, "event", "door_opened");
  // ponytail: blocking hold. The board has nothing else to do for these few
  // seconds; swap for a timer if you add concurrent slot work.
  delay(ttlSeconds * 1000UL);
  digitalWrite(PIN_DOOR_LOCK, LOW);
  publishEvent(TOPIC_EVENT, "event", "door_closed");
}

void driveSlot(int slotNumber, const char *eventName) {
  if (slotNumber < 1 || slotNumber > 8) return;
  const int pin = PIN_SLOTS[slotNumber - 1];
  digitalWrite(pin, HIGH);
  delay(1200);
  digitalWrite(pin, LOW);

  JsonDocument doc;
  doc["event"] = eventName;
  doc["slot_number"] = slotNumber;
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish("device/" DEVICE_UUID "/rack/event", buf);
}

void onMessage(char *topic, byte *payload, unsigned int length) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.println("[cmd] malformed JSON, dropped");
    return;
  }

  const String nonce = doc["nonce"] | "";
  const String action = doc["action"] | "";
  if (nonce.isEmpty() || action.isEmpty()) {
    Serial.println("[cmd] missing nonce/action, dropped");
    return;
  }
  if (nonceSeen(nonce)) {
    Serial.println("[cmd] replayed nonce, dropped");
    return;
  }
  rememberNonce(nonce);

  Serial.printf("[cmd] %s\n", action.c_str());

  if (action == "unlock_door") {
    openDoor(doc["ttl_s"] | 30);
  } else if (action == "dispense") {
    driveSlot(doc["slot_number"] | 0, "dispensed");
  } else if (action == "unlock") {
    driveSlot(doc["slot_number"] | 0, "returned");
  }
}

void mqttConnect() {
  if (mqtt.connected()) return;
  const String clientId = String("kms-") + DEVICE_UUID;
  const bool ok = strlen(MQTT_USERNAME)
    ? mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)
    : mqtt.connect(clientId.c_str());

  if (ok) {
    Serial.println("[mqtt] connected");
    mqtt.subscribe(TOPIC_CMD, 1);
    mqtt.subscribe("device/" DEVICE_UUID "/rack/command", 1);
    publishCode();      // backend may have restarted; re-seed the cache
  } else {
    Serial.printf("[mqtt] connect failed rc=%d\n", mqtt.state());
  }
}

void sendHeartbeat() {
  if (!mqtt.connected()) return;
  JsonDocument doc;
  doc["firmware_version"] = "1.0.0";
  doc["rssi"] = WiFi.RSSI();
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_HEARTBEAT, buf);

  JsonDocument telem;
  telem["battery_pct"] = 100;      // wire your fuel gauge here
  telem["on_backup"] = false;
  telem["rssi"] = WiFi.RSSI();
  serializeJson(telem, buf);
  mqtt.publish(TOPIC_TELEM, buf);

  lastHeartbeat = millis();
}

void checkTamper() {
  const bool tampered = digitalRead(PIN_TAMPER) == HIGH;   // NC opens -> HIGH
  if (tampered && !lastTamperState) {
    publishEvent(TOPIC_TAMPER, "reason", "enclosure_opened_without_command");
    Serial.println("[tamper] reported");
  }
  lastTamperState = tampered;
}

// ── Setup / loop ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_DOOR_LOCK, OUTPUT);
  digitalWrite(PIN_DOOR_LOCK, LOW);
  pinMode(PIN_TAMPER, INPUT_PULLDOWN);
  for (int i = 0; i < 8; i++) {
    pinMode(PIN_SLOTS[i], OUTPUT);
    digitalWrite(PIN_SLOTS[i], LOW);
  }

  // AP + STA at once: AP for walk-up phones, STA for the MQTT uplink.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (strlen(AP_PASSWORD)) {
    WiFi.softAP(AP_SSID, AP_PASSWORD);
  } else {
    WiFi.softAP(AP_SSID);
  }
  Serial.printf("[ap] %s at %s\n", AP_SSID, apIP.toString().c_str());

  WiFi.begin(STA_SSID, STA_PASSWORD);
  Serial.print("[sta] connecting");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED
    ? "\n[sta] up: " + WiFi.localIP().toString()
    : "\n[sta] offline — AP still serves codes, MQTT will retry");

  // Answer every DNS query with our own IP. That is what triggers the portal.
  dnsServer.start(53, "*", apIP);

  web.on("/", handleRoot);
  web.on("/generate_204", handleProbe);         // Android
  web.on("/gen_204", handleProbe);
  web.on("/hotspot-detect.html", handleProbe);  // iOS / macOS
  web.on("/ncsi.txt", handleProbe);             // Windows
  web.onNotFound(handleProbe);
  web.begin();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(512);

  generateCode();
  Serial.printf("[code] initial %s\n", proximityCode);
}

void loop() {
  dnsServer.processNextRequest();
  web.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    mqttConnect();
    mqtt.loop();
  }

  const uint32_t now = millis();
  if (now - lastCodeRotate >= CODE_ROTATE_MS) rotateCode();
  if (now - lastHeartbeat  >= HEARTBEAT_MS)   sendHeartbeat();
  checkTamper();
}
