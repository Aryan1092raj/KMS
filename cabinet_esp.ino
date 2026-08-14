/* Cabinet ESP32 sketch
   - Creates WPA2 SoftAP
   - DNSServer redirects all DNS to the device (captive portal)
   - AsyncWebServer serves local KMS web UI from LittleFS
   - Provides /api/login (POST) -> returns session token
   - Provides /api/action (POST) -> requires session token header; encrypts+HMACs payload and sends to Serial1 (TX/RX wiring to electronics ESP)
   - Uses AES-CTR + HMAC-SHA256 via mbedTLS (available in ESP32 Arduino core)
*/

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "LittleFS.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"

#define AP_SSID_PREFIX "ESP-KMS-"
#define AP_PASSWORD "ReplaceWithStrongPassword!" // WPA2 password (change)
#define DNS_PORT 53
#define HTTP_PORT 80

// UART for transmitting to electronics ESP
#define UART_TX_PIN 17
#define UART_RX_PIN 16
#define UART_BAUD 115200

// Security: 32-byte shared key (use unique per-device!)
const uint8_t SHARED_KEY[32] = {
  // Replace these bytes with a secure random key per device
  0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,
  0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
  0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,
  0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x11,0x22
};

// Server objects
DNSServer dnsServer;
AsyncWebServer server(HTTP_PORT);

// Simple in-memory session storage
String currentSessionToken = "";
uint32_t sessionExpiry = 0; // epoch millis

// Utility: random token
String genToken(size_t len=32) {
  static const char *chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String s;
  s.reserve(len);
  for (size_t i=0;i<len;i++) s += chars[esp_random() % 62];
  return s;
}

// Cryptography: AES-CTR encryption and HMAC-SHA256
// Encrypt plaintext -> ciphertext (same length), produce HMAC over (nonce||ciphertext)
bool aes_ctr_encrypt_and_hmac(const uint8_t *key32, const uint8_t *plaintext, size_t plen,
                              uint8_t *nonce16, uint8_t *ciphertext, uint8_t *mac32) {
  // nonce16: output 16 bytes (we will use 16 bytes)
  uint32_t r;
  for (int i=0;i<4;i++){ ((uint32_t*)nonce16)[i] = esp_random(); } // fill 16 bytes with random
  // AES-CTR using first 16 bytes of key as AES key (use key34? We'll use 16 bytes AES-128)
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int ret = mbedtls_aes_setkey_enc(&aes, key32, 128); // AES-128 using first 16 bytes
  if (ret != 0) { mbedtls_aes_free(&aes); return false; }
  unsigned char stream_block[16];
  unsigned char nonce_counter[16];
  memcpy(nonce_counter, nonce16, 16);
  unsigned int nc_off = 0;
  ret = mbedtls_aes_crypt_ctr(&aes, plen, &nc_off, nonce_counter, stream_block, plaintext, ciphertext);
  mbedtls_aes_free(&aes);
  if (ret != 0) return false;
  // HMAC-SHA256 over (nonce||ciphertext)
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_init(&ctx);
  if (mbedtls_md_setup(&ctx, info, 1) != 0) { mbedtls_md_free(&ctx); return false; }
  if (mbedtls_md_hmac_starts(&ctx, key32, 32) != 0) { mbedtls_md_free(&ctx); return false; }
  if (mbedtls_md_hmac_update(&ctx, nonce16, 16) != 0) { mbedtls_md_free(&ctx); return false; }
  if (mbedtls_md_hmac_update(&ctx, ciphertext, plen) != 0) { mbedtls_md_free(&ctx); return false; }
  if (mbedtls_md_hmac_finish(&ctx, mac32) != 0) { mbedtls_md_free(&ctx); return false; }
  mbedtls_md_free(&ctx);
  return true;
}

// UART framed send: [0xAA][0x55][len_hi][len_lo][nonce(16)][ciphertext][mac(32)]
void uart_send_frame(const uint8_t *payload, size_t payload_len) {
  // Prepare buffer for ciphertext and mac
  uint8_t nonce[16];
  uint8_t *ciphertext = (uint8_t*)malloc(payload_len);
  uint8_t mac[32];
  if (!aes_ctr_encrypt_and_hmac(SHARED_KEY, payload, payload_len, nonce, ciphertext, mac)) {
    Serial.println("Encryption failed");
    free(ciphertext);
    return;
  }
  uint16_t totalPayloadLen = 16 + payload_len + 32; // nonce + ciphertext + mac
  // Frame header
  Serial1.write(0xAA);
  Serial1.write(0x55);
  Serial1.write((totalPayloadLen >> 8) & 0xFF);
  Serial1.write(totalPayloadLen & 0xFF);
  // nonce
  Serial1.write(nonce, 16);
  // ciphertext
  Serial1.write(ciphertext, payload_len);
  // mac
  Serial1.write(mac, 32);
  free(ciphertext);
}

// Helper: require session token in header "X-Session-Token"
bool checkSession(AsyncWebServerRequest *request) {
  if (!request->hasHeader("X-Session-Token")) return false;
  String token = request->header("X-Session-Token")->value();
  if (token != currentSessionToken) return false;
  if (millis() > sessionExpiry) return false;
  return true;
}

void setup() {
  Serial.begin(115200);
  // UART1 for transmit to electronics ESP
  Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  if(!LittleFS.begin()){
    Serial.println("LittleFS mount failed");
  }

  // Build AP SSID with chip ID
  String chip = String((uint32_t)ESP.getEfuseMac(), HEX);
  String apSsid = String(AP_SSID_PREFIX) + chip.substring(chip.length()-6);
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("Started AP %s, IP: %s\n", apSsid.c_str(), apIP.toString().c_str());

  // Setup DNSServer to redirect all domains to AP IP
  dnsServer.start(DNS_PORT, "*", apIP);

  // Routes
  // Serve static files from LittleFS (put index.html etc into LittleFS)
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Login API (POST JSON {password: "..."}), returns JSON {token:"..."}
  server.on("/api/login", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("plain", true)) {
      String body = request->getParam("plain", true)->value();
      // For demo: accept a single password stored in code; in prod store hashed password
      const String expectedPass = "kmsadminpw"; // change
      if (body.indexOf(expectedPass) != -1) {
        currentSessionToken = genToken(40);
        sessionExpiry = millis() + (30 * 60 * 1000); // 30 minutes
        String resp = "{\"token\":\"" + currentSessionToken + "\"}";
        request->send(200, "application/json", resp);
        return;
      }
    }
    request->send(401, "application/json", "{\"error\":\"invalid\"}");
  });

  // Action API (POST JSON) requires session token header
  server.on("/api/action", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!checkSession(request)) {
      request->send(403, "application/json", "{\"error\":\"auth\"}");
      return;
    }
    if (request->hasParam("plain", true)) {
      String body = request->getParam("plain", true)->value();
      // Convert String body to bytes and send over UART encrypted
      uart_send_frame((const uint8_t*)body.c_str(), body.length());
      request->send(200, "application/json", "{\"status\":\"sent\"}");
      return;
    }
    request->send(400, "application/json", "{\"error\":\"no_body\"}");
  });

  // Catch-all to serve captive portal page
  server.onNotFound([](AsyncWebServerRequest *request){
    // redirect everything to root index.html
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
    response->addHeader("Cache-Control","no-cache, no-store, must-revalidate");
    request->send(response);
  });

  server.begin();
  Serial.println("Web server started");
}

void loop() {
  dnsServer.processNextRequest();
  // keep sessions valid (nothing else here)
}