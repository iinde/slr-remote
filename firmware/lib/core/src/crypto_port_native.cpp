// Host-test crypto backend (macOS CommonCrypto). X25519 is not needed on the host.
#ifndef ARDUINO
#include "crypto_port.h"

#if !defined(__APPLE__)
#error "crypto_port_native.cpp uses CommonCrypto; native tests need a macOS host"
#endif

#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <stdlib.h>

void xc_sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
  CC_SHA256(msg, (CC_LONG)len, out);
}

void xc_hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
                    uint8_t out[32]) {
  CCHmac(kCCHmacAlgSHA256, key, key_len, msg, msg_len, out);
}

bool xc_ct_equal(const uint8_t* a, const uint8_t* b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

void xc_random(uint8_t* out, size_t len) { arc4random_buf(out, len); }
void xc_add_entropy(const uint8_t*, size_t) {}

bool xc_x25519_keypair(uint8_t[32], uint8_t[32]) { return false; }
bool xc_x25519_shared(const uint8_t[32], const uint8_t[32], uint8_t[32]) { return false; }
#endif  // !ARDUINO
