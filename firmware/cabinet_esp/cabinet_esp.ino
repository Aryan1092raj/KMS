// ─────────────────────────────────────────────────────────────────────────────
// KMS high-level (cabinet) ESP32
//
// SoftAP + captive portal + LittleFS web UI, unchanged from the original
// sketch. What changed is everything behind /api:
//
//   - /api/action now speaks the plaintext protocol.h line format accepted by
//     the Arduino Uno low-level controller.
//   - JSON bodies are read through AsyncCallbackJsonWebHandler. The original
//     used request->getParam("plain", true), which only exists for
//     application/x-www-form-urlencoded, so a real application/json POST fell
//     through to the 400/401 branch every time.
//   - Login compares the whole password field, in constant time, instead of
//     testing whether the raw body *contained* the password anywhere.
//   - UART writes happen only in loop(). Handlers run in the AsyncTCP task;
//     touching Serial1 from there races the poll loop, so they hand lines to
//     a FreeRTOS queue instead.
//
// Libraries (Arduino IDE -> Library Manager):
//   - ESPAsyncWebServer + AsyncTCP
//   - ArduinoJson (pulled in by AsyncJson.h)
//   mbedTLS and LittleFS ship with the ESP32 core.
// ─────────────────────────────────────────────────────────────────────────────

#include <WiFi.h>
#include <AsyncTCP.h>
// Must precede ESPAsyncWebServer.h: that header gates its JSON support on
// __has_include("ArduinoJson.h"), and the Arduino builder only puts ArduinoJson
// on the include path if the sketch names it. Without this line
// AsyncCallbackJsonWebHandler silently does not exist.
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <DNSServer.h>
#include "LittleFS.h"

#include "config.h"
#include "protocol.h"

DNSServer dnsServer;
AsyncWebServer server(HTTP_PORT);

// Lines waiting to go out on the UART. Written by the AsyncTCP task, read by
// loop(), which is the only context that touches Serial1.
static QueueHandle_t txQueue;

// Last known low-level state, for /api/status. Written by loop(), read by the
// AsyncTCP task, so every access sits in a critical section.
static portMUX_TYPE snapMux = portMUX_INITIALIZER_UNLOCKED;
static int      snapBatt  = -1;          // -1 = not reported yet
static int      snapState = -1;          // -1 = unknown, else SystemState
static char     snapEvent[PROTO_MAX_LINE] = "";
static uint32_t snapEventAt = 0;
static uint32_t uartRejected = 0;

// Session state. Both handlers that touch it run in the AsyncTCP task, so it
// needs no lock — only loop() runs elsewhere and loop() never reads it.
static String   sessionToken = "";
static uint32_t sessionExpiry = 0;
static uint8_t  loginFails = 0;
static uint32_t lockedUntil = 0;

// ── Auth ─────────────────────────────────────────────────────────────────────

static bool ctEqual(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

static String genToken(size_t len = 40) {
  static const char *chars =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String s;
  s.reserve(len);
  for (size_t i = 0; i < len; i++) s += chars[esp_random() % 62];
  return s;
}

// millis() wraps every ~49.7 days. "millis() > sessionExpiry" compares
// absolute values, so after the wrap a live session's expiry sits far in the
// numeric future and the session never expires — it fails open. The
// subtraction stays correct across the wrap.
static bool sessionLive() {
  return sessionToken.length() && (int32_t)(millis() - sessionExpiry) < 0;
}

static bool checkSession(AsyncWebServerRequest *request) {
  if (!sessionLive()) return false;
  if (!request->hasHeader("X-Session-Token")) return false;
  // ESPAsyncWebServer 3.x returns the value directly; the original's
  // header(...)->value() is a 2.x-only API and no longer compiles.
  const String given = request->header("X-Session-Token");
  // An early return on the first mismatching byte leaks the length of the
  // matching prefix through response timing, which is enough to walk a token
  // out one byte at a time.
  if (given.length() != sessionToken.length()) return false;
  return ctEqual((const uint8_t *)given.c_str(),
                     (const uint8_t *)sessionToken.c_str(), given.length());
}

static bool passwordOk(const char *given) {
  const size_t n = strlen(ADMIN_PASSWORD);
  if (!given || strlen(given) != n) return false;
  return ctEqual((const uint8_t *)given,
                     (const uint8_t *)ADMIN_PASSWORD, n);
}

// ── Outbound protocol lines ──────────────────────────────────────────────────

// Validates through the same parser the low level uses, then queues. Anything
// the low level would answer with ERR is rejected here instead of burning a
// round trip.
static bool queueLine(const char *verb, const char *arg) {
  char line[PROTO_MAX_LINE];
  ProtoCmd c;

  if (!proto_fmt(line, sizeof line, verb, arg)) return false;
  proto_parse(line, &c);
  if (!c.ok) return false;
  switch (c.verb) {
    case PCMD_GOTO: case PCMD_ANGLE: case PCMD_ACTUATE:
    case PCMD_BATT_Q: case PCMD_STATUS_Q:
      break;
    default:
      return false;   // not a command this end is allowed to send
  }
  return xQueueSend(txQueue, line, 0) == pdTRUE;
}

// ── Inbound protocol lines ───────────────────────────────────────────────────

static void noteEvent(const char *line) {
  size_t j = 0;
  char clean[PROTO_MAX_LINE];
  // Drops the trailing '\n' and anything that would need escaping, so the
  // value can be dropped straight into the /api/status JSON.
  for (size_t i = 0; line[i] && j < sizeof clean - 1; i++) {
    const char c = line[i];
    if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') clean[j++] = c;
  }
  clean[j] = '\0';

  portENTER_CRITICAL(&snapMux);
  memcpy(snapEvent, clean, j + 1);
  snapEventAt = millis();
  portEXIT_CRITICAL(&snapMux);
}

static void handleReply(const char *line) {
  ProtoCmd c;
  proto_parse(line, &c);

  switch (c.verb) {
    case PCMD_BATT_VAL:
      if (c.ok) {
        portENTER_CRITICAL(&snapMux);
        snapBatt = (int)c.num;
        portEXIT_CRITICAL(&snapMux);
      }
      return;
    case PCMD_STATUS_VAL:
      if (c.ok) {
        portENTER_CRITICAL(&snapMux);
        snapState = (int)c.num;
        portEXIT_CRITICAL(&snapMux);
      }
      return;
    case PCMD_ACK:
    case PCMD_DONE:
    case PCMD_ERR:
      noteEvent(line);
      return;
    default:
      return;
  }
}

// ── HTTP ─────────────────────────────────────────────────────────────────────

static void sendErr(AsyncWebServerRequest *r, int code, const char *msg) {
  char body[96];
  snprintf(body, sizeof body, "{\"error\":\"%s\"}", msg);
  r->send(code, "application/json", body);
}

static void onLogin(AsyncWebServerRequest *request, JsonVariant &json) {
  if ((int32_t)(millis() - lockedUntil) < 0) {
    sendErr(request, 429, "locked");
    return;
  }
  // Field-scoped compare. The original tested body.indexOf(expectedPass), so
  // {"password":"wrong","note":"kmsadminpw"} authenticated — as did any body
  // that mentioned the password for any reason.
  if (!json.is<JsonObject>() || !passwordOk(json["password"].as<const char *>())) {
    // Rate limit: the AP is reachable by anyone in radio range and the
    // password is a single fixed string, so an unthrottled endpoint is a
    // few minutes of guessing.
    if (++loginFails >= 5) {
      loginFails = 0;
      lockedUntil = millis() + 30000UL;
    }
    sendErr(request, 401, "invalid");
    return;
  }

  loginFails = 0;
  sessionToken = genToken();
  sessionExpiry = millis() + SESSION_TTL_MS;

  char body[80];
  snprintf(body, sizeof body, "{\"token\":\"%s\"}", sessionToken.c_str());
  request->send(200, "application/json", body);
}

static void onAction(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!checkSession(request)) { sendErr(request, 403, "auth"); return; }
  if (!json.is<JsonObject>())  { sendErr(request, 400, "bad_body"); return; }

  bool queued = false;

  // Escape hatch: a literal protocol line. Still parsed and verb-checked by
  // queueLine(), so it cannot smuggle a reply verb or a malformed param.
  const char *raw = json["line"].as<const char *>();
  if (raw && *raw) {
    ProtoCmd c;
    proto_parse(raw, &c);
    switch (c.verb) {
      case PCMD_GOTO:      queued = c.ok && queueLine("GOTO", c.arg);    break;
      case PCMD_ANGLE:     queued = c.ok && queueLine("ANGLE", c.arg);   break;
      case PCMD_ACTUATE:   queued = c.ok && queueLine("ACTUATE", c.arg); break;
      case PCMD_BATT_Q:    queued = queueLine("BATT", "?");             break;
      case PCMD_STATUS_Q:  queued = queueLine("STATUS", "?");           break;
      default:             sendErr(request, 400, "bad_line");           return;
    }
    if (!queued) { sendErr(request, 400, "bad_line"); return; }
    request->send(202, "application/json", "{\"queued\":true}");
    return;
  }

  const char *cmd = json["cmd"].as<const char *>();
  if (!cmd) { sendErr(request, 400, "no_cmd"); return; }

  if (!strcmp(cmd, "GOTO")) {
    if (!json["pos"].is<long>()) { sendErr(request, 400, "bad_pos"); return; }
    char pos[16];
    snprintf(pos, sizeof pos, "%ld", (long)json["pos"].as<long>());
    queued = queueLine("GOTO", pos);
  } else if (!strcmp(cmd, "ANGLE")) {
    if (!json["degrees"].is<long>()) { sendErr(request, 400, "bad_degrees"); return; }
    char degrees[16];
    snprintf(degrees, sizeof degrees, "%ld", (long)json["degrees"].as<long>());
    queued = queueLine("ANGLE", degrees);
  } else if (!strcmp(cmd, "ACTUATE")) {
    if (!json["on"].is<bool>()) { sendErr(request, 400, "bad_on"); return; }
    queued = queueLine("ACTUATE", json["on"].as<bool>() ? "1" : "0");
  } else if (!strcmp(cmd, "BATT")) {
    queued = queueLine("BATT", "?");
  } else if (!strcmp(cmd, "STATUS")) {
    queued = queueLine("STATUS", "?");
  } else {
    sendErr(request, 400, "unknown_cmd");
    return;
  }

  if (!queued) { sendErr(request, 503, "queue_full"); return; }
  // 202, not 200: the line is queued for the UART, and DONE/ERR arrives later
  // on /api/status. The original answered {"status":"sent"} before knowing
  // whether anything moved.
  request->send(202, "application/json", "{\"queued\":true}");
}

static void onStatus(AsyncWebServerRequest *request) {
  if (!checkSession(request)) { sendErr(request, 403, "auth"); return; }

  int batt, st;
  char ev[PROTO_MAX_LINE];
  uint32_t evAt;
  portENTER_CRITICAL(&snapMux);
  batt = snapBatt;
  st   = snapState;
  evAt = snapEventAt;
  memcpy(ev, snapEvent, sizeof ev);
  portEXIT_CRITICAL(&snapMux);

  const char *stName = (st < 0) ? "UNKNOWN" : proto_state_name((SystemState)st);

  char body[192];
  snprintf(body, sizeof body,
           "{\"state\":\"%s\",\"battery_pct\":%d,\"last_event\":\"%s\","
           "\"last_event_age_ms\":%lu,\"uart_rejected\":%lu}",
           stName, batt, ev,
           (unsigned long)(evAt ? millis() - evAt : 0),
           (unsigned long)uartRejected);
  request->send(200, "application/json", body);
}

// ── Setup / loop ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial1.begin(COMM_BAUD_RATE, SERIAL_8N1, COMM_RX_PIN, COMM_TX_PIN);
  txQueue = xQueueCreate(8, PROTO_MAX_LINE);

  if (!LittleFS.begin()) Serial.println("LittleFS mount failed");

  String chip = String((uint32_t)ESP.getEfuseMac(), HEX);
  String apSsid = String(AP_SSID_PREFIX) + chip.substring(chip.length() - 6);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("Started AP %s, IP: %s\n", apSsid.c_str(), apIP.toString().c_str());

  dnsServer.start(DNS_PORT, "*", apIP);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // AsyncCallbackJsonWebHandler reassembles a chunked body and parses it
  // before the callback runs. getParam("plain", true) only ever populates for
  // urlencoded form posts, which is why the original JSON API never matched.
  AsyncCallbackJsonWebHandler *loginH =
      new AsyncCallbackJsonWebHandler("/api/login", onLogin);
  loginH->setMethod(HTTP_POST);
  loginH->setMaxContentLength(256);
  server.addHandler(loginH);

  AsyncCallbackJsonWebHandler *actionH =
      new AsyncCallbackJsonWebHandler("/api/action", onAction);
  actionH->setMethod(HTTP_POST);
  actionH->setMaxContentLength(256);
  server.addHandler(actionH);

  server.on("/api/status", HTTP_GET, onStatus);

  server.onNotFound([](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response =
        request->beginResponse(LittleFS, "/index.html", "text/html");
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    request->send(response);
  });

  server.begin();
  Serial.println("Web server started");
}

void loop() {
  static uint32_t lastPoll = 0;
  char line[PROTO_MAX_LINE];
  static size_t rxPos = 0;
  static bool rxOverflow = false;

  dnsServer.processNextRequest();

  // Only context that writes the UART.
  while (xQueueReceive(txQueue, line, 0) == pdTRUE) {
    Serial1.write((const uint8_t *)line, strlen(line));
  }

  while (Serial1.available()) {
    const char c = (char)Serial1.read();
    if (c == '\n') {
      if (!rxOverflow && rxPos > 0) {
        line[rxPos] = '\0';
        ProtoCmd parsed;
        proto_parse(line, &parsed);
        if (parsed.verb == PCMD_UNKNOWN || !parsed.ok) uartRejected++;
        else handleReply(line);
      }
      rxPos = 0;
      rxOverflow = false;
    } else if (!rxOverflow && c != '\r') {
      if (rxPos + 1 < sizeof(line)) line[rxPos++] = c;
      else rxOverflow = true;
    }
  }

  // Ask rather than wait: DONE and ERR arrive unsolicited, but STATUS and BATT
  // only come back when asked, and /api/status has to answer with something.
  if (millis() - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = millis();
    queueLine("STATUS", "?");
    queueLine("BATT", "?");
  }
}
