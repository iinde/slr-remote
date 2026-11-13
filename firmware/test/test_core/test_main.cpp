// Host unit tests for lib/core. Run: pio test -e native
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "airtime.h"
#include "battery.h"
#include "buttons.h"
#include "crypto_port.h"
#include "dutycycle.h"
#include "fujievt.h"
#include "geo.h"
#include "linkmgr.h"
#include "modes.h"
#include "press.h"
#include "proto.h"
#include "rangelog_fmt.h"
#include "region.h"
#include "shotseq.h"

void setUp() {}
void tearDown() {}

static void hex_to(const char* hex, uint8_t* out) {
  for (size_t i = 0; hex[2 * i]; i++) {
    unsigned v;
    sscanf(hex + 2 * i, "%2x", &v);
    out[i] = (uint8_t)v;
  }
}

// ---------------- crypto ----------------
static void test_sha256_abc() {
  uint8_t out[32], exp[32];
  hex_to("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", exp);
  xc_sha256((const uint8_t*)"abc", 3, out);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, out, 32);
}

static void test_hmac_rfc4231_case2() {
  const char* key = "Jefe";
  const char* msg = "what do ya want for nothing?";
  uint8_t out[32], exp[32];
  hex_to("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", exp);
  xc_hmac_sha256((const uint8_t*)key, 4, (const uint8_t*)msg, strlen(msg), out);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, out, 32);
}

static void test_ct_equal_and_random() {
  uint8_t a[16] = {1, 2, 3}, b[16] = {1, 2, 3};
  TEST_ASSERT_TRUE(xc_ct_equal(a, b, 16));
  b[15] = 1;
  TEST_ASSERT_FALSE(xc_ct_equal(a, b, 16));
  uint8_t r1[32] = {0}, r2[32] = {0};
  xc_random(r1, 32);
  xc_random(r2, 32);
  TEST_ASSERT_FALSE(xc_ct_equal(r1, r2, 32));
}

// ---------------- proto ----------------
static uint8_t KEY[32], KEY2[32];

static Frame sample_frame() {
  Frame f{};
  f.type = MsgType::Shot;
  f.net = 0xBEEF;
  f.ctr = 0x01020304;
  f.seq = 0xA55A;
  f.len = 2;
  f.payload[0] = 0xF4;
  f.payload[1] = 0x01;
  return f;
}

static void test_frame_round_trip() {
  Frame f = sample_frame();
  uint8_t buf[PROTO_MAX_FRAME];
  size_t n = frame_encode(f, KEY, buf, sizeof buf);
  TEST_ASSERT_EQUAL(PROTO_HDR + 2 + PROTO_TAG, n);
  TEST_ASSERT_EQUAL_HEX8(PROTO_VER, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, buf[2]);  // little-endian net
  TEST_ASSERT_EQUAL_HEX8(0x04, buf[4]);  // little-endian ctr
  Frame g{};
  TEST_ASSERT_TRUE(frame_decode(buf, n, KEY, g));
  TEST_ASSERT_EQUAL(MsgType::Shot, g.type);
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, g.net);
  TEST_ASSERT_EQUAL_HEX32(0x01020304, g.ctr);
  TEST_ASSERT_EQUAL_HEX16(0xA55A, g.seq);
  TEST_ASSERT_EQUAL(2, g.len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(f.payload, g.payload, 2);
}

static void test_frame_max_payload_round_trip() {
  Frame f = sample_frame();
  f.len = PROTO_MAX_PAYLOAD;
  for (int i = 0; i < (int)PROTO_MAX_PAYLOAD; i++) f.payload[i] = (uint8_t)i;
  uint8_t buf[PROTO_MAX_FRAME];
  size_t n = frame_encode(f, KEY, buf, sizeof buf);
  TEST_ASSERT_EQUAL(PROTO_MAX_FRAME, n);
  Frame g{};
  TEST_ASSERT_TRUE(frame_decode(buf, n, KEY, g));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(f.payload, g.payload, PROTO_MAX_PAYLOAD);
}

static void test_frame_tamper_any_byte() {
  Frame f = sample_frame();
  uint8_t buf[PROTO_MAX_FRAME];
  size_t n = frame_encode(f, KEY, buf, sizeof buf);
  for (size_t i = 0; i < n; i++) {
    for (int bit = 0; bit < 8; bit++) {
      uint8_t t[PROTO_MAX_FRAME];
      memcpy(t, buf, n);
      t[i] ^= (uint8_t)(1 << bit);
      Frame g{};
      TEST_ASSERT_FALSE_MESSAGE(frame_decode(t, n, KEY, g), "tampered frame accepted");
    }
  }
  Frame g{};
  TEST_ASSERT_FALSE(frame_decode(buf, n - 1, KEY, g));  // truncated
  TEST_ASSERT_FALSE(frame_decode(buf, 0, KEY, g));
}

static void test_frame_wrong_key() {
  Frame f = sample_frame();
  uint8_t buf[PROTO_MAX_FRAME];
  size_t n = frame_encode(f, KEY, buf, sizeof buf);
  Frame g{};
  TEST_ASSERT_FALSE(frame_decode(buf, n, KEY2, g));
}

static void test_frame_encode_limits() {
  Frame f = sample_frame();
  uint8_t buf[PROTO_MAX_FRAME + 8];
  f.len = PROTO_MAX_PAYLOAD + 1;
  TEST_ASSERT_EQUAL(0, frame_encode(f, KEY, buf, sizeof buf));
  f.len = 2;
  TEST_ASSERT_EQUAL(0, frame_encode(f, KEY, buf, PROTO_HDR + 2 + PROTO_TAG - 1));
}

static void test_replay_guard() {
  ReplayGuard g(10);
  TEST_ASSERT_FALSE(g.accept(10));
  TEST_ASSERT_FALSE(g.accept(9));
  TEST_ASSERT_TRUE(g.accept(11));
  TEST_ASSERT_FALSE(g.accept(11));
  TEST_ASSERT_TRUE(g.accept(500));
  TEST_ASSERT_FALSE(g.accept(499));
  TEST_ASSERT_EQUAL_UINT32(500, g.last());
}

static void test_seq_dedupe() {
  SeqDedupe d;
  TEST_ASSERT_TRUE(d.firstTimeId(1));
  TEST_ASSERT_FALSE(d.firstTimeId(1));
  TEST_ASSERT_TRUE(d.firstTimeId(7));
  TEST_ASSERT_FALSE(d.firstTimeId(7));
  for (uint32_t s = 100; s < 115; s++) TEST_ASSERT_TRUE(d.firstTimeId(s));  // 17 seen total
  TEST_ASSERT_FALSE(d.firstTimeId(114));
  TEST_ASSERT_TRUE(d.firstTimeId(1));  // oldest fell out of the 16-slot ring
}

static void test_ack_round_trip() {
  AckPayload a{};
  a.ack_seq = 0x1234;
  a.ack_type = MsgType::Shoot;
  a.stage = ShotStage::CamEvent;
  a.err = 3;
  a.rx_rssi = -121;
  a.rx_snr_q4 = -60;
  a.cam_batt = 87;
  a.cam_link = CamLink::Ready;
  a.ble_rssi = -45;
  a.sf = 11;
  a.gps_flags = 3;
  a.lat_e7 = 515074000;
  a.lon_e7 = -1278000;
  a.duty_permille = 999;
  a.utc = 1794000000u;
  uint8_t buf[ACK_PAYLOAD_LEN];
  TEST_ASSERT_EQUAL(ACK_PAYLOAD_LEN, ack_pack(a, buf));
  AckPayload b{};
  TEST_ASSERT_TRUE(ack_unpack(buf, sizeof buf, b));
  TEST_ASSERT_EQUAL_HEX16(0x1234, b.ack_seq);
  TEST_ASSERT_EQUAL(MsgType::Shoot, b.ack_type);
  TEST_ASSERT_EQUAL(ShotStage::CamEvent, b.stage);
  TEST_ASSERT_EQUAL(3, b.err);
  TEST_ASSERT_EQUAL_INT8(-121, b.rx_rssi);
  TEST_ASSERT_EQUAL_INT8(-60, b.rx_snr_q4);
  TEST_ASSERT_EQUAL(87, b.cam_batt);
  TEST_ASSERT_EQUAL(CamLink::Ready, b.cam_link);
  TEST_ASSERT_EQUAL_INT8(-45, b.ble_rssi);
  TEST_ASSERT_EQUAL(11, b.sf);
  TEST_ASSERT_EQUAL(3, b.gps_flags);
  TEST_ASSERT_EQUAL_INT32(515074000, b.lat_e7);
  TEST_ASSERT_EQUAL_INT32(-1278000, b.lon_e7);
  TEST_ASSERT_EQUAL(999, b.duty_permille);
  TEST_ASSERT_EQUAL_UINT32(1794000000u, b.utc);
  AckPayload c{};   // an older 26-byte Ack (no utc) still parses, with utc unknown
  TEST_ASSERT_TRUE(ack_unpack(buf, ACK_PAYLOAD_LEN_V1, c));
  TEST_ASSERT_EQUAL_UINT32(0, c.utc);
  TEST_ASSERT_FALSE(ack_unpack(buf, ACK_PAYLOAD_LEN_V1 - 1, b));
}

static void test_ping_round_trip() {
  PingPayload p{77, -118, -37, 1};
  uint8_t buf[PING_PAYLOAD_LEN];
  TEST_ASSERT_EQUAL(PING_PAYLOAD_LEN, ping_pack(p, buf));
  PingPayload q{};
  TEST_ASSERT_TRUE(ping_unpack(buf, sizeof buf, q));
  TEST_ASSERT_EQUAL(77, q.hh_batt);
  TEST_ASSERT_EQUAL_INT8(-118, q.last_rssi);
  TEST_ASSERT_EQUAL_INT8(-37, q.last_snr_q4);
  TEST_ASSERT_EQUAL(1, q.flags);
  TEST_ASSERT_FALSE(ping_unpack(buf, 3, q));
}

static void test_net_from_key() {
  uint8_t d[32];
  xc_sha256(KEY, 32, d);
  TEST_ASSERT_EQUAL_HEX16((uint16_t)(d[0] | (d[1] << 8)), net_from_key(KEY));
  TEST_ASSERT_NOT_EQUAL(net_from_key(KEY), net_from_key(KEY2));
}

// ---------------- airtime ----------------
static void assert_within_2pct(double expected, uint32_t got) {
  TEST_ASSERT_FLOAT_WITHIN((float)(expected * 0.02), (float)expected, (float)got);
}

static void test_airtime() {
  // Semtech formula, explicit header, CRC on, CR 4/5, preamble 8, 25 bytes.
  assert_within_2pct(61.7, lora_airtime_ms(7, 125, 5, 8, 25));
  assert_within_2pct(411.6, lora_airtime_ms(10, 125, 5, 8, 25));
  // SF11/12 use LDRO; without it SF11/25 B would be 741 ms and SF12/30 B 1483 ms.
  assert_within_2pct(823.3, lora_airtime_ms(11, 125, 5, 8, 25));
  assert_within_2pct(1482.8, lora_airtime_ms(12, 125, 5, 8, 25));
  assert_within_2pct(1646.6, lora_airtime_ms(12, 125, 5, 8, 30));
  // Never under-report (duty budget): result rounds up.
  TEST_ASSERT_GREATER_OR_EQUAL(62, lora_airtime_ms(7, 125, 5, 8, 25));
}

// ---------------- link manager ----------------
static void test_sf_floor_margin_bars() {
  TEST_ASSERT_EQUAL_FLOAT(-7.5f, sf_floor_db(7));
  TEST_ASSERT_EQUAL_FLOAT(-10.f, sf_floor_db(8));
  TEST_ASSERT_EQUAL_FLOAT(-12.5f, sf_floor_db(9));
  TEST_ASSERT_EQUAL_FLOAT(-15.f, sf_floor_db(10));
  TEST_ASSERT_EQUAL_FLOAT(-17.5f, sf_floor_db(11));
  TEST_ASSERT_EQUAL_FLOAT(-20.f, sf_floor_db(12));
  TEST_ASSERT_EQUAL_FLOAT(10.f, margin_db(2.5f, 7));
  TEST_ASSERT_EQUAL_FLOAT(5.f, margin_db(-15.f, 12));
  TEST_ASSERT_EQUAL(0, bars_from_margin(-0.1f));
  TEST_ASSERT_EQUAL(1, bars_from_margin(0.f));
  TEST_ASSERT_EQUAL(1, bars_from_margin(2.9f));
  TEST_ASSERT_EQUAL(2, bars_from_margin(3.f));
  TEST_ASSERT_EQUAL(2, bars_from_margin(5.9f));
  TEST_ASSERT_EQUAL(3, bars_from_margin(6.f));
  TEST_ASSERT_EQUAL(3, bars_from_margin(9.9f));
  TEST_ASSERT_EQUAL(4, bars_from_margin(10.f));
}

static LinkSample ack(float snr_up, float snr_dn) { return LinkSample{true, snr_up, snr_dn, 100}; }
static LinkSample miss() { return LinkSample{false, 0, 0, 0}; }

static void test_link_step_up() {
  LinkManager lm(7);
  // margin up 8, down 4.5 -> worst 4.5 < 5
  TEST_ASSERT_EQUAL(LinkManager::Action::RequestSf, lm.onPing(ack(0.5f, -3.f)));
  TEST_ASSERT_EQUAL(8, lm.requestedSf());
  TEST_ASSERT_EQUAL(7, lm.sf());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.5f, lm.lastMarginDb());
  lm.confirmSf(8);
  TEST_ASSERT_EQUAL(8, lm.sf());
  // exactly 5 dB worst margin: no step
  TEST_ASSERT_EQUAL(LinkManager::Action::None, lm.onPing(ack(-5.f, 0.f)));
  // at SF12 never asks for more
  lm.confirmSf(12);
  TEST_ASSERT_EQUAL(LinkManager::Action::None, lm.onPing(ack(-19.f, -19.f)));
}

static void test_link_step_down_needs_five() {
  LinkManager lm(9);
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(LinkManager::Action::None, lm.onPing(ack(5.f, 5.f)));
  // a middling ping breaks the streak
  TEST_ASSERT_EQUAL(LinkManager::Action::None, lm.onPing(ack(-5.f, -5.f)));  // margin 7.5
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(LinkManager::Action::None, lm.onPing(ack(5.f, 5.f)));
  TEST_ASSERT_EQUAL(LinkManager::Action::RequestSf, lm.onPing(ack(5.f, 5.f)));
  // margin 17.5 dB at SF9: floor((17.5 - 8) / 2.5) = 3 steps, clamped to SF7
  TEST_ASSERT_EQUAL(7, lm.requestedSf());
  // a miss also breaks the streak
  LinkManager l2(9);
  for (int i = 0; i < 4; i++) l2.onPing(ack(5.f, 5.f));
  l2.onPing(miss());
  TEST_ASSERT_EQUAL(LinkManager::Action::None, l2.onPing(ack(5.f, 5.f)));
  // SF7 never steps down
  LinkManager l3(7);
  for (int i = 0; i < 10; i++) TEST_ASSERT_EQUAL(LinkManager::Action::None, l3.onPing(ack(10.f, 10.f)));
}

static void test_duty_reage_snapshot() {
  // 60 s of airtime saved 10 min before the snapshot; the board was then off for 30 min.
  DutyCycle src;
  src.record(60000, 1000);
  src.usedMs(1000 + 10 * 60000);           // roll 10 minutes: the airtime is now 10 min old
  DutySnapshot snap;
  src.save(snap);
  DutyCycle d;
  const uint32_t boot = 5000;
  d.restoreShifted(snap, boot);
  d.record(2000, boot);                    // cold-start precharge: must NOT be aged
  TEST_ASSERT_EQUAL_UINT32(62000, d.usedMs(boot));
  d.reageSnapshot(snap, boot, 30 * 60000, boot);       // real time: 30 min more
  TEST_ASSERT_EQUAL_UINT32(62000, d.usedMs(boot));     // 40 min old: still inside the hour
  d.reageSnapshot(snap, boot, 0, boot);                 // no-op
  DutyCycle e;
  e.restoreShifted(snap, boot);
  e.record(2000, boot);
  e.reageSnapshot(snap, boot, 60 * 60000, boot);        // off for an hour: the saved airtime is gone
  TEST_ASSERT_EQUAL_UINT32(2000, e.usedMs(boot));       // only the precharge stays
}

static void test_link_strong_steps_down_fast() {
  LinkManager lm(12);   // > 25 dB: one ping is enough
  TEST_ASSERT_EQUAL(LinkManager::Action::RequestSf, lm.onPing(ack(10.f, 10.f)));   // margin 30 dB at SF12
  TEST_ASSERT_EQUAL(7, lm.requestedSf());
  LinkManager m2(12);   // 20..25 dB: two pings
  TEST_ASSERT_EQUAL(LinkManager::Action::None, m2.onPing(ack(2.f, 2.f)));          // margin 22
  TEST_ASSERT_EQUAL(LinkManager::Action::RequestSf, m2.onPing(ack(2.f, 2.f)));
  TEST_ASSERT_EQUAL(7, m2.requestedSf());
}

static void test_link_rescue_and_bars() {
  LinkManager lm(8);
  lm.onPing(ack(5.f, 5.f));
  TEST_ASSERT_EQUAL(4, lm.bars());
  TEST_ASSERT_EQUAL(LinkManager::Action::None, lm.onPing(miss()));
  TEST_ASSERT_EQUAL(0, lm.bars());
  TEST_ASSERT_EQUAL(LinkManager::Action::None, lm.onPing(miss()));
  TEST_ASSERT_EQUAL(LinkManager::Action::Rescue, lm.onPing(miss()));
  TEST_ASSERT_EQUAL(3, lm.consecutiveMisses());
  lm.confirmSf(12);
  TEST_ASSERT_EQUAL(12, lm.sf());
  TEST_ASSERT_EQUAL(0, lm.consecutiveMisses());
}

static void test_link_fixed_mode() {
  LinkManager lm(7);
  lm.setAdaptive(false);
  TEST_ASSERT_EQUAL(LinkManager::Action::None, lm.onPing(ack(-20.f, -20.f)));
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL(LinkManager::Action::None, lm.onPing(ack(10.f, 10.f)));
  lm.onPing(miss());
  lm.onPing(miss());
  TEST_ASSERT_EQUAL(LinkManager::Action::Rescue, lm.onPing(miss()));
}

static void test_link_multi_step_and_state() {
  // Just over 12 dB: one step (SF9 -> SF8).
  LinkManager a(9);
  for (int i = 0; i < 4; i++) a.onPing(ack(-0.3f, -0.3f));  // margin 12.2
  TEST_ASSERT_EQUAL(LinkManager::Action::RequestSf, a.onPing(ack(-0.3f, -0.3f)));
  TEST_ASSERT_EQUAL(8, a.requestedSf());
  // SF12 with ~20 dB to spare: 4 steps at once (SF8), sized from the weakest of the streak.
  LinkManager b(12);
  b.onPing(ack(5.f, 5.f));   // margin 25
  b.onPing(ack(0.f, 0.f));   // margin 20 (weakest)
  for (int i = 0; i < 2; i++) b.onPing(ack(5.f, 5.f));
  TEST_ASSERT_EQUAL(LinkManager::Action::RequestSf, b.onPing(ack(5.f, 5.f)));
  TEST_ASSERT_EQUAL(8, b.requestedSf());
  // The streak survives a deep-sleep rebuild (same SF), not a change of SF.
  LinkManager c(12);
  for (int i = 0; i < 3; i++) c.onPing(ack(-6.f, -6.f));  // margin 14
  const LinkManager::State st = c.state();
  LinkManager d(12);
  d.restore(st);
  TEST_ASSERT_EQUAL(LinkManager::Action::None, d.onPing(ack(-6.f, -6.f)));
  TEST_ASSERT_EQUAL(LinkManager::Action::RequestSf, d.onPing(ack(-6.f, -6.f)));
  TEST_ASSERT_EQUAL(10, d.requestedSf());  // floor((14 - 8) / 2.5) = 2
  LinkManager e(11);
  e.restore(st);  // collected at SF12: ignored
  for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(LinkManager::Action::None, e.onPing(ack(-4.f, -4.f)));
}

static void test_link_success_pct() {
  LinkManager lm(7);
  TEST_ASSERT_TRUE(isnan(lm.successPct()));  // nothing tried: "--", never "0 %"
  lm.onPing(ack(0, 0));
  lm.onPing(miss());
  TEST_ASSERT_EQUAL_FLOAT(50.f, lm.successPct());
  LinkManager l2(7);
  for (int i = 0; i < 10; i++) l2.onPing(miss());
  for (int i = 0; i < 20; i++) l2.onPing(ack(0, 0));  // window is 20: misses rolled out
  TEST_ASSERT_EQUAL_FLOAT(100.f, l2.successPct());
  for (int i = 0; i < 5; i++) l2.onPing(miss());
  TEST_ASSERT_EQUAL_FLOAT(75.f, l2.successPct());
}

static void test_silence_watch() {
  SilenceWatch w;
  w.onValidRx(1000);
  TEST_ASSERT_FALSE(w.shouldRescue(8999, 7, 8000));
  TEST_ASSERT_TRUE(w.shouldRescue(9000, 7, 8000));
  TEST_ASSERT_FALSE(w.shouldRescue(9000, 12, 8000));  // already at SF12
  w.onPeerSleep(10000);
  TEST_ASSERT_FALSE(w.shouldRescue(100000, 7, 8000));
  w.onValidRx(100000);
  TEST_ASSERT_TRUE(w.shouldRescue(108000, 7, 8000));
  // millis wrap
  SilenceWatch x;
  x.onValidRx(0xFFFFF000u);
  TEST_ASSERT_FALSE(x.shouldRescue(0x00000100u, 7, 8000));
  TEST_ASSERT_TRUE(x.shouldRescue(0x00001F40u, 7, 8000));
}

// ---------------- duty cycle ----------------
static void test_duty_priority_vs_ping() {
  DutyCycle d(0.10f);  // 360 000 ms budget
  uint32_t t = 5000;
  TEST_ASSERT_TRUE(d.canSend(288000, t, false));
  TEST_ASSERT_FALSE(d.canSend(288001, t, false));
  d.record(288000, t);
  TEST_ASSERT_EQUAL_UINT32(288000, d.usedMs(t));
  TEST_ASSERT_FALSE(d.canSend(1, t, false));   // pings stop at 80 %
  TEST_ASSERT_TRUE(d.canSend(72000, t, true));  // shutter allowed to 100 %
  TEST_ASSERT_FALSE(d.canSend(72001, t, true));
  TEST_ASSERT_EQUAL(800, d.usedPermille(t));
  d.record(100000, t);
  TEST_ASSERT_EQUAL(1000, d.usedPermille(t));  // capped
}

static void test_duty_rolls_off() {
  DutyCycle d(0.10f);
  d.record(1000, 0);
  d.record(500, 30 * 60000u);
  TEST_ASSERT_EQUAL_UINT32(1500, d.usedMs(59 * 60000u));
  // Minute 0 stays counted until a full hour after it ENDS (61 buckets).
  TEST_ASSERT_EQUAL_UINT32(1500, d.usedMs(60 * 60000u + 1));
  TEST_ASSERT_EQUAL_UINT32(500, d.usedMs(61 * 60000u));
  TEST_ASSERT_EQUAL_UINT32(0, d.usedMs(91 * 60000u));
  d.record(10, 100 * 3600000u);  // long gap
  TEST_ASSERT_EQUAL_UINT32(10, d.usedMs(100 * 3600000u));
}

static void test_duty_wraparound() {
  DutyCycle d(0.10f);
  uint32_t t0 = 0xFFFFFFFFu - 30000u;
  d.record(2000, t0);
  uint32_t t1 = t0 + 120000u;  // wraps past zero
  TEST_ASSERT_TRUE(t1 < t0);
  d.record(3000, t1);
  TEST_ASSERT_EQUAL_UINT32(5000, d.usedMs(t1));
  TEST_ASSERT_EQUAL_UINT32(5000, d.usedMs(t0 + 3600000u + 1000));
  TEST_ASSERT_EQUAL_UINT32(3000, d.usedMs(t0 + 3660000u + 1000));
  TEST_ASSERT_EQUAL_UINT32(0, d.usedMs(t1 + 3600000u + 60000u));
}

// ---------------- geo ----------------
static void test_geo() {
  double d = geo_distance_m(51.5074, -0.1278, 48.8566, 2.3522);
  TEST_ASSERT_FLOAT_WITHIN(1000.0, 343500.0, d);
  double b = geo_bearing_deg(51.5074, -0.1278, 48.8566, 2.3522);
  TEST_ASSERT_FLOAT_WITHIN(1.5, 148.0, b);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, geo_distance_m(51, -1, 51, -1));
  double w = geo_bearing_deg(51, 0, 51, -1);
  TEST_ASSERT_TRUE(w >= 0 && w < 360);
  TEST_ASSERT_FLOAT_WITHIN(1.0, 270.0, w);
  TEST_ASSERT_EQUAL_STRING("N", geo_cardinal(0));
  TEST_ASSERT_EQUAL_STRING("N", geo_cardinal(22.4));
  TEST_ASSERT_EQUAL_STRING("NE", geo_cardinal(22.5));
  TEST_ASSERT_EQUAL_STRING("E", geo_cardinal(90));
  TEST_ASSERT_EQUAL_STRING("SE", geo_cardinal(148));
  TEST_ASSERT_EQUAL_STRING("S", geo_cardinal(180));
  TEST_ASSERT_EQUAL_STRING("SW", geo_cardinal(225));
  TEST_ASSERT_EQUAL_STRING("W", geo_cardinal(270));
  TEST_ASSERT_EQUAL_STRING("NW", geo_cardinal(337.4));
  TEST_ASSERT_EQUAL_STRING("N", geo_cardinal(337.5));
  TEST_ASSERT_EQUAL_STRING("N", geo_cardinal(359.99));
  TEST_ASSERT_EQUAL_STRING("N", geo_cardinal(360));
  TEST_ASSERT_EQUAL_STRING("NW", geo_cardinal(-30));
}

// ---------------- buttons ----------------
static BtnEvent feed(ClickButton& b, bool p, uint32_t from, uint32_t to, BtnEvent* first = nullptr) {
  BtnEvent last = BtnEvent::None;
  for (uint32_t t = from; t < to; t++) {
    BtnEvent e = b.update(p, t);
    if (e != BtnEvent::None) {
      if (first && *first == BtnEvent::None) *first = e;
      last = e;
    }
  }
  return last;
}

static void test_click_bounce_ignored() {
  ClickButton b;
  feed(b, false, 0, 100);
  // 10 ms glitches, never stable for 25 ms
  for (uint32_t t = 100; t < 300; t += 20) {
    TEST_ASSERT_EQUAL(BtnEvent::None, feed(b, true, t, t + 10));
    TEST_ASSERT_EQUAL(BtnEvent::None, feed(b, false, t + 10, t + 20));
  }
  TEST_ASSERT_FALSE(b.isDown());
}

static void test_click() {
  ClickButton b;
  feed(b, false, 0, 100);
  TEST_ASSERT_EQUAL(BtnEvent::None, feed(b, true, 100, 300));
  TEST_ASSERT_TRUE(b.isDown());
  TEST_ASSERT_EQUAL(BtnEvent::Click, feed(b, false, 300, 400));
  TEST_ASSERT_FALSE(b.isDown());
}

static void test_long_and_verylong() {
  ClickButton b;
  feed(b, false, 0, 100);
  // press at 100: Long when held reaches 1000 ms
  TEST_ASSERT_EQUAL(BtnEvent::None, feed(b, true, 100, 1099));
  TEST_ASSERT_EQUAL(BtnEvent::Long, b.update(true, 1100));
  TEST_ASSERT_EQUAL(BtnEvent::None, feed(b, true, 1101, 4099));
  TEST_ASSERT_EQUAL(BtnEvent::VeryLong, b.update(true, 4100));
  TEST_ASSERT_EQUAL(BtnEvent::None, feed(b, true, 4101, 6000));
  TEST_ASSERT_EQUAL(BtnEvent::None, feed(b, false, 6000, 6200));  // nothing on release
  // and a later click still works
  feed(b, true, 7000, 7100);
  TEST_ASSERT_EQUAL(BtnEvent::Click, feed(b, false, 7100, 7200));
}

static void test_long_only_release() {
  ClickButton b;
  feed(b, false, 0, 10);
  BtnEvent first = BtnEvent::None;
  feed(b, true, 10, 2000, &first);
  TEST_ASSERT_EQUAL(BtnEvent::Long, first);
  TEST_ASSERT_EQUAL(BtnEvent::None, feed(b, false, 2000, 2100));
}

static StageEvent feed2(TwoStage& s, bool h, bool f, uint32_t from, uint32_t to) {
  StageEvent got = StageEvent::None;
  for (uint32_t t = from; t < to; t++) {
    StageEvent e = s.update(h, f, t);
    if (e != StageEvent::None) {
      TEST_ASSERT_EQUAL_MESSAGE(StageEvent::None, got, "more than one event in window");
      got = e;
    }
  }
  return got;
}

static void test_twostage_sequence() {
  TwoStage s;
  TEST_ASSERT_EQUAL(StageEvent::None, feed2(s, false, false, 0, 50));
  TEST_ASSERT_EQUAL(StageEvent::None, feed2(s, true, false, 50, 60));  // under debounce
  TEST_ASSERT_EQUAL(StageEvent::HalfDown, feed2(s, true, false, 60, 200));
  TEST_ASSERT_EQUAL(1, s.level());
  TEST_ASSERT_EQUAL(StageEvent::FullDown, feed2(s, true, true, 200, 300));
  TEST_ASSERT_EQUAL(2, s.level());
  TEST_ASSERT_EQUAL(StageEvent::FullUp, feed2(s, true, false, 300, 400));
  TEST_ASSERT_EQUAL(1, s.level());
  TEST_ASSERT_EQUAL(StageEvent::AllUp, feed2(s, false, false, 400, 500));
  TEST_ASSERT_EQUAL(0, s.level());
}

static void test_twostage_full_without_half() {
  TwoStage s;
  feed2(s, false, false, 0, 50);
  TEST_ASSERT_EQUAL(StageEvent::FullDown, feed2(s, false, true, 50, 200));
  TEST_ASSERT_EQUAL(2, s.level());
  TEST_ASSERT_EQUAL(StageEvent::AllUp, feed2(s, false, false, 200, 300));
}

static void test_twostage_bounce() {
  TwoStage s;
  feed2(s, false, false, 0, 50);
  for (uint32_t t = 50; t < 250; t += 20) {
    TEST_ASSERT_EQUAL(StageEvent::None, feed2(s, true, false, t, t + 8));
    TEST_ASSERT_EQUAL(StageEvent::None, feed2(s, false, false, t + 8, t + 20));
  }
}

// ---------------- battery ----------------
static void test_battery() {
  TEST_ASSERT_EQUAL(0, lipo_pct(2.9f));
  TEST_ASSERT_EQUAL(0, lipo_pct(3.30f));
  TEST_ASSERT_EQUAL(5, lipo_pct(3.60f));
  TEST_ASSERT_EQUAL(10, lipo_pct(3.65f));
  TEST_ASSERT_EQUAL(30, lipo_pct(3.75f));
  TEST_ASSERT_EQUAL(80, lipo_pct(4.00f));
  TEST_ASSERT_EQUAL(100, lipo_pct(4.15f));
  TEST_ASSERT_EQUAL(100, lipo_pct(4.30f));
  TEST_ASSERT_EQUAL(0, lipo_pct(NAN));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.9f * 1.045f, vbat_from_adc_mv(1000));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.f, vbat_from_adc_mv(0));
}

// ---------------- shot sequencer ----------------
struct Popped { CamCmd cmd; uint32_t at; };

static int run(ShotSequencer& s, uint32_t from, uint32_t to, Popped* out, int cap) {
  int n = 0;
  for (uint32_t t = from; t <= to; t++) {
    CamCmd c;
    while (s.poll(t, c)) {
      TEST_ASSERT_TRUE(n < cap);
      out[n++] = Popped{c, t};
    }
  }
  return n;
}

static void test_shot_one_press() {
  ShotSequencer s;
  TEST_ASSERT_TRUE(s.startShot(1000, 500));
  TEST_ASSERT_TRUE(s.busy());
  TEST_ASSERT_FALSE(s.startShot(1010, 500));  // ignored while busy
  Popped p[8];
  int n = run(s, 1000, 3000, p, 8);
  TEST_ASSERT_EQUAL(3, n);
  TEST_ASSERT_EQUAL(CamCmd::FocusPress, p[0].cmd);
  TEST_ASSERT_EQUAL_UINT32(1000, p[0].at);
  TEST_ASSERT_EQUAL(CamCmd::ShutterPress, p[1].cmd);
  TEST_ASSERT_EQUAL_UINT32(1500, p[1].at);
  TEST_ASSERT_EQUAL(CamCmd::Release, p[2].cmd);
  TEST_ASSERT_EQUAL_UINT32(1700, p[2].at);
  TEST_ASSERT_FALSE(s.busy());
  TEST_ASSERT_TRUE(s.startShot(4000, 500));
}

static void test_shot_af_locked() {
  ShotSequencer s;
  s.startShot(0, 800);
  Popped p[8];
  TEST_ASSERT_EQUAL(1, run(s, 0, 100, p, 8));
  s.afLocked(150);
  int n = run(s, 150, 1000, p, 8);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL(CamCmd::ShutterPress, p[0].cmd);
  TEST_ASSERT_EQUAL_UINT32(150, p[0].at);
  TEST_ASSERT_EQUAL(CamCmd::Release, p[1].cmd);
  TEST_ASSERT_EQUAL_UINT32(350, p[1].at);
  // afLocked when idle does nothing
  s.afLocked(2000);
  CamCmd c;
  TEST_ASSERT_FALSE(s.poll(2000, c));
}

static void test_shot_two_stage_passthrough() {
  ShotSequencer s;
  Popped p[8];
  s.half(0);
  TEST_ASSERT_FALSE(s.busy());
  TEST_ASSERT_EQUAL(1, run(s, 0, 10, p, 8));
  TEST_ASSERT_EQUAL(CamCmd::FocusPress, p[0].cmd);
  s.full(100);
  TEST_ASSERT_EQUAL(1, run(s, 100, 110, p, 8));  // already focusing: no second focus
  TEST_ASSERT_EQUAL(CamCmd::ShutterPress, p[0].cmd);
  s.release(200);
  TEST_ASSERT_EQUAL(1, run(s, 200, 210, p, 8));
  TEST_ASSERT_EQUAL(CamCmd::Release, p[0].cmd);
  // full without half: focus first, then shutter
  s.full(300);
  TEST_ASSERT_EQUAL(2, run(s, 300, 310, p, 8));
  TEST_ASSERT_EQUAL(CamCmd::FocusPress, p[0].cmd);
  TEST_ASSERT_EQUAL(CamCmd::ShutterPress, p[1].cmd);
}

static void test_shot_two_stage_cancels_one_press() {
  ShotSequencer s;
  Popped p[8];
  s.startShot(0, 500);
  TEST_ASSERT_EQUAL(1, run(s, 0, 50, p, 8));  // focus popped
  s.release(60);
  TEST_ASSERT_FALSE(s.busy());
  int n = run(s, 60, 2000, p, 8);
  TEST_ASSERT_EQUAL(1, n);  // shutter + pending release dropped
  TEST_ASSERT_EQUAL(CamCmd::Release, p[0].cmd);
  TEST_ASSERT_EQUAL_UINT32(60, p[0].at);
}

// ---------------- range log ----------------
static void test_range_csv() {
  char buf[256];
  size_t n = range_csv_header(buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING(
      "t_s,mode,sf,tx_dbm,hh_lat,hh_lon,cam_lat,cam_lon,dist_m,rssi_up,snr_up,rssi_dn,snr_dn,"
      "ble_rssi,rtt_ms,retries,stage\n",
      buf);
  TEST_ASSERT_EQUAL(strlen(buf), n);
  RangeRow r{};
  r.t_s = 42;
  r.mode = 1;
  r.sf = 9;
  r.tx_dbm = 14;
  r.hh_lat = 51.5074123;
  r.hh_lon = -0.1278;
  r.cam_lat = NAN;
  r.cam_lon = NAN;
  r.dist_m = NAN;
  r.rssi_up = -101.3f;
  r.snr_up = -3.5f;
  r.rssi_dn = NAN;
  r.snr_dn = NAN;
  r.ble_rssi = -60;
  r.rtt_ms = 180;
  r.retries = 1;
  r.stage = 3;
  n = range_csv_row(r, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("42,1,9,14,51.5074123,-0.1278000,,,,-101.3,-3.5,,,-60,180,1,3\n", buf);
  TEST_ASSERT_EQUAL(strlen(buf), n);
  TEST_ASSERT_EQUAL(0, range_csv_row(r, buf, 10));  // too small: 0, not a truncated row
}

// ---------------- regression cases ----------------
static void test_duty_snapshot_restore() {
  DutyCycle a(0.10f);
  a.record(5000, 1000);
  a.record(7000, 1000 + 125000);
  DutySnapshot snap;
  a.save(snap);
  DutyCycle b(0.10f);
  b.restore(snap);
  TEST_ASSERT_EQUAL_UINT32(12000, b.usedMs(1000 + 130000));
  TEST_ASSERT_EQUAL_UINT32(7000, b.usedMs(1000 + 3600000u + 60000u));
  DutyCycle c(0.10f);
  c.precharge(288000, 50);
  TEST_ASSERT_FALSE(c.canSend(1000, 60, false));  // 80 % already booked: pings held
  TEST_ASSERT_TRUE(c.canSend(1000, 60, true));    // commands still have the reserve
}

static void test_cmd_first_ctr_and_ids() {
  TEST_ASSERT_EQUAL_UINT32(1025, cmd_first_ctr(1025, (uint16_t)1025));
  TEST_ASSERT_EQUAL_UINT32(1025, cmd_first_ctr(1028, (uint16_t)1025));        // retry 3
  TEST_ASSERT_EQUAL_UINT32(0x1FFFF, cmd_first_ctr(0x20002, (uint16_t)0xFFFF));  // seq wrap
  // Same low 16 bits on different wakes are different commands.
  SeqDedupe d;
  TEST_ASSERT_TRUE(d.firstTimeId(1 + 1024u * 64));
  TEST_ASSERT_TRUE(d.firstTimeId(1 + 1024u * 128));
  TEST_ASSERT_FALSE(d.firstTimeId(1 + 1024u * 64));
  TEST_ASSERT_EQUAL_UINT16((uint16_t)(1 + 1024u * 64), (uint16_t)(1 + 1024u * 128));
}

static void test_link_timing() {
  TEST_ASSERT_EQUAL_UINT32(2000, ping_interval_ms(50, 40, 2000));
  TEST_ASSERT_EQUAL_UINT32(20 * (1483 + 2138), ping_interval_ms(1483, 2138, 2000));
  TEST_ASSERT_EQUAL_UINT32(500, hunt_interval_ms(50, 40, 500));
  TEST_ASSERT_EQUAL_UINT32(4 * (1483 + 2138), hunt_interval_ms(1483, 2138, 500));
  TEST_ASSERT_EQUAL_UINT32(8000, rescue_silence_ms(2000, 50, 8000));
  // Three missed ping slots, as the handheld's own 3-miss rescue rule.
  TEST_ASSERT_EQUAL_UINT32(3 * 72420 + 1483, rescue_silence_ms(72420, 1483, 8000));
  TEST_ASSERT_EQUAL_UINT32(3 * 3100 + 62, rescue_silence_ms(3100, 62, 8000));
  // Round-trip pinging stays at or below ~5 % of the hour on each board.
  const uint32_t iv = ping_interval_ms(1483, 2138, 2000);
  TEST_ASSERT_TRUE((double)2138 / iv <= 0.05 + 1e-9);
}

static void test_shot_button_state() {
  ShotSequencer s;
  s.startShot(0, 500);
  CamCmd c;
  TEST_ASSERT_TRUE(s.poll(0, c));  // FocusPress
  TEST_ASSERT_TRUE(s.buttonDown());
  TEST_ASSERT_FALSE(s.shutterDown());
  TEST_ASSERT_TRUE(s.releaseQueued());
  TEST_ASSERT_TRUE(s.poll(500, c));  // ShutterPress
  TEST_ASSERT_EQUAL(CamCmd::ShutterPress, c);
  TEST_ASSERT_TRUE(s.shutterDown());
  TEST_ASSERT_TRUE(s.releaseQueued());
  TEST_ASSERT_FALSE(s.poll(600, c));  // Release not due yet
  TEST_ASSERT_TRUE(s.poll(700, c));
  TEST_ASSERT_EQUAL(CamCmd::Release, c);
  TEST_ASSERT_FALSE(s.buttonDown());
  TEST_ASSERT_FALSE(s.shutterDown());
  TEST_ASSERT_FALSE(s.releaseQueued());
  s.full(1000);  // two-stage: shutter held, no Release queued until the user lets go
  TEST_ASSERT_TRUE(s.poll(1000, c));
  TEST_ASSERT_TRUE(s.poll(1000, c));
  TEST_ASSERT_TRUE(s.shutterDown());
  TEST_ASSERT_FALSE(s.releaseQueued());
}


// ---------------- round-1 fixes ----------------
static void test_ack_id_binding() {
  AckPayload a{};
  a.ack_seq = 0x1234;
  a.ack_type = MsgType::Shot;
  a.stage = ShotStage::CamEvent;
  a.duty_permille = 812;
  a.ack_id_hi = 0x0003;
  uint8_t buf[ACK_PAYLOAD_LEN];
  TEST_ASSERT_EQUAL_UINT32(ACK_PAYLOAD_LEN, ack_pack(a, buf));
  TEST_ASSERT_EQUAL_UINT8(0, buf[24]);  // still 2 reserved zero bytes
  TEST_ASSERT_EQUAL_UINT8(0, buf[25]);
  AckPayload b;
  TEST_ASSERT_TRUE(ack_unpack(buf, sizeof buf, b));
  TEST_ASSERT_EQUAL_UINT16(0x0003, b.ack_id_hi);
  TEST_ASSERT_EQUAL_UINT16(812, b.duty_permille);
  // Two commands 65536 apart share seq; only the right one matches.
  const uint32_t idA = 0x00021234, idB = 0x00031234;
  TEST_ASSERT_EQUAL_UINT16((uint16_t)idA, (uint16_t)idB);
  TEST_ASSERT_TRUE(ack_matches_id(b.ack_seq, b.ack_id_hi, idB));
  TEST_ASSERT_FALSE(ack_matches_id(b.ack_seq, b.ack_id_hi, idA));
  TEST_ASSERT_FALSE(ack_matches_id(0x1235, b.ack_id_hi, idB));
  // Retry id reconstruction feeds the same id_hi across a 16-bit wrap.
  TEST_ASSERT_EQUAL_UINT16(1, cmd_id_hi(cmd_first_ctr(0x20002, (uint16_t)0xFFFF)));
}

static void test_rescue_probe_sf() {
  // Even probes SF12, odd sweep from the link SF.
  TEST_ASSERT_EQUAL_UINT8(12, rescue_probe_sf(0, 9));
  TEST_ASSERT_EQUAL_UINT8(9, rescue_probe_sf(1, 9));
  TEST_ASSERT_EQUAL_UINT8(12, rescue_probe_sf(2, 9));
  TEST_ASSERT_EQUAL_UINT8(10, rescue_probe_sf(3, 9));
  TEST_ASSERT_EQUAL_UINT8(11, rescue_probe_sf(5, 9));
  TEST_ASSERT_EQUAL_UINT8(7, rescue_probe_sf(7, 9));
  TEST_ASSERT_EQUAL_UINT8(8, rescue_probe_sf(9, 9));
  TEST_ASSERT_EQUAL_UINT8(9, rescue_probe_sf(11, 9));  // full sweep wraps
  // Link already on 12: sweep from SF7.
  TEST_ASSERT_EQUAL_UINT8(7, rescue_probe_sf(1, 12));
  TEST_ASSERT_EQUAL_UINT8(11, rescue_probe_sf(9, 12));
  // A SetSf to 11 went unanswered from SF10: SF11 is the first odd probe, then the sweep
  // from the old SF (which is not skipped).
  TEST_ASSERT_EQUAL_UINT8(12, rescue_probe_sf(0, 10, 11));
  TEST_ASSERT_EQUAL_UINT8(11, rescue_probe_sf(1, 10, 11));
  TEST_ASSERT_EQUAL_UINT8(10, rescue_probe_sf(3, 10, 11));
  TEST_ASSERT_EQUAL_UINT8(11, rescue_probe_sf(5, 10, 11));
  TEST_ASSERT_EQUAL_UINT8(9, rescue_probe_sf(1, 9, 0));   // 0 = no pending SF
  TEST_ASSERT_EQUAL_UINT8(9, rescue_probe_sf(1, 9, 12));  // SF12 is already every even probe
  // Each probe is paced by its own SF: an SF7 probe is not held behind SF12's interval.
  // The Ack is 47 bytes since utc was added (the planner constant follows proto.h).
  const uint32_t iv7 = ping_interval_ms(lora_airtime_ms(7, 125, 5, 8, PLAN_PING_FRAME),
                                        lora_airtime_ms(7, 125, 5, 8, PLAN_ACK_FRAME), 2000);
  const uint32_t iv12 = ping_interval_ms(lora_airtime_ms(12, 125, 5, 8, PLAN_PING_FRAME),
                                         lora_airtime_ms(12, 125, 5, 8, PLAN_ACK_FRAME), 2000);
  TEST_ASSERT_TRUE(iv7 < 5000);
  TEST_ASSERT_TRUE(iv12 > 60000);  // longer than the handheld's idle timeout: hence probe_now
}

static void test_tx_first_try_expiry() {
  // Shoot queued at 0 behind a Focus that takes 3.5 s at SF12: it became head at 3500.
  TEST_ASSERT_EQUAL(TxExpiry::None, tx_first_try_expiry(3600, 0, 3500, 0, 3000, 10000));
  TEST_ASSERT_EQUAL(TxExpiry::None, tx_first_try_expiry(6400, 0, 3500, 0, 3000, 10000));
  TEST_ASSERT_EQUAL(TxExpiry::AtHead, tx_first_try_expiry(6500, 0, 3500, 0, 3000, 10000));
  // Stuck behind a Release retrying for 30 s: never head, gives up at max_queue (no late fire).
  TEST_ASSERT_EQUAL(TxExpiry::None, tx_first_try_expiry(9999, 0, 9999, 0, 3000, 10000));
  TEST_ASSERT_EQUAL(TxExpiry::Queued, tx_first_try_expiry(10000, 0, 9999, 0, 3000, 10000));
  // Queued during the boot hold-off: both clocks start at its end.
  TEST_ASSERT_EQUAL(TxExpiry::None, tx_first_try_expiry(4900, 100, 100, 2000, 3000, 10000));
  TEST_ASSERT_EQUAL(TxExpiry::AtHead, tx_first_try_expiry(5000, 100, 100, 2000, 3000, 10000));
  // millis() wrap.
  const uint32_t q = 0xFFFFF000u;
  TEST_ASSERT_EQUAL(TxExpiry::None, tx_first_try_expiry(q + 2000, q, q, q - 5000, 3000, 10000));
  TEST_ASSERT_EQUAL(TxExpiry::AtHead, tx_first_try_expiry(q + 3000, q, q, q - 5000, 3000, 10000));
}

static void test_fuji_notify_evidence() {
  const uint8_t configured[] = {0x02, 0x00};
  const uint8_t other[] = {0x01, 0x00};
  // Link ready at 10000; the camera's own status traffic right after that is not a shot.
  TEST_ASSERT_FALSE(fuji_notify_is_shot_evidence(true, false, other, 2, 10100, 10000));
  TEST_ASSERT_FALSE(fuji_notify_is_shot_evidence(true, false, other, 2, 12999, 10000));
  TEST_ASSERT_TRUE(fuji_notify_is_shot_evidence(true, false, other, 2, 13000, 10000));
  // NOT1 = 02 00 ("configured") never counts, however late.
  TEST_ASSERT_FALSE(fuji_notify_is_shot_evidence(true, true, configured, 2, 50000, 10000));
  TEST_ASSERT_TRUE(fuji_notify_is_shot_evidence(true, true, other, 2, 50000, 10000));
  // Not ready (handshake) or not an evidence characteristic: never.
  TEST_ASSERT_FALSE(fuji_notify_is_shot_evidence(true, false, other, 2, 50000, 0));
  TEST_ASSERT_FALSE(fuji_notify_is_shot_evidence(false, false, other, 2, 50000, 10000));
  // millis() wrap.
  TEST_ASSERT_TRUE(fuji_notify_is_shot_evidence(true, false, other, 2, 2000, 0xFFFFF000u));
}

static void test_duty_cold_precharge() {
  DutyCycle d(0.10f);
  TEST_ASSERT_EQUAL_UINT32(360000, d.budgetMs());
  // The extra booked on top of the saved 250 s: the unsaved bound, capped at the budget.
  TEST_ASSERT_EQUAL_UINT32(20500, duty_cold_precharge_ms(250000, 20500, d.budgetMs()));
  TEST_ASSERT_EQUAL_UINT32(10000, duty_cold_precharge_ms(350000, 20500, d.budgetMs()));
  TEST_ASSERT_EQUAL_UINT32(0, duty_cold_precharge_ms(360000, 20500, d.budgetMs()));
  TEST_ASSERT_EQUAL_UINT32(20500, duty_cold_precharge_ms(0, 20500, d.budgetMs()));
  DutySnapshot s{};
  s.bucket_ms[5] = 350000;
  s.init = 1;
  TEST_ASSERT_EQUAL_UINT32(10000, duty_cold_start(d, &s, true, 20500, 0));
  TEST_ASSERT_FALSE(d.canSend(1, 10, true));  // whole hour treated as used
}

// Airtime sent at the very end of minute 0 is inside the true sliding hour until t = 3659 s.
static void test_duty_true_rolling_hour() {
  DutyCycle d(0.10f);
  d.record(0, 0);           // bucket 0 starts at t = 0
  d.record(30000, 59000);   // 30 s at the end of minute 0
  d.record(330000, 60000);  // the rest of the budget in minute 1
  for (uint32_t t = 3600000; t <= 3659000; t += 1000) {
    TEST_ASSERT_EQUAL_UINT32(360000, d.usedMs(t));
    TEST_ASSERT_FALSE(d.canSend(1, t, true));
  }
  TEST_ASSERT_EQUAL_UINT32(330000, d.usedMs(3660000));
  TEST_ASSERT_TRUE(d.canSend(30000, 3660000, true));
}

// N power cuts with nothing transmitted in between book the unsaved bound once, not N times.
static void test_duty_cold_start_no_ratchet() {
  constexpr uint32_t BOUND = 26500;
  DutyCycle a(0.10f);
  a.record(100000, 1000);
  DutySnapshot nvs;
  a.save(nvs);
  bool dirty = true;  // airtime was recorded after that snapshot
  for (int boot = 0; boot < 20; boot++) {
    DutyCycle b(0.10f);
    const uint32_t now = 5000u + (uint32_t)boot;  // the clock restarts on every power-on
    const uint32_t extra = duty_cold_start(b, &nvs, dirty, BOUND, now);
    TEST_ASSERT_EQUAL_UINT32(boot == 0 ? BOUND : 0, extra);
    TEST_ASSERT_EQUAL_UINT32(100000 + BOUND, b.usedMs(now));
    TEST_ASSERT_TRUE(b.canSend(1000, now, false));  // never locked out by boots alone
    if (dirty) {  // duty_begin persists the result and clears the flag
      b.save(nvs);
      dirty = false;
    }
  }
  // The restored history keeps aging: an hour of uptime later it has rolled off.
  DutyCycle c(0.10f);
  duty_cold_start(c, &nvs, false, BOUND, 0);
  TEST_ASSERT_EQUAL_UINT32(100000 + BOUND, c.usedMs(30 * 60000u));
  TEST_ASSERT_EQUAL_UINT32(0, c.usedMs(62 * 60000u));
  // No snapshot at all, dirty: just the bound.
  DutyCycle e(0.10f);
  TEST_ASSERT_EQUAL_UINT32(BOUND, duty_cold_start(e, nullptr, true, BOUND, 7));
  TEST_ASSERT_EQUAL_UINT32(BOUND, e.usedMs(7));
}

// The one-press AF wait and shutter hold count from the end of each blocking write.
static void test_shot_step_done_rebases() {
  ShotSequencer s;
  s.startShot(0, 500);
  CamCmd c;
  TEST_ASSERT_TRUE(s.poll(0, c));
  TEST_ASSERT_EQUAL(CamCmd::FocusPress, c);
  s.stepDone(CamCmd::FocusPress, 2000);  // focus write (after a 2 s Ack) returned at 2000
  TEST_ASSERT_FALSE(s.poll(2499, c));
  TEST_ASSERT_TRUE(s.poll(2500, c));
  TEST_ASSERT_EQUAL(CamCmd::ShutterPress, c);
  s.stepDone(CamCmd::ShutterPress, 2600);
  TEST_ASSERT_FALSE(s.poll(2799, c));
  TEST_ASSERT_TRUE(s.poll(2800, c));
  TEST_ASSERT_EQUAL(CamCmd::Release, c);
  // A fast write never moves a step earlier.
  s.startShot(10000, 500);
  TEST_ASSERT_TRUE(s.poll(10000, c));
  s.stepDone(CamCmd::FocusPress, 10001);
  TEST_ASSERT_FALSE(s.poll(10500, c));
  TEST_ASSERT_TRUE(s.poll(10501, c));
  // Two-stage steps are never re-timed.
  ShotSequencer t;
  t.half(0);
  TEST_ASSERT_TRUE(t.poll(0, c));
  t.stepDone(CamCmd::FocusPress, 5000);
  t.full(100);
  TEST_ASSERT_TRUE(t.poll(100, c));
  TEST_ASSERT_EQUAL(CamCmd::ShutterPress, c);
}

// Inside the post-ready settle window a notification counts once a shutter write has begun on
// this link (wake / deferred shots), never before it.
static void test_fuji_notify_after_shutter() {
  const uint8_t other[] = {0x01, 0x00};
  const uint8_t configured[] = {0x02, 0x00};
  TEST_ASSERT_FALSE(fuji_notify_is_shot_evidence(true, false, other, 2, 10400, 10000, 0));
  TEST_ASSERT_FALSE(fuji_notify_is_shot_evidence(true, false, other, 2, 10400, 10000, 10500));
  TEST_ASSERT_TRUE(fuji_notify_is_shot_evidence(true, false, other, 2, 10800, 10000, 10500));
  // A shutter write from the previous link does not open the window.
  TEST_ASSERT_FALSE(fuji_notify_is_shot_evidence(true, false, other, 2, 10800, 10000, 9000));
  // The configured event never counts.
  TEST_ASSERT_FALSE(fuji_notify_is_shot_evidence(true, true, configured, 2, 10800, 10000, 10500));
}

// A timed Shoot (relay one-button "shoot now"): this board lets go hold_ms after the shutter
// write returns, with no second command. A Release that comes sooner still wins.
static void test_shot_full_timed_lets_go_by_itself() {
  ShotSequencer s;
  s.fullTimed(1000, 200);
  TEST_ASSERT_FALSE(s.busy());  // not a one-press: a Shot may still replace it
  CamCmd c;
  TEST_ASSERT_TRUE(s.poll(1000, c));
  TEST_ASSERT_EQUAL(CamCmd::FocusPress, c);
  s.stepDone(c, 1300);  // a slow focus write: the shutter is not delayed by an AF wait
  TEST_ASSERT_TRUE(s.poll(1300, c));
  TEST_ASSERT_EQUAL(CamCmd::ShutterPress, c);
  TEST_ASSERT_TRUE(s.shutterDown());
  TEST_ASSERT_TRUE(s.releaseQueued());  // the camera board's watchdog treats it as timed
  s.stepDone(c, 2100);                  // shutter write took 800 ms: hold from its end
  TEST_ASSERT_FALSE(s.poll(2299, c));
  TEST_ASSERT_TRUE(s.poll(2300, c));
  TEST_ASSERT_EQUAL(CamCmd::Release, c);
  TEST_ASSERT_FALSE(s.buttonDown());
  // The backup Release from the handheld arrives later: one more Release, harmless.
  s.release(2600);
  TEST_ASSERT_TRUE(s.poll(2600, c));
  TEST_ASSERT_EQUAL(CamCmd::Release, c);
  TEST_ASSERT_FALSE(s.poll(5000, c));

  // A Release before the timed one lets go at once; nothing is left queued.
  ShotSequencer t;
  t.half(0);
  TEST_ASSERT_TRUE(t.poll(0, c));
  t.fullTimed(100, 500);  // already focusing: no second FocusPress
  TEST_ASSERT_TRUE(t.poll(100, c));
  TEST_ASSERT_EQUAL(CamCmd::ShutterPress, c);
  t.stepDone(c, 100);
  t.release(150);
  TEST_ASSERT_TRUE(t.poll(150, c));
  TEST_ASSERT_EQUAL(CamCmd::Release, c);
  TEST_ASSERT_FALSE(t.poll(10000, c));

  // A plain full() still waits for its Release (held BURST/BULB keep working).
  ShotSequencer u;
  u.full(0);
  TEST_ASSERT_TRUE(u.poll(0, c));
  TEST_ASSERT_TRUE(u.poll(0, c));
  u.stepDone(c, 10);
  TEST_ASSERT_FALSE(u.releaseQueued());
  TEST_ASSERT_FALSE(u.poll(60000, c));
  TEST_ASSERT_TRUE(u.shutterDown());
}

static void test_shoot_payload_hold() {
  uint8_t pl[6] = {1, 2, 3, 4, 0, 0};
  TEST_ASSERT_EQUAL_UINT16(0, shoot_hold_ms(pl, 4));  // old 4-byte Shoot: held
  shoot_put_hold(pl, 200);
  TEST_ASSERT_EQUAL_UINT8(1, pl[0]);  // the echo is untouched
  TEST_ASSERT_EQUAL_UINT8(4, pl[3]);
  TEST_ASSERT_EQUAL_UINT16(200, shoot_hold_ms(pl, SHOOT_PAYLOAD_LEN_TIMED));
  shoot_put_hold(pl, 60000);
  TEST_ASSERT_EQUAL_UINT16(SHOOT_MAX_HOLD_MS, shoot_hold_ms(pl, 6));  // clamped
  // Round trip through a signed frame.
  uint8_t key[32] = {7};
  Frame f{};
  f.type = MsgType::Shoot;
  f.net = 1;
  f.ctr = 9;
  f.seq = 9;
  f.len = 6;
  shoot_put_hold(f.payload, 200);
  uint8_t buf[PROTO_MAX_FRAME];
  const size_t n = frame_encode(f, key, buf, sizeof buf);
  Frame g{};
  TEST_ASSERT_TRUE(frame_decode(buf, n, key, g));
  TEST_ASSERT_EQUAL_UINT16(200, shoot_hold_ms(g.payload, g.len));
}

// ---------------- mode engine (C3, C6, C7) ----------------
// A small driver for the one-button engine: 5 ms steps, PRG only (the full harness lives in
// test_onebutton).
namespace {
struct EngSim {
  ModeEngine eng;
  PressClassifier pc;
  ObEnv env;
  bool down = false;
  int shots = 0, shoots = 0, focuses = 0, releases = 0;
  uint16_t last_tag = 0;
  ObScreen scr;
  void setup(const ObSettings& s, bool relay, uint8_t sf) {
    eng.configure(s);
    env.now = 1000;
    env.relay = relay;
    env.sf = sf;
    pc.setTable(eng.table());
  }
  void step(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 5) {
      env.now += 5;
      PressEvent ev = pc.update(down, env.now);
      if (ev.type != PressEvt::None) eng.onPress(ev, env);
      eng.tick(env);
      pc.setTable(eng.table());
      ObAction a;
      while (eng.popAction(a)) {
        if (a.act == ObAct::Shot) shots++, last_tag = a.tag;
        else if (a.act == ObAct::Shoot) shoots++, last_tag = a.tag;
        else if (a.act == ObAct::Focus) focuses++;
        else if (a.act == ObAct::Release) releases++;
      }
    }
  }
  void press(uint32_t hold_ms, uint32_t after = 100) {
    down = true;
    step(hold_ms);
    down = false;
    step(after);
  }
  const ObScreen& screen() {
    eng.screen(scr, env);
    return scr;
  }
};
}  // namespace

static void test_plan_frames_match_proto() {
  // C3: the planner costs the frames the boards really send.
  TEST_ASSERT_EQUAL_UINT(PROTO_HDR + ACK_PAYLOAD_LEN + PROTO_TAG, PLAN_ACK_FRAME);
  TEST_ASSERT_EQUAL_UINT(47, PLAN_ACK_FRAME);
  TEST_ASSERT_EQUAL_UINT(PROTO_HDR + PING_PAYLOAD_LEN + PROTO_TAG, PLAN_PING_FRAME);
  TEST_ASSERT_EQUAL_UINT(PROTO_HDR + SHOOT_PAYLOAD_LEN_TIMED + PROTO_TAG, PLAN_SHOT_FRAME);
  // Camera side of a relayed shot = Ack + Result, both 47 bytes.
  TEST_ASSERT_EQUAL_UINT32(2 * lora_airtime_ms(12, 125, 5, 8, 47), plan_shot_cost_ms(12, true));
  TEST_ASSERT_EQUAL_UINT32(2 * lora_airtime_ms(8, 125, 5, 8, 47), plan_shot_cost_ms(8, true));
}

static void iv_start_one_shot(EngSim& s, bool relay, uint8_t sf) {
  ObSettings st;
  st.iv_every_s = 30;
  st.iv_count = 1;
  s.setup(st, relay, sf);
  s.eng.setMode(ObMode::Interval);
  s.press(100);   // ARM screen
  s.press(1100);  // hold 1 s: start, the only shot goes out at once
}

static void test_interval_done_waits_for_sf12_outcome() {
  // C6: at SF12 a clean relayed shot is reported about 13 s after it left. The run must
  // still be waiting then, and count it.
  EngSim s;
  iv_start_one_shot(s, true, 12);
  TEST_ASSERT_EQUAL(1, s.shots);
  TEST_ASSERT_TRUE(s.eng.running());
  s.step(12900);  // about 13.1 s after the shot
  TEST_ASSERT_TRUE(s.eng.running());
  s.eng.onOutcome(s.last_tag, ObOutcome::Ok, s.env);
  s.step(20);
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL_STRING("DONE", s.screen().big);
  TEST_ASSERT_EQUAL_STRING("1 of 1 taken", s.screen().l1);
  TEST_ASSERT_EQUAL(1, s.shots);
}

static void test_interval_done_wait_is_bounded() {
  // C6: an outcome that never comes still ends the run. Relay SF12: within a minute.
  EngSim s;
  iv_start_one_shot(s, true, 12);
  s.step(60000);
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL_STRING("DONE", s.screen().big);
  TEST_ASSERT_EQUAL_STRING("0 of 1 taken", s.screen().l1);
  // Direct keeps the 10 s wait. Relay at SF7 is sized for SF12 too (a try can go out on SF12
  // and the rest of the exchange then stays there), so it is bounded the same way.
  EngSim d;
  iv_start_one_shot(d, false, 7);
  d.step(9500);
  TEST_ASSERT_TRUE(d.eng.running());
  d.step(700);
  TEST_ASSERT_FALSE(d.eng.running());
  EngSim f;
  iv_start_one_shot(f, true, 7);
  f.step(13800);  // an SF12 try answered late: still inside the wait
  TEST_ASSERT_TRUE(f.eng.running());
  f.step(46200);
  TEST_ASSERT_FALSE(f.eng.running());
  // An outcome that arrives early ends the run at once, whatever the SF.
  EngSim q;
  iv_start_one_shot(q, true, 12);
  q.step(500);
  q.eng.onOutcome(q.last_tag, ObOutcome::Ok, q.env);
  q.step(20);
  TEST_ASSERT_FALSE(q.eng.running());
  TEST_ASSERT_EQUAL_STRING("1 of 1 taken", q.screen().l1);
}

static void test_aflock_timeout_under_a_press_does_not_relock() {
  // C7: the lock runs out while a tap that began on LOCKED is still down. Letting go must
  // not send a new Focus (a fresh lock): AF is released and nothing fires.
  EngSim s;
  ObSettings st;
  st.aflock_s = 5;
  s.setup(st, false, 7);
  s.eng.setMode(ObMode::AfLock);
  s.press(100);
  TEST_ASSERT_EQUAL(1, s.focuses);
  TEST_ASSERT_TRUE(s.eng.held());
  s.step(4700);  // about 100 ms of lock left
  TEST_ASSERT_TRUE(s.eng.held());
  s.press(300, 500);  // down before the timeout, up after it
  TEST_ASSERT_EQUAL(1, s.focuses);
  TEST_ASSERT_EQUAL(0, s.shoots + s.shots);
  TEST_ASSERT_EQUAL(1, s.releases);
  TEST_ASSERT_FALSE(s.eng.held());
  TEST_ASSERT_EQUAL_STRING("AF RELEASED", s.screen().big);
  // The next tap is an ordinary one: it locks again, and a tap on that lock still fires.
  s.press(100);
  TEST_ASSERT_EQUAL(2, s.focuses);
  TEST_ASSERT_TRUE(s.eng.held());
  s.press(100, 400);
  TEST_ASSERT_EQUAL(1, s.shoots);
  TEST_ASSERT_FALSE(s.eng.held());
}

// ---------------------------------------------------------------- regions

static void test_airtime_500khz() {
  // US channel: 500 kHz, CR 4/5, preamble 8. Exact Semtech figures, rounded up.
  TEST_ASSERT_EQUAL_UINT32(15, lora_airtime_ms(7, 500, 5, 8, 20));
  TEST_ASSERT_EQUAL_UINT32(21, lora_airtime_ms(7, 500, 5, 8, 40));
  TEST_ASSERT_EQUAL_UINT32(26, lora_airtime_ms(8, 500, 5, 8, 20));
  TEST_ASSERT_EQUAL_UINT32(72, lora_airtime_ms(9, 500, 5, 8, 40));
  TEST_ASSERT_EQUAL_UINT32(93, lora_airtime_ms(10, 500, 5, 8, 20));
  TEST_ASSERT_EQUAL_UINT32(330, lora_airtime_ms(12, 500, 5, 8, 20));
}

static void test_airtime_ldro_follows_symbol_time_not_sf() {
  // At 500 kHz the SF11 and SF12 symbols are 4.1 and 8.2 ms, under the 16 ms line, so low
  // data-rate optimisation is off. Decided from SF alone these would read 186, 268 and 494.
  TEST_ASSERT_EQUAL_UINT32(165, lora_airtime_ms(11, 500, 5, 8, 20));
  TEST_ASSERT_EQUAL_UINT32(247, lora_airtime_ms(11, 500, 5, 8, 40));
  TEST_ASSERT_EQUAL_UINT32(453, lora_airtime_ms(12, 500, 5, 8, 40));
  // 125 kHz keeps it on at SF11 and SF12 (EU, unchanged) and off at SF10.
  assert_within_2pct(823.3, lora_airtime_ms(11, 125, 5, 8, 25));
  assert_within_2pct(411.6, lora_airtime_ms(10, 125, 5, 8, 25));
  // 250 kHz: on at SF12 only (16.4 ms symbol), which SF alone would also get wrong at SF11.
  TEST_ASSERT_EQUAL_UINT32(330, lora_airtime_ms(11, 250, 5, 8, 20));
}

static void test_region_table() {
  const RegionCfg& eu = region_cfg(Region::EU868);
  TEST_ASSERT_EQUAL_FLOAT(869.525f, eu.freq_mhz);
  TEST_ASSERT_EQUAL_FLOAT(125.0f, eu.bw_khz);
  TEST_ASSERT_EQUAL_FLOAT(0.10f, eu.duty_limit);
  TEST_ASSERT_EQUAL(PowerRule::Erp, eu.rule);
  TEST_ASSERT_EQUAL(27, eu.limit_dbm);
  const RegionCfg& us = region_cfg(Region::US915);
  TEST_ASSERT_EQUAL_FLOAT(918.0f, us.freq_mhz);
  TEST_ASSERT_EQUAL_FLOAT(500.0f, us.bw_khz);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, us.duty_limit);
  TEST_ASSERT_EQUAL(PowerRule::Conducted, us.rule);
  TEST_ASSERT_EQUAL(25, us.limit_dbm);
  TEST_ASSERT_EQUAL(6, us.full_gain_dbi);
  // The whole US channel sits inside 902 to 928 MHz, the EU one inside 869.40 to 869.65.
  TEST_ASSERT_TRUE(us.freq_mhz - us.bw_khz / 2000 >= 902.0f && us.freq_mhz + us.bw_khz / 2000 <= 928.0f);
  TEST_ASSERT_TRUE(eu.freq_mhz - eu.bw_khz / 2000 >= 869.40f && eu.freq_mhz + eu.bw_khz / 2000 <= 869.65f);
  // Unset: the strictest figures for the maths, and never allowed on air.
  const RegionCfg& un = region_cfg(Region::Unset);
  TEST_ASSERT_EQUAL_FLOAT(125.0f, un.bw_khz);
  TEST_ASSERT_EQUAL_FLOAT(0.10f, un.duty_limit);
  TEST_ASSERT_FALSE(region_can_tx(Region::Unset));
  TEST_ASSERT_TRUE(region_can_tx(Region::EU868));
  TEST_ASSERT_TRUE(region_can_tx(Region::US915));
  TEST_ASSERT_FALSE(region_can_tx((Region)3));
}

static void test_region_names_and_menu_step() {
  TEST_ASSERT_EQUAL_STRING("Choose", region_label(Region::Unset));
  TEST_ASSERT_EQUAL_STRING("EU 868", region_label(Region::EU868));
  TEST_ASSERT_EQUAL_STRING("US 915", region_label(Region::US915));
  TEST_ASSERT_EQUAL_STRING("unset", region_key(Region::Unset));
  TEST_ASSERT_EQUAL_STRING("eu", region_key(Region::EU868));
  TEST_ASSERT_EQUAL_STRING("us", region_key(Region::US915));
  Region r = Region::Unset;
  TEST_ASSERT_TRUE(region_from_key("us", r));
  TEST_ASSERT_EQUAL(Region::US915, r);
  TEST_ASSERT_TRUE(region_from_key("eu", r));
  TEST_ASSERT_EQUAL(Region::EU868, r);
  // The console can never put a board back to "not chosen", and junk changes nothing.
  TEST_ASSERT_FALSE(region_from_key("unset", r));
  TEST_ASSERT_FALSE(region_from_key("", r));
  TEST_ASSERT_FALSE(region_from_key("EU", r));
  TEST_ASSERT_FALSE(region_from_key(nullptr, r));
  TEST_ASSERT_EQUAL(Region::EU868, r);
  // Menu: Choose -> EU 868 -> US 915 -> EU 868, never back to Choose.
  TEST_ASSERT_EQUAL(Region::EU868, region_next(Region::Unset));
  TEST_ASSERT_EQUAL(Region::US915, region_next(Region::EU868));
  TEST_ASSERT_EQUAL(Region::EU868, region_next(Region::US915));
}

static void test_region_on_load_migration() {
  // A board set up before regions existed (role stored, no region key) was EU: it stays EU.
  TEST_ASSERT_EQUAL(Region::EU868, region_on_load(false, 0, true));
  // A fresh or factory-reset board has no role: not chosen, radio silent.
  TEST_ASSERT_EQUAL(Region::Unset, region_on_load(false, 0, false));
  // A stored key always wins, whatever the role.
  TEST_ASSERT_EQUAL(Region::US915, region_on_load(true, 2, true));
  TEST_ASSERT_EQUAL(Region::US915, region_on_load(true, 2, false));
  TEST_ASSERT_EQUAL(Region::EU868, region_on_load(true, 1, false));
  // Stored as not chosen (a role picked before a region): stays not chosen, never EU by default.
  TEST_ASSERT_EQUAL(Region::Unset, region_on_load(true, 0, true));
  // A value this build does not know is treated as not chosen (silent), not as some band.
  TEST_ASSERT_EQUAL(Region::Unset, region_on_load(true, 3, true));
  TEST_ASSERT_EQUAL(Region::Unset, region_on_load(true, 255, true));
  TEST_ASSERT_TRUE(region_valid(0) && region_valid(1) && region_valid(2));
  TEST_ASSERT_FALSE(region_valid(3));
}

// The board's fixed figures (config.h): +11 dB front end, SX1262 setting held to -9..14.
// A two-key model of the settings store, saved the way store_save does it: region first,
// role only once the region key is in flash. fail_region / stop_after_first model a failed
// write and a reset between the two writes.
struct FakeNvs {
  bool has_role = false, has_region = false;
  uint8_t role = 0, region = 0;
};
struct FakeStore {
  FakeNvs nvs;
  uint8_t role = 0;         // 0 = no role
  Region region = Region::Unset;
  bool region_stored = false;
  void load() {
    role = nvs.has_role ? nvs.role : 0;
    region = region_on_load(nvs.has_region, nvs.region, role != 0);
    region_stored = nvs.has_region;
  }
  bool save(uint8_t new_role, Region new_region, bool fail_region = false, bool stop_after_first = false) {
    bool ok = true;
    if (region_must_write(new_region != region, region_stored)) {
      if (!fail_region) { nvs.has_region = true; nvs.region = (uint8_t)new_region; }
      region_stored = !fail_region;
      ok &= region_stored;
      if (stop_after_first) return false;
    }
    if (new_role != role) {
      if (region_role_may_write(region_stored)) { nvs.has_role = true; nvs.role = new_role; }
      else ok = false;
    }
    return ok;
  }
};

static void test_region_save_pins_key_before_role() {
  TEST_ASSERT_TRUE(region_must_write(true, true));
  TEST_ASSERT_TRUE(region_must_write(true, false));
  TEST_ASSERT_TRUE(region_must_write(false, false));
  TEST_ASSERT_FALSE(region_must_write(false, true));
  TEST_ASSERT_TRUE(region_role_may_write(true));
  TEST_ASSERT_FALSE(region_role_may_write(false));

  // Fresh board, role chosen before any region: it must come back Unset, never EU.
  FakeStore a;
  a.load();
  TEST_ASSERT_EQUAL(Region::Unset, a.region);
  TEST_ASSERT_TRUE(a.save(1, Region::Unset));
  TEST_ASSERT_TRUE(a.nvs.has_region);
  a.load();
  TEST_ASSERT_EQUAL(1, a.role);
  TEST_ASSERT_EQUAL(Region::Unset, a.region);

  // Fresh board, role and US saved together, reset after the first write: no role reaches
  // flash without its region, so the reload is US with no role, not EU.
  FakeStore b;
  b.load();
  TEST_ASSERT_FALSE(b.save(1, Region::US915, false, true));
  TEST_ASSERT_FALSE(b.nvs.has_role);
  b.load();
  TEST_ASSERT_EQUAL(0, b.role);
  TEST_ASSERT_EQUAL(Region::US915, b.region);

  // Fresh board, the region write fails: the role is held back and the save reports failure.
  FakeStore c;
  c.load();
  TEST_ASSERT_FALSE(c.save(2, Region::Unset, true));
  TEST_ASSERT_FALSE(c.nvs.has_role);
  c.load();
  TEST_ASSERT_EQUAL(Region::Unset, c.region);
  // The next save tries the region key again and then lets the role through.
  TEST_ASSERT_TRUE(c.save(2, Region::Unset));
  c.load();
  TEST_ASSERT_EQUAL(2, c.role);
  TEST_ASSERT_EQUAL(Region::Unset, c.region);

  // A board from before regions (role, no key) loads EU, and its first save pins EU even
  // though the region did not change, so a later role change cannot move it.
  FakeStore d;
  d.nvs.has_role = true;
  d.nvs.role = 1;
  d.load();
  TEST_ASSERT_EQUAL(Region::EU868, d.region);
  TEST_ASSERT_TRUE(d.save(2, Region::EU868));
  TEST_ASSERT_TRUE(d.nvs.has_region);
  d.load();
  TEST_ASSERT_EQUAL(2, d.role);
  TEST_ASSERT_EQUAL(Region::EU868, d.region);
  // Once pinned, an unchanged region is not written again.
  TEST_ASSERT_FALSE(region_must_write(false, d.region_stored));
}

static void test_region_about_fits_the_screen() {
  // About line: "Heltec V4  LoRa " (16) plus the channel, in a 6 px font on a 128 px screen.
  const Region all[] = {Region::Unset, Region::EU868, Region::US915};
  for (Region r : all) TEST_ASSERT_TRUE(16 + strlen(region_cfg(r).about) <= 21);
}

static int8_t legal(Region r, int8_t gain) { return region_legal_sx_dbm(r, gain, 11, 14, -9); }

static void test_region_legal_power_eu() {
  // 27 dBm ERP: 27 + 2.15 - gain - 11, floored, never above the hardware's 14.
  TEST_ASSERT_EQUAL(14, legal(Region::EU868, 0));
  TEST_ASSERT_EQUAL(14, legal(Region::EU868, 2));
  TEST_ASSERT_EQUAL(14, legal(Region::EU868, 3));
  TEST_ASSERT_EQUAL(13, legal(Region::EU868, 5));
  TEST_ASSERT_EQUAL(10, legal(Region::EU868, 8));
  TEST_ASSERT_EQUAL(3, legal(Region::EU868, 15));
  TEST_ASSERT_EQUAL(-9, legal(Region::EU868, 40));
  // ERP never passes 27 dBm at any whole gain the console accepts.
  for (int g = 0; g <= 15; g++)
    TEST_ASSERT_TRUE(legal(Region::EU868, (int8_t)g) + 11 + g - 2.15f <= 27.0f);
}

static void test_region_legal_power_us() {
  // 25 dBm conducted with antennas up to 6 dBi: the hardware ceiling (14 + 11) is the cap.
  TEST_ASSERT_EQUAL(14, legal(Region::US915, 0));
  TEST_ASSERT_EQUAL(14, legal(Region::US915, 2));
  TEST_ASSERT_EQUAL(14, legal(Region::US915, 3));
  TEST_ASSERT_EQUAL(14, legal(Region::US915, 5));
  TEST_ASSERT_EQUAL(14, legal(Region::US915, 6));
  // Above 6 dBi it comes down dB for dB.
  TEST_ASSERT_EQUAL(13, legal(Region::US915, 7));
  TEST_ASSERT_EQUAL(11, legal(Region::US915, 9));
  TEST_ASSERT_EQUAL(5, legal(Region::US915, 15));
  TEST_ASSERT_EQUAL(-9, legal(Region::US915, 40));
  // Conducted power never passes 25 dBm, and radiated power (EIRP) never passes 31 dBm,
  // well inside the rule's 30 dBm conducted and 36 dBm EIRP.
  for (int g = 0; g <= 15; g++) {
    const int cond = legal(Region::US915, (int8_t)g) + 11;
    TEST_ASSERT_TRUE(cond <= 25);
    TEST_ASSERT_TRUE(cond + g <= 31);
  }
  // A stronger front end or chip setting would still be held to the region's 25 dBm.
  TEST_ASSERT_EQUAL(14, region_legal_sx_dbm(Region::US915, 3, 11, 22, -9));
  TEST_ASSERT_EQUAL(10, region_legal_sx_dbm(Region::US915, 3, 15, 22, -9));
  // Not chosen: the EU cap, the lower of the two (and nothing is sent anyway).
  TEST_ASSERT_EQUAL(legal(Region::EU868, 5), legal(Region::Unset, 5));
}

static void test_region_duty_limit() {
  TEST_ASSERT_EQUAL_UINT32(360000, region_budget_ms(Region::EU868));
  TEST_ASSERT_EQUAL_UINT32(3600000, region_budget_ms(Region::US915));
  TEST_ASSERT_EQUAL_UINT32(360000, region_budget_ms(Region::Unset));
  DutyCycle eu(region_cfg(Region::EU868).duty_limit), us(region_cfg(Region::US915).duty_limit);
  TEST_ASSERT_EQUAL_UINT32(region_budget_ms(Region::EU868), eu.budgetMs());
  TEST_ASSERT_EQUAL_UINT32(region_budget_ms(Region::US915), us.budgetMs());
  // 400 s on air in one hour: far past the EU's 360 s, about a ninth of the US hour.
  const uint32_t t = 1000;
  us.record(400000, t);
  TEST_ASSERT_TRUE(us.canSend(2000, t, false));
  TEST_ASSERT_TRUE(us.canSend(2000, t, true));
  TEST_ASSERT_EQUAL_UINT16(111, us.usedPermille(t));
  // A whole hour of back to back SF12 pings (worst case) still leaves room.
  us.record(2400000, t);
  TEST_ASSERT_TRUE(us.canSend(2000, t, false));
}

static void test_region_switch_keeps_airtime() {
  // US airtime is recorded in full, so a board switched back to EU starts out knowing it.
  DutyCycle us(region_cfg(Region::US915).duty_limit);
  const uint32_t t = 5000;
  us.record(400000, t);
  DutySnapshot snap;
  us.save(snap);
  DutyCycle eu(region_cfg(Region::EU868).duty_limit);
  eu.restore(snap);
  TEST_ASSERT_EQUAL_UINT32(400000, eu.usedMs(t));
  TEST_ASSERT_EQUAL_UINT16(1000, eu.usedPermille(t));
  TEST_ASSERT_FALSE(eu.canSend(62, t, true));
  // It rolls off like any other airtime: an hour and a bit later the EU board may send.
  TEST_ASSERT_TRUE(eu.canSend(62, t + 62 * 60000UL, true));
}

static void test_link_timing_at_500khz() {
  // Fast SFs: the round trip is so short that the fixed 2 s floor sets the ping pace.
  uint32_t ping = lora_airtime_ms(7, 500, 5, 8, 23), ack = lora_airtime_ms(7, 500, 5, 8, 47);
  TEST_ASSERT_EQUAL_UINT32(2000, ping_interval_ms(ping, ack, 2000));
  TEST_ASSERT_EQUAL_UINT32(8000, rescue_silence_ms(2000, ping, 8000));
  ping = lora_airtime_ms(8, 500, 5, 8, 23), ack = lora_airtime_ms(8, 500, 5, 8, 47);
  TEST_ASSERT_EQUAL_UINT32(2000, ping_interval_ms(ping, ack, 2000));
  // SF12: 20 round trips, about a quarter of the 125 kHz pace.
  ping = lora_airtime_ms(12, 500, 5, 8, 23), ack = lora_airtime_ms(12, 500, 5, 8, 47);
  TEST_ASSERT_EQUAL_UINT32(330, ping);
  TEST_ASSERT_EQUAL_UINT32(494, ack);
  const uint32_t iv = ping_interval_ms(ping, ack, 2000);
  TEST_ASSERT_EQUAL_UINT32(16480, iv);
  const uint32_t iv125 = ping_interval_ms(lora_airtime_ms(12, 125, 5, 8, 23), lora_airtime_ms(12, 125, 5, 8, 47), 2000);
  TEST_ASSERT_TRUE(iv * 3 < iv125);
  TEST_ASSERT_EQUAL_UINT32(3 * iv + ping, rescue_silence_ms(iv, ping, 8000));
}

int main(int, char**) {
  for (int i = 0; i < 32; i++) {
    KEY[i] = (uint8_t)(i * 7 + 1);
    KEY2[i] = KEY[i];
  }
  KEY2[31] ^= 0x80;
  UNITY_BEGIN();
  RUN_TEST(test_sha256_abc);
  RUN_TEST(test_hmac_rfc4231_case2);
  RUN_TEST(test_ct_equal_and_random);
  RUN_TEST(test_frame_round_trip);
  RUN_TEST(test_frame_max_payload_round_trip);
  RUN_TEST(test_frame_tamper_any_byte);
  RUN_TEST(test_frame_wrong_key);
  RUN_TEST(test_frame_encode_limits);
  RUN_TEST(test_replay_guard);
  RUN_TEST(test_seq_dedupe);
  RUN_TEST(test_ack_round_trip);
  RUN_TEST(test_ping_round_trip);
  RUN_TEST(test_net_from_key);
  RUN_TEST(test_airtime);
  RUN_TEST(test_sf_floor_margin_bars);
  RUN_TEST(test_link_step_up);
  RUN_TEST(test_link_step_down_needs_five);
  RUN_TEST(test_link_strong_steps_down_fast);
  RUN_TEST(test_duty_reage_snapshot);
  RUN_TEST(test_link_rescue_and_bars);
  RUN_TEST(test_link_fixed_mode);
  RUN_TEST(test_link_multi_step_and_state);
  RUN_TEST(test_link_success_pct);
  RUN_TEST(test_silence_watch);
  RUN_TEST(test_duty_priority_vs_ping);
  RUN_TEST(test_duty_rolls_off);
  RUN_TEST(test_duty_wraparound);
  RUN_TEST(test_geo);
  RUN_TEST(test_click_bounce_ignored);
  RUN_TEST(test_click);
  RUN_TEST(test_long_and_verylong);
  RUN_TEST(test_long_only_release);
  RUN_TEST(test_twostage_sequence);
  RUN_TEST(test_twostage_full_without_half);
  RUN_TEST(test_twostage_bounce);
  RUN_TEST(test_battery);
  RUN_TEST(test_shot_one_press);
  RUN_TEST(test_shot_af_locked);
  RUN_TEST(test_shot_two_stage_passthrough);
  RUN_TEST(test_shot_two_stage_cancels_one_press);
  RUN_TEST(test_range_csv);
  RUN_TEST(test_duty_snapshot_restore);
  RUN_TEST(test_cmd_first_ctr_and_ids);
  RUN_TEST(test_link_timing);
  RUN_TEST(test_shot_button_state);
  RUN_TEST(test_ack_id_binding);
  RUN_TEST(test_rescue_probe_sf);
  RUN_TEST(test_tx_first_try_expiry);
  RUN_TEST(test_duty_cold_precharge);
  RUN_TEST(test_fuji_notify_evidence);
  RUN_TEST(test_duty_true_rolling_hour);
  RUN_TEST(test_duty_cold_start_no_ratchet);
  RUN_TEST(test_shot_step_done_rebases);
  RUN_TEST(test_fuji_notify_after_shutter);
  RUN_TEST(test_shot_full_timed_lets_go_by_itself);
  RUN_TEST(test_shoot_payload_hold);
  RUN_TEST(test_plan_frames_match_proto);
  RUN_TEST(test_interval_done_waits_for_sf12_outcome);
  RUN_TEST(test_interval_done_wait_is_bounded);
  RUN_TEST(test_aflock_timeout_under_a_press_does_not_relock);
  RUN_TEST(test_airtime_500khz);
  RUN_TEST(test_airtime_ldro_follows_symbol_time_not_sf);
  RUN_TEST(test_region_table);
  RUN_TEST(test_region_names_and_menu_step);
  RUN_TEST(test_region_on_load_migration);
  RUN_TEST(test_region_save_pins_key_before_role);
  RUN_TEST(test_region_about_fits_the_screen);
  RUN_TEST(test_region_legal_power_eu);
  RUN_TEST(test_region_legal_power_us);
  RUN_TEST(test_region_duty_limit);
  RUN_TEST(test_region_switch_keeps_airtime);
  RUN_TEST(test_link_timing_at_500khz);
  return UNITY_END();
}
