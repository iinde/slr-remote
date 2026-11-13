#include "proto.h"

#include <string.h>

#include "crypto_port.h"

namespace {
void put16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
bool valid_type(uint8_t t) { return t >= (uint8_t)MsgType::Ping && t <= (uint8_t)MsgType::Sleep; }

void tag_of(const uint8_t key[32], const uint8_t* msg, size_t n, uint8_t tag[PROTO_TAG]) {
  uint8_t mac[32];
  xc_hmac_sha256(key, 32, msg, n, mac);
  memcpy(tag, mac, PROTO_TAG);
}
}  // namespace

size_t frame_encode(const Frame& f, const uint8_t key[32], uint8_t* out, size_t cap) {
  if (f.len > PROTO_MAX_PAYLOAD) return 0;
  const size_t body = PROTO_HDR + f.len, total = body + PROTO_TAG;
  if (out == nullptr || cap < total) return 0;
  out[0] = PROTO_VER;
  out[1] = (uint8_t)f.type;
  put16(out + 2, f.net);
  put32(out + 4, f.ctr);
  put16(out + 8, f.seq);
  out[10] = f.len;
  memcpy(out + PROTO_HDR, f.payload, f.len);
  tag_of(key, out, body, out + body);
  return total;
}

bool frame_decode(const uint8_t* in, size_t n, const uint8_t key[32], Frame& out) {
  if (in == nullptr || n < PROTO_HDR + PROTO_TAG || n > PROTO_MAX_FRAME) return false;
  if (in[0] != PROTO_VER) return false;
  const uint8_t len = in[10];
  if (len > PROTO_MAX_PAYLOAD || n != PROTO_HDR + len + PROTO_TAG) return false;
  uint8_t tag[PROTO_TAG];
  tag_of(key, in, PROTO_HDR + len, tag);
  if (!xc_ct_equal(tag, in + PROTO_HDR + len, PROTO_TAG)) return false;
  if (!valid_type(in[1])) return false;
  out.type = (MsgType)in[1];
  out.net = get16(in + 2);
  out.ctr = get32(in + 4);
  out.seq = get16(in + 8);
  out.len = len;
  memcpy(out.payload, in + PROTO_HDR, len);
  return true;
}

bool ReplayGuard::accept(uint32_t ctr) {
  if (ctr <= last_) return false;
  last_ = ctr;
  return true;
}

bool SeqDedupe::firstTimeId(uint32_t id) {
  for (uint8_t i = 0; i < n_; i++)
    if (ring_[i] == id) return false;
  ring_[head_] = id;
  head_ = (uint8_t)((head_ + 1) % 16);
  if (n_ < 16) n_++;
  return true;
}

// Ack layout: seq(2) type stage err rssi snr batt link ble sf gps lat(4) lon(4) duty(2)
// id_hi(2) = 24 bytes, then 2 reserved zero bytes to the fixed 26.
size_t ack_pack(const AckPayload& a, uint8_t* out) {
  memset(out, 0, ACK_PAYLOAD_LEN);
  put16(out, a.ack_seq);
  out[2] = (uint8_t)a.ack_type;
  out[3] = (uint8_t)a.stage;
  out[4] = a.err;
  out[5] = (uint8_t)a.rx_rssi;
  out[6] = (uint8_t)a.rx_snr_q4;
  out[7] = a.cam_batt;
  out[8] = (uint8_t)a.cam_link;
  out[9] = (uint8_t)a.ble_rssi;
  out[10] = a.sf;
  out[11] = a.gps_flags;
  put32(out + 12, (uint32_t)a.lat_e7);
  put32(out + 16, (uint32_t)a.lon_e7);
  put16(out + 20, a.duty_permille);
  put16(out + 22, a.ack_id_hi);
  put32(out + 24, a.utc);
  return ACK_PAYLOAD_LEN;
}

bool ack_unpack(const uint8_t* in, size_t n, AckPayload& a) {
  if (in == nullptr || n < ACK_PAYLOAD_LEN_V1) return false;
  if (in[3] > (uint8_t)ShotStage::Failed || in[8] > (uint8_t)CamLink::Ready) return false;
  a.ack_seq = get16(in);
  a.ack_type = (MsgType)in[2];
  a.stage = (ShotStage)in[3];
  a.err = in[4];
  a.rx_rssi = (int8_t)in[5];
  a.rx_snr_q4 = (int8_t)in[6];
  a.cam_batt = in[7];
  a.cam_link = (CamLink)in[8];
  a.ble_rssi = (int8_t)in[9];
  a.sf = in[10];
  a.gps_flags = in[11];
  a.lat_e7 = (int32_t)get32(in + 12);
  a.lon_e7 = (int32_t)get32(in + 16);
  a.duty_permille = get16(in + 20);
  a.ack_id_hi = get16(in + 22);
  a.utc = n >= ACK_PAYLOAD_LEN ? get32(in + 24) : 0;
  return true;
}

size_t ping_pack(const PingPayload& p, uint8_t* out) {
  out[0] = p.hh_batt;
  out[1] = (uint8_t)p.last_rssi;
  out[2] = (uint8_t)p.last_snr_q4;
  out[3] = p.flags;
  return PING_PAYLOAD_LEN;
}

bool ping_unpack(const uint8_t* in, size_t n, PingPayload& p) {
  if (in == nullptr || n < PING_PAYLOAD_LEN) return false;
  p.hh_batt = in[0];
  p.last_rssi = (int8_t)in[1];
  p.last_snr_q4 = (int8_t)in[2];
  p.flags = in[3];
  return true;
}

uint16_t net_from_key(const uint8_t key[32]) {
  uint8_t d[32];
  xc_sha256(key, 32, d);
  return get16(d);
}
