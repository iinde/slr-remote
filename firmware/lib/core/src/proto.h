// LoRa frame format between handheld and camera board.
// Wire: [ver u8][type u8][net u16][ctr u32][seq u16][len u8][payload len][tag 8]
// All multi-byte fields little-endian. tag = HMAC-SHA256(key, everything before tag)[0..7].
#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr uint8_t PROTO_VER = 1;
constexpr size_t PROTO_HDR = 11;
constexpr size_t PROTO_TAG = 8;
constexpr size_t PROTO_MAX_PAYLOAD = 40;
constexpr size_t PROTO_MAX_FRAME = PROTO_HDR + PROTO_MAX_PAYLOAD + PROTO_TAG;  // 59

enum class MsgType : uint8_t {
  Ping = 1,     // handheld -> cam: keepalive / link probe. payload: PingPayload
  Ack = 2,      // cam -> hh: immediate reply to any frame. payload: AckPayload
  Focus = 3,    // hh -> cam: half-press down (hold focus)
  Shoot = 4,    // hh -> cam: full-press down. payload: echo u32 [+ u16 max hold ms, see below]
  Release = 5,  // hh -> cam: button fully released
  Shot = 6,     // hh -> cam: one-press focus->wait->shoot->release. payload: u16 focus_ms
  SetSf = 7,    // hh -> cam: switch SF after ACK. payload: u8 sf
  Result = 8,   // cam -> hh: outcome of a command. payload: AckPayload (stage filled)
  Sleep = 9,    // hh -> cam: handheld entering deep sleep; don't rescue on silence
};

struct Frame {
  MsgType type;
  uint16_t net;
  uint32_t ctr;  // sender's monotonic counter (replay protection)
  uint16_t seq;  // command id; Ack/Result echo the seq they answer
  uint8_t len;
  uint8_t payload[PROTO_MAX_PAYLOAD];
};

// Returns frame length written to out, or 0 on error (len > max, cap too small).
size_t frame_encode(const Frame& f, const uint8_t key[32], uint8_t* out, size_t cap);
// Returns true only if well-formed, version matches, and tag verifies.
bool frame_decode(const uint8_t* in, size_t n, const uint8_t key[32], Frame& out);

// Accepts a counter only if strictly greater than every counter accepted before.
class ReplayGuard {
 public:
  explicit ReplayGuard(uint32_t last = 0) : last_(last) {}
  bool accept(uint32_t ctr);
  uint32_t last() const { return last_; }
 private:
  uint32_t last_;
};

// Executes each command at most once (handheld retries reuse seq). Remembers last 16.
// Keys on the full 32-bit command id (cmd_first_ctr), which never repeats across handheld
// wakes; there is deliberately no 16-bit seq variant (it would collide with low ids).
class SeqDedupe {
 public:
  bool firstTimeId(uint32_t id);  // true the first time an id is seen
 private:
  uint32_t ring_[16] = {0};
  uint8_t n_ = 0, head_ = 0;
};

// The handheld sets seq = low 16 bits of the counter of a command's first send, and every
// retry is a later counter. Rebuilds that first counter (the command's identity).
inline uint32_t cmd_first_ctr(uint32_t ctr, uint16_t seq) {
  return ctr - (uint16_t)((uint16_t)ctr - seq);
}
// High half of a command id, carried in AckPayload.ack_id_hi.
inline uint16_t cmd_id_hi(uint32_t id) { return (uint16_t)(id >> 16); }
// True if an Ack/Result (seq + id_hi) answers the command whose full id is id.
inline bool ack_matches_id(uint16_t ack_seq, uint16_t ack_id_hi, uint32_t id) {
  return ack_seq == (uint16_t)id && ack_id_hi == cmd_id_hi(id);
}

enum class ShotStage : uint8_t {
  Idle = 0,
  Sent = 1,       // handheld transmitted (local only, never on the wire)
  Delivered = 2,  // camera board received it (Ack)
  Accepted = 3,   // camera accepted the BLE write(s) with response
  CamEvent = 4,   // camera emitted a notification within CAM_EVENT_WINDOW_MS
  Failed = 5,
};

enum class CamLink : uint8_t { None = 0, NotPaired = 1, Scanning = 2, Connecting = 3, Ready = 4 };

struct AckPayload {          // 26 bytes on the wire
  uint16_t ack_seq;          // seq being answered
  MsgType ack_type;          // type being answered
  ShotStage stage;           // Delivered for Ack; Accepted/CamEvent/Failed for Result
  uint8_t err;               // 0 ok; nonzero = FujiErr code
  int8_t rx_rssi;            // dBm the camera board measured on the frame it answers
  int8_t rx_snr_q4;          // SNR * 4 (dB), clamped to int8
  uint8_t cam_batt;          // camera-board battery %, 255 = unknown
  CamLink cam_link;
  int8_t ble_rssi;           // camera-board -> camera BLE RSSI, 0 = unknown
  uint8_t sf;                // SF the camera board is listening on now
  uint8_t gps_flags;         // bit0 fix valid, bit1 geotag enabled
  int32_t lat_e7, lon_e7;    // camera-board position (valid if bit0)
  uint16_t duty_permille;    // camera-board TX duty used, per mille of the hour
  uint32_t utc = 0;          // camera-board UTC seconds from GPS, 0 = unknown (bytes 24..27)
  uint16_t ack_id_hi = 0;    // high 16 bits of the answered command's id (cmd_first_ctr):
                             // with ack_seq this binds an Ack/Result to one exact command
};
constexpr size_t ACK_PAYLOAD_LEN = 28;
constexpr size_t ACK_PAYLOAD_LEN_V1 = 26;   // before utc: still accepted
size_t ack_pack(const AckPayload& a, uint8_t* out);           // returns ACK_PAYLOAD_LEN
bool ack_unpack(const uint8_t* in, size_t n, AckPayload& a);  // false if n < ACK_PAYLOAD_LEN

struct PingPayload {         // 4 bytes
  uint8_t hh_batt;           // handheld battery %
  int8_t last_rssi;          // what the handheld heard on the last Ack (dBm), 0 = none
  int8_t last_snr_q4;
  uint8_t flags;             // bit0 = want camera GPS in Ack
};
constexpr size_t PING_PAYLOAD_LEN = 4;
size_t ping_pack(const PingPayload& p, uint8_t* out);
bool ping_unpack(const uint8_t* in, size_t n, PingPayload& p);

// Shoot payload: u32 freshness echo, then optionally u16 max_hold_ms (len 6). A non-zero
// max hold makes the camera board let go by itself that long after the shutter write, so the
// press never lasts a whole Release airtime (about 1.2 s at SF12, a burst in CH). The
// handheld still sends its Release as a backup. 0 or len 4 = held until Release.
constexpr uint8_t SHOOT_PAYLOAD_LEN_TIMED = 6;
constexpr uint16_t SHOOT_MAX_HOLD_MS = 2000;  // the camera board clamps a timed hold to this
inline uint16_t shoot_hold_ms(const uint8_t* pl, uint8_t len) {
  if (len < SHOOT_PAYLOAD_LEN_TIMED) return 0;
  const uint16_t h = (uint16_t)(pl[4] | pl[5] << 8);
  return h > SHOOT_MAX_HOLD_MS ? SHOOT_MAX_HOLD_MS : h;
}
inline void shoot_put_hold(uint8_t* pl, uint16_t hold_ms) {
  pl[4] = (uint8_t)(hold_ms & 0xFF);
  pl[5] = (uint8_t)(hold_ms >> 8);
}

// Derive the 16-bit network id from a 32-byte pairing key (first two bytes of SHA-256(key)).
uint16_t net_from_key(const uint8_t key[32]);
