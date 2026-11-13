// Portable crypto primitives. ESP32: mbedtls (crypto_port_esp.cpp, #ifdef ARDUINO).
// Host tests: CommonCrypto (crypto_port_native.cpp, #ifndef ARDUINO).
#pragma once
#include <stddef.h>
#include <stdint.h>

void xc_sha256(const uint8_t* msg, size_t len, uint8_t out[32]);
void xc_hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
                    uint8_t out[32]);
// Constant-time compare. Returns true if equal.
bool xc_ct_equal(const uint8_t* a, const uint8_t* b, size_t len);
// Cryptographically secure random bytes. ESP32: a SHA-256/HMAC generator seeded from the
// hardware RNG (with its SAR-ADC entropy source forced on when the RF is off), the mixed-in
// pool below and timing jitter. Host: arc4random_buf.
void xc_random(uint8_t* out, size_t len);
// Mix extra entropy (e.g. SX1262 wideband RSSI bytes) into the pool used by xc_random and
// X25519 key generation. Never reduces entropy.
void xc_add_entropy(const uint8_t* data, size_t len);
// X25519. Native build returns false (not needed by host tests).
bool xc_x25519_keypair(uint8_t priv[32], uint8_t pub[32]);
bool xc_x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t out[32]);
