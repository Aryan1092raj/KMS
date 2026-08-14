/* Electronics ESP32 sketch
   - Listens on Serial1 for framed encrypted messages from cabinet ESP
   - Frame: 0xAA 0x55 len_hi len_lo [nonce16][ciphertext][mac32]
   - Verifies HMAC-SHA256 and decrypts AES-CTR then prints payload (hook electronics control here)
*/

#include <Arduino.h>
#include "mbedtls/aes.h"
#include "mbedtls/md.h"

#define UART_RX_PIN 16
#define UART_TX_PIN 17
#define UART_BAUD 115200

const uint8_t SHARED_KEY[32] = {
  // MUST match the cabinet ESP key exactly
  0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,
  0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
  0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe,
  0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x11,0x22
};

void setup() {
  Serial.begin(115200);
  Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.println("Electronics ESP ready");
}

bool aes_ctr_decrypt_and_verify(const uint8_t *key32, const uint8_t *nonce16, const uint8_t *ciphertext, size_t clen,
                                const uint8_t *mac32, uint8_t *out_plain) {
  // Verify HMAC-SHA256
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_init(&ctx);
  if (mbedtls_md_setup(&ctx, info, 1) != 0) { mbedtls_md_free(&ctx); return false; }
  if (mbedtls_md_hmac_starts(&ctx, key32, 32) != 0) { mbedtls_md_free(&ctx); return false; }
  if (mbedtls_md_hmac_update(&ctx, nonce16, 16) != 0) { mbedtls_md_free(&ctx); return false; }
  if (mbedtls_md_hmac_update(&ctx, ciphertext, clen) != 0) { mbedtls_md_free(&ctx); return false; }
  uint8_t calc_mac[32];
  if (mbedtls_md_hmac_finish(&ctx, calc_mac) != 0) { mbedtls_md_free(&ctx); return false; }
  mbedtls_md_free(&ctx);
  if (memcmp(calc_mac, mac32, 32) != 0) {
    Serial.println("HMAC mismatch");
    return false;
  }
  // AES-CTR decrypt (same as encrypt)
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mbedtls_aes_setkey_enc(&aes, key32, 128) != 0) { mbedtls_aes_free(&aes); return false; }
  unsigned char stream_block[16];
  unsigned char nonce_counter[16];
  memcpy(nonce_counter, nonce16, 16);
  unsigned int nc_off = 0;
  if (mbedtls_aes_crypt_ctr(&aes, clen, &nc_off, nonce_counter, stream_block, ciphertext, out_plain) != 0) {
    mbedtls_aes_free(&aes); return false;
  }
  mbedtls_aes_free(&aes);
  return true;
}

void processFrame(const uint8_t *frame, size_t len) {
  // frame contains [nonce(16)][ciphertext(len-48)][mac(32)] and total len = 16 + c + 32
  if (len < (16+32)) return;
  size_t clen = len - 16 - 32;
  const uint8_t *nonce = frame;
  const uint8_t *ciphertext = frame + 16;
  const uint8_t *mac = frame + 16 + clen;
  uint8_t *plain = (uint8_t*)malloc(clen+1);
  if (!aes_ctr_decrypt_and_verify(SHARED_KEY, nonce, ciphertext, clen, mac, plain)) {
    free(plain);
    return;
  }
  plain[clen] = 0;
  Serial.print("Received action payload: ");
  Serial.println((char*)plain);
  // TODO: parse JSON and actuate electronics here
  free(plain);
}

void loop() {
  static enum {WAIT_SYNC1, WAIT_SYNC2, READ_LEN_HI, READ_LEN_LO, READ_PAYLOAD} state = WAIT_SYNC1;
  static uint16_t payloadLen = 0;
  static uint8_t *buf = nullptr;
  static size_t bufPos = 0;

  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (state == WAIT_SYNC1) {
      if (b == 0xAA) state = WAIT_SYNC2;
    } else if (state == WAIT_SYNC2) {
      if (b == 0x55) state = READ_LEN_HI;
      else state = WAIT_SYNC1;
    } else if (state == READ_LEN_HI) {
      payloadLen = ((uint16_t)b) << 8;
      state = READ_LEN_LO;
    } else if (state == READ_LEN_LO) {
      payloadLen |= b;
      if (payloadLen > 4096) { // sanity
        state = WAIT_SYNC1;
      } else {
        buf = (uint8_t*)malloc(payloadLen);
        bufPos = 0;
        state = READ_PAYLOAD;
      }
    } else if (state == READ_PAYLOAD) {
      buf[bufPos++] = b;
      if (bufPos >= payloadLen) {
        // process frame
        processFrame(buf, payloadLen);
        free(buf);
        buf = nullptr;
        state = WAIT_SYNC1;
      }
    }
  }
}