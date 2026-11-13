// ESP32 crypto backend: mbedtls (ESP-IDF 4.4 / mbedtls 2.28) + hardware RNG.
#ifdef ARDUINO
#include "crypto_port.h"

#include <bootloader_random.h>
#include <esp_bt.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <string.h>

void xc_sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), msg, len, out);
}

void xc_hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
                    uint8_t out[32]) {
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, key_len, msg, msg_len, out);
}

bool xc_ct_equal(const uint8_t* a, const uint8_t* b, size_t len) {
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

// ---- random numbers ----
// esp_fill_random is only truly random while Wi-Fi/BT is running or the SAR-ADC entropy
// source is enabled. Pairing runs with BT off, so force the ADC source on around every
// draw, then condition everything through SHA-256 together with a pool fed by
// xc_add_entropy (SX1262 wideband RSSI) and micros() jitter. Output comes from
// HMAC-SHA256(seed, counter), reseeded on every call.
namespace {
uint8_t g_pool[32];

bool bt_running() {
  return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
}

void hw_bytes(uint8_t* out, size_t len) {
  const bool adc = !bt_running();  // with BT up the RF noise source is already active
  if (adc) bootloader_random_enable();
  esp_fill_random(out, len);
  if (adc) bootloader_random_disable();
}

void fresh_seed(uint8_t seed[32]) {
  uint8_t buf[32 + 64 + 8];
  memcpy(buf, g_pool, 32);
  hw_bytes(buf + 32, 64);
  const uint64_t t = (uint64_t)esp_timer_get_time();
  memcpy(buf + 96, &t, 8);
  xc_sha256(buf, sizeof buf, seed);
  xc_sha256(seed, 32, g_pool);  // forward the pool; the seed itself is never kept
  mbedtls_platform_zeroize(buf, sizeof buf);  // never optimised away
}

void drbg(uint8_t* out, size_t len) {
  uint8_t seed[32], block[32];
  fresh_seed(seed);
  uint32_t ctr = 0;
  while (len) {
    xc_hmac_sha256(seed, 32, reinterpret_cast<const uint8_t*>(&ctr), sizeof ctr, block);
    const size_t n = len < 32 ? len : 32;
    memcpy(out, block, n);
    out += n;
    len -= n;
    ctr++;
  }
  mbedtls_platform_zeroize(seed, sizeof seed);
  mbedtls_platform_zeroize(block, sizeof block);
}
}  // namespace

void xc_add_entropy(const uint8_t* data, size_t len) {
  if (!data || !len) return;
  uint8_t buf[32 + 64];
  while (len) {
    const size_t n = len < 64 ? len : 64;
    memcpy(buf, g_pool, 32);
    memcpy(buf + 32, data, n);
    xc_sha256(buf, 32 + n, g_pool);
    data += n;
    len -= n;
  }
  mbedtls_platform_zeroize(buf, sizeof buf);
}

void xc_random(uint8_t* out, size_t len) { drbg(out, len); }

// mbedtls RNG callback (X25519 scalar generation, blinding).
static int rng_cb(void*, unsigned char* out, size_t len) {
  drbg(out, len);
  return 0;
}

static void clamp(uint8_t k[32]) {
  k[0] &= 248;
  k[31] &= 127;
  k[31] |= 64;
}

namespace {
struct Ctx {
  mbedtls_ecp_group grp;
  mbedtls_mpi d, z;
  mbedtls_ecp_point Q;
  Ctx() {
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);
    mbedtls_ecp_point_init(&Q);
  }
  ~Ctx() {
    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&z);
    mbedtls_ecp_point_free(&Q);
  }
  bool load() { return mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0; }
};
}  // namespace

bool xc_x25519_keypair(uint8_t priv[32], uint8_t pub[32]) {
  Ctx c;
  bool ok = c.load() && mbedtls_ecdh_gen_public(&c.grp, &c.d, &c.Q, rng_cb, nullptr) == 0 &&
            mbedtls_mpi_write_binary_le(&c.d, priv, 32) == 0 &&
            mbedtls_mpi_write_binary_le(&c.Q.X, pub, 32) == 0;
  if (!ok) {
    memset(priv, 0, 32);
    memset(pub, 0, 32);
  }
  return ok;
}

bool xc_x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t out[32]) {
  uint8_t k[32], u[32];
  memcpy(k, priv, 32);
  clamp(k);
  memcpy(u, peer_pub, 32);
  u[31] &= 127;  // RFC 7748: ignore the top bit of the u-coordinate
  Ctx c;
  bool ok = c.load() && mbedtls_mpi_read_binary_le(&c.d, k, 32) == 0 &&
            mbedtls_mpi_read_binary_le(&c.Q.X, u, 32) == 0 && mbedtls_mpi_lset(&c.Q.Z, 1) == 0 &&
            mbedtls_ecdh_compute_shared(&c.grp, &c.z, &c.Q, &c.d, rng_cb, nullptr) == 0 &&
            mbedtls_mpi_write_binary_le(&c.z, out, 32) == 0;
  mbedtls_platform_zeroize(k, sizeof k);  // clamped private scalar: a plain memset is a dead store
  // An all-zero secret means a low-order peer point: reject it.
  static const uint8_t zero[32] = {0};
  if (ok && xc_ct_equal(out, zero, 32)) ok = false;
  if (!ok) memset(out, 0, 32);
  return ok;
}
#endif  // ARDUINO
