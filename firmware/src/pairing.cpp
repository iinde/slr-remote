// Board-to-board pairing over LoRa: committed X25519 exchange at minimum power, a 6-digit
// code confirmed by the user on both boards, then authenticated Ping/Ack round trips before
// anything is saved. See pairing.h for the wire format.
#include "pairing.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "crypto_port.h"
#include "duty.h"
#include "hw.h"
#include "proto.h"
#include "radio.h"

namespace {

constexpr uint8_t MAGIC0 = 'X', MAGIC1 = 'P';
constexpr uint8_t T_HELLO = 1, T_REPLY = 2, T_REVEAL = 3;
constexpr uint8_t NONCE_LEN = 16;
constexpr uint8_t HELLO_LEN = 3 + 32;               // commitment
constexpr uint8_t REPLY_LEN = 3 + 32 + NONCE_LEN;   // cam pub + cam nonce
constexpr uint8_t REVEAL_LEN = 3 + 32 + NONCE_LEN;  // hh pub + hh nonce
constexpr uint8_t PAIR_SF = 7;
constexpr uint32_t HELLO_EVERY_MS = 1200;
constexpr uint32_t REVEAL_EVERY_MS = 2400;
constexpr uint32_t PING_EVERY_MS = 1500;
constexpr uint32_t COMMIT_EVERY_MS = 1000;
constexpr uint32_t LINGER_MS = 8000;  // after the last frame heard: keep answering retries
// Handheld in Commit: keep sending the commit Ping this long after the camera was last heard,
// longer than the camera lingers, so a lost commit Ack is retried for the camera's whole window.
constexpr uint32_t COMMIT_GIVE_UP_MS = LINGER_MS + 4000;
constexpr uint32_t CODE_MOD = 1000000;
constexpr char LABEL[] = "xe5-remote-pair-v2";
constexpr char COMMIT_LABEL[] = "xe5-commit";

// Exchange: HELLO/REPLY/REVEAL. Confirm: code on screen. Verify: authenticated Ping/Ack.
// Commit: handheld proves it holds the Ack, camera saves. Linger: answer late retries.
enum class Phase : uint8_t { Exchange, Confirm, Verify, Commit, Linger };

struct Ctx {
  bool initiator = false;
  uint8_t priv[32], pub[32], nonce[NONCE_LEN];
  uint8_t peer[32], peer_nonce[NONCE_LEN];
  uint8_t commit[32];       // handheld: own commitment; camera: the one it locked onto
  bool have_commit = false;  // camera: a HELLO was answered
  bool have_peer = false;
  uint8_t key[32];
  uint16_t net = 0;
  uint32_t code = 0;
  Phase phase = Phase::Exchange;
  uint32_t last_tx = 0, last_reveal = 0, last_heard = 0;
  uint16_t ping_seq = 0;
  ReplayGuard guard;
  uint32_t peer_ctr = 0;
  PairRecord rec;
  char code_text[16];
};

void wipe(void* p, size_t n) {
  volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
  while (n--) *v++ = 0;
}

bool tx_allowed() { return millis() - hw_boot_ms() >= radiocfg::BOOT_TX_HOLDOFF_MS; }

bool g_duty_blocked = false;  // the governor refused a pairing frame (shown to the user)

// Checked BEFORE a counter is taken for an authenticated frame, so refusals burn nothing.
bool can_tx(uint8_t len) {
  if (!tx_allowed()) return false;
  if (!duty_can_send(radio_airtime_ms(len), false)) {
    g_duty_blocked = true;
    return false;
  }
  return true;
}

// Every pairing frame goes through the board's duty-cycle governor. A transmit that started
// is booked even if TX_DONE never came (it may have gone out).
bool send_raw(const uint8_t* buf, uint8_t len) {
  if (!can_tx(len)) return false;
  const uint32_t air = radio_airtime_ms(len);
  const RadioTx r = radio_send(buf, len);
  if (radio_tx_started(r)) duty_record(air);
  if (r == RadioTx::Done) g_duty_blocked = false;
  return r == RadioTx::Done;
}

bool send_pair_frame(uint8_t type, const uint8_t* a, size_t alen, const uint8_t* b, size_t blen) {
  uint8_t buf[REPLY_LEN];
  buf[0] = MAGIC0;
  buf[1] = MAGIC1;
  buf[2] = type;
  memcpy(buf + 3, a, alen);
  if (blen) memcpy(buf + 3 + alen, b, blen);
  return send_raw(buf, (uint8_t)(3 + alen + blen));
}

bool send_hello(Ctx& c) { return send_pair_frame(T_HELLO, c.commit, 32, nullptr, 0); }
bool send_reply(Ctx& c) { return send_pair_frame(T_REPLY, c.pub, 32, c.nonce, NONCE_LEN); }
bool send_reveal(Ctx& c) { return send_pair_frame(T_REVEAL, c.pub, 32, c.nonce, NONCE_LEN); }

// Pairing frame type, or 0 if p is not one (then it may be an authenticated frame).
uint8_t pair_type(const RxPacket& p) {
  if (p.len < 3 || p.data[0] != MAGIC0 || p.data[1] != MAGIC1) return 0;
  const uint8_t t = p.data[2];
  if (t == T_HELLO && p.len == HELLO_LEN) return t;
  if ((t == T_REPLY && p.len == REPLY_LEN) || (t == T_REVEAL && p.len == REVEAL_LEN)) return t;
  return 0;
}

bool all_zero(const uint8_t* b, size_t n) {
  uint8_t acc = 0;
  for (size_t i = 0; i < n; i++) acc |= b[i];
  return acc == 0;
}

// C = SHA-256("xe5-commit" || pub || nonce)
void make_commit(const uint8_t pub[32], const uint8_t nonce[NONCE_LEN], uint8_t out[32]) {
  constexpr size_t L = sizeof(COMMIT_LABEL) - 1;
  uint8_t buf[L + 32 + NONCE_LEN];
  memcpy(buf, COMMIT_LABEL, L);
  memcpy(buf + L, pub, 32);
  memcpy(buf + L + 32, nonce, NONCE_LEN);
  xc_sha256(buf, sizeof buf, out);
}

// key  = SHA-256(label || shared || hh_pub || cam_pub || hh_nonce || cam_nonce)
// code = u32 LE of SHA-256(key)[0..3] mod 10^6. The handheld's key and nonce are fixed by
// its commitment before it sees the camera's, and the camera's are fresh per session, so a
// man in the middle matches both screens with probability 1e-6 per attempt.
bool derive(Ctx& c) {
  uint8_t shared[32];
  if (!xc_x25519_shared(c.priv, c.peer, shared)) return false;
  const uint8_t* hh = c.initiator ? c.pub : c.peer;
  const uint8_t* cam = c.initiator ? c.peer : c.pub;
  const uint8_t* hn = c.initiator ? c.nonce : c.peer_nonce;
  const uint8_t* cn = c.initiator ? c.peer_nonce : c.nonce;
  constexpr size_t L = sizeof(LABEL) - 1;
  uint8_t buf[L + 96 + 2 * NONCE_LEN];
  memcpy(buf, LABEL, L);
  memcpy(buf + L, shared, 32);
  memcpy(buf + L + 32, hh, 32);
  memcpy(buf + L + 64, cam, 32);
  memcpy(buf + L + 96, hn, NONCE_LEN);
  memcpy(buf + L + 96 + NONCE_LEN, cn, NONCE_LEN);
  xc_sha256(buf, sizeof buf, c.key);
  uint8_t h[32];
  xc_sha256(c.key, 32, h);
  const uint32_t v = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 |
                     (uint32_t)h[3] << 24;
  c.code = v % CODE_MOD;
  c.net = net_from_key(c.key);
  wipe(shared, sizeof shared);
  wipe(buf, sizeof buf);
  wipe(h, sizeof h);
  return !all_zero(c.key, 32);
}

bool send_auth(Ctx& c, MsgType type, uint16_t seq, const uint8_t* pl, uint8_t len) {
  if (!can_tx((uint8_t)(PROTO_HDR + len + PROTO_TAG))) return false;  // no counter burnt
  Frame f{};
  f.type = type;
  f.net = c.net;
  f.ctr = store_next_ctr();
  if (f.ctr == 0) return false;  // counter block not persisted: never reuse a counter
  f.seq = seq;
  f.len = len;
  memcpy(f.payload, pl, len);
  uint8_t buf[PROTO_MAX_FRAME];
  const size_t n = frame_encode(f, c.key, buf, sizeof buf);
  return n && send_raw(buf, (uint8_t)n);
}

bool send_ping(Ctx& c, uint16_t seq) {
  PingPayload pp{};
  pp.hh_batt = hw_batt_pct();
  uint8_t pl[PING_PAYLOAD_LEN];
  ping_pack(pp, pl);
  return send_auth(c, MsgType::Ping, seq, pl, sizeof pl);
}

bool send_ack(Ctx& c, const Frame& in, const RxPacket& p) {
  AckPayload a{};
  a.ack_seq = in.seq;
  a.ack_type = in.type;
  a.stage = ShotStage::Delivered;
  a.rx_rssi = (int8_t)constrain((int)p.rssi, -128, 127);
  a.rx_snr_q4 = (int8_t)constrain((int)(p.snr * 4.f), -128, 127);
  a.cam_batt = hw_batt_pct();
  a.cam_link = CamLink::None;
  a.sf = PAIR_SF;
  uint8_t pl[ACK_PAYLOAD_LEN];
  ack_pack(a, pl);
  return send_auth(c, MsgType::Ack, in.seq, pl, sizeof pl);
}

bool decode_auth(Ctx& c, const RxPacket& p, MsgType want, Frame& f) {
  if (!frame_decode(p.data, p.len, c.key, f)) return false;
  return f.net == c.net && f.type == want;
}

void enter_confirm(Ctx& c, void (*show)(const char*, const char*)) {
  c.phase = Phase::Confirm;
  snprintf(c.code_text, sizeof c.code_text, "CODE %06lu", (unsigned long)c.code);
  show(c.code_text, "Same? hold PRG");
}

enum class Step : uint8_t { Ok, Abort };

// Handheld side of the exchange. Any pairing frame that doesn't fit this session aborts it.
Step hh_pairing_frame(Ctx& c, const RxPacket& p, uint8_t type, void (*show)(const char*, const char*)) {
  if (type != T_REPLY) return Step::Abort;  // another handheld, or a stray REVEAL
  const uint8_t* pub = p.data + 3;
  const uint8_t* nonce = p.data + 3 + 32;
  if (c.have_peer) {
    if (memcmp(pub, c.peer, 32) != 0 || memcmp(nonce, c.peer_nonce, NONCE_LEN) != 0)
      return Step::Abort;  // a second, different camera answered
    send_reveal(c);       // our REVEAL was probably lost
    return Step::Ok;
  }
  if (all_zero(pub, 32) || memcmp(pub, c.pub, 32) == 0) return Step::Abort;
  memcpy(c.peer, pub, 32);
  memcpy(c.peer_nonce, nonce, NONCE_LEN);
  if (!derive(c)) return Step::Abort;
  c.have_peer = true;
  send_reveal(c);
  c.last_reveal = millis();
  enter_confirm(c, show);
  return Step::Ok;
}

// Camera side: lock onto the first commitment, answer only it, check the reveal against it.
Step cam_pairing_frame(Ctx& c, const RxPacket& p, uint8_t type, void (*show)(const char*, const char*)) {
  if (type == T_HELLO) {
    if (!c.have_commit) {
      memcpy(c.commit, p.data + 3, 32);
      c.have_commit = true;
    } else if (memcmp(c.commit, p.data + 3, 32) != 0) {
      return Step::Abort;  // a second handheld
    }
    send_reply(c);
    return Step::Ok;
  }
  if (type != T_REVEAL || !c.have_commit) return Step::Abort;
  const uint8_t* pub = p.data + 3;
  const uint8_t* nonce = p.data + 3 + 32;
  if (c.have_peer) {
    return memcmp(pub, c.peer, 32) == 0 && memcmp(nonce, c.peer_nonce, NONCE_LEN) == 0
               ? Step::Ok
               : Step::Abort;
  }
  uint8_t check[32];
  make_commit(pub, nonce, check);
  if (!xc_ct_equal(check, c.commit, 32)) return Step::Abort;
  if (all_zero(pub, 32) || memcmp(pub, c.pub, 32) == 0) return Step::Abort;
  memcpy(c.peer, pub, 32);
  memcpy(c.peer_nonce, nonce, NONCE_LEN);
  if (!derive(c)) return Step::Abort;
  c.have_peer = true;
  enter_confirm(c, show);
  return Step::Ok;
}

void make_record(Ctx& c, uint32_t peer_ctr) {
  c.rec.valid = true;
  memcpy(c.rec.key, c.key, 32);
  c.rec.net = c.net;
  c.rec.peer_ctr = peer_ctr;
}

void gather_entropy() {
  uint8_t rb[16];
  if (radio_random(rb, sizeof rb)) xc_add_entropy(rb, sizeof rb);
  const uint32_t t = micros();
  xc_add_entropy(reinterpret_cast<const uint8_t*>(&t), sizeof t);
  wipe(rb, sizeof rb);
}

bool keygen(Ctx& c) {
  gather_entropy();
  if (!xc_x25519_keypair(c.priv, c.pub)) return false;
  xc_random(c.nonce, NONCE_LEN);
  xc_random(reinterpret_cast<uint8_t*>(&c.ping_seq), sizeof c.ping_seq);
  if (c.initiator) make_commit(c.pub, c.nonce, c.commit);
  return true;
}

}  // namespace

PairResult pair_run(bool initiator, void (*show)(const char*, const char*), int (*input)(),
                    PairRecord& out, uint32_t timeout_ms) {
  const uint8_t prev_sf = radio_sf();
  const int8_t prev_dbm = radio_tx_dbm();
  radio_set_sf(PAIR_SF);
  radio_set_tx_dbm(radiocfg::PAIR_SX_DBM);

  Ctx c;
  c.initiator = initiator;
  g_duty_blocked = false;
  bool duty_shown = false;
  bool cam_saved = false;  // camera: the new pairing is in flash (any exit now reports Ok)
  PairResult res = PairResult::Failed;
  if (!keygen(c)) {
    show("PAIR FAILED", "no crypto");
  } else {
    show(initiator ? "PAIRING..." : "WAITING...", "Other board: Pair");
    const uint32_t start = millis();
    res = PairResult::Timeout;
    for (;;) {
      const uint32_t now = millis();
      // Commit/Linger are bounded by their own timers and must finish (the key may be saved).
      if (c.phase == Phase::Linger && now - c.last_heard >= LINGER_MS) {
        res = PairResult::Ok;
        break;
      }
      if (c.phase == Phase::Commit && !initiator && now - c.last_heard >= LINGER_MS) {
        res = PairResult::Failed;  // camera never got the commit Ping: nothing saved here
        break;
      }
      if (c.phase == Phase::Commit && initiator && now - c.last_heard >= COMMIT_GIVE_UP_MS) {
        // The camera may have saved (its commit Ack lost) while we did not: say so.
        show("PAIR UNSURE", "Pair again");
        res = PairResult::Unsure;
        break;
      }
      if (g_duty_blocked && !duty_shown) {
        duty_shown = true;
        show("DUTY LIMIT", "Pairing waits for airtime");
      }
      const bool closing = c.phase == Phase::Commit || c.phase == Phase::Linger;
      if (!closing && now - start >= timeout_ms) break;
      const int in = input();
      if (in < 0 && !closing) {
        res = PairResult::Rejected;
        break;
      }

      RxPacket pkt;
      const RxPacket* p = radio_poll(pkt) ? &pkt : nullptr;
      const uint8_t ptype = p ? pair_type(*p) : 0;
      if (ptype) {
        const Step s = initiator ? hh_pairing_frame(c, *p, ptype, show)
                                 : cam_pairing_frame(c, *p, ptype, show);
        // From Verify on, the keys are agreed and confirmed: a stray or forged pairing frame
        // (no key needed to send one) must not end a session that may be saving.
        if (s == Step::Abort && c.phase < Phase::Verify) {
          show("PAIR ABORTED", "Other pairing nearby?");
          res = PairResult::Failed;
          break;
        }
        p = nullptr;
      }

      if (c.phase == Phase::Exchange) {
        if (initiator && now - c.last_tx >= HELLO_EVERY_MS) {
          c.last_tx = now;
          send_hello(c);
        }
      } else if (c.phase == Phase::Confirm) {
        if (initiator && now - c.last_reveal >= REVEAL_EVERY_MS) {
          c.last_reveal = now;
          send_reveal(c);
        }
        if (in > 0) {
          c.phase = Phase::Verify;
          c.last_tx = 0;
          show("CHECKING...", initiator ? "" : "Waiting for remote");
        }
      } else if (initiator) {
        Frame f;
        if (c.phase == Phase::Verify) {  // Ping until an authenticated Ack
          if (p && decode_auth(c, *p, MsgType::Ack, f) && f.seq == c.ping_seq) {
            c.peer_ctr = f.ctr;
            c.phase = Phase::Commit;
            c.last_heard = now;
            c.last_tx = 0;
          } else if (c.last_tx == 0 || now - c.last_tx >= PING_EVERY_MS) {
            c.last_tx = now;  // refused or not, keep the cadence (no 5 ms retry spin)
            send_ping(c, c.ping_seq);
            if (now - c.last_reveal >= REVEAL_EVERY_MS) {
              c.last_reveal = now;
              send_reveal(c);
            }
          }
        } else {  // Commit: second Ping (seq+1) until the camera acks it, then save
          const uint16_t cseq = (uint16_t)(c.ping_seq + 1);
          if (p && decode_auth(c, *p, MsgType::Ack, f) && f.ctr > c.peer_ctr) {
            c.last_heard = now;  // any authenticated frame: the camera is still there
            c.peer_ctr = f.ctr;
            if (f.seq == cseq) {
              make_record(c, f.ctr);
              if (!store_save_pair(c.rec)) {
                show("PAIR FAILED", "storage");
                res = PairResult::Unsure;  // the camera has saved the new key
                break;
              }
              res = PairResult::Ok;
              break;
            }
          }
          if (c.last_tx == 0 || now - c.last_tx >= COMMIT_EVERY_MS) {
            c.last_tx = now;
            send_ping(c, cseq);
          }
        }
      } else {  // camera: Verify / Commit / Linger
        Frame f;
        if (p && decode_auth(c, *p, MsgType::Ping, f) && c.guard.accept(f.ctr)) {
          c.last_heard = now;
          if (c.phase == Phase::Verify) {
            c.ping_seq = f.seq;  // the handheld's commit Ping is this + 1
            c.phase = Phase::Commit;
            show("CHECKING...", "Almost there");
          } else if (c.phase == Phase::Commit && f.seq == (uint16_t)(c.ping_seq + 1)) {
            // The handheld holds our Ack: only now replace the stored pairing.
            make_record(c, f.ctr);
            if (!store_save_pair(c.rec)) {  // no commit Ack: the handheld must not save either
              show("PAIR FAILED", "storage");
              res = PairResult::StoreFailed;
              break;
            }
            cam_saved = true;
            c.phase = Phase::Linger;
            show("PAIRED", "");
          } else if (cam_saved) {
            // A commit retry heard after the save (our commit Ack was lost): its counter is
            // above the saved peer_ctr, so persist it, or a recording of it would pass our
            // replay guard after the reboot that follows.
            store_put_peer_ctr(f.ctr);
            c.rec.peer_ctr = f.ctr;
          }
          send_ack(c, f, *p);
        }
      }
      delay(5);
    }
  }

  if (cam_saved) res = PairResult::Ok;  // saved: never report this board as unpaired
  if (res == PairResult::Ok) out = c.rec;
  wipe(c.priv, sizeof c.priv);
  wipe(c.key, sizeof c.key);
  wipe(c.nonce, sizeof c.nonce);
  wipe(&c.rec, sizeof c.rec);
  radio_set_sf(prev_sf);
  radio_set_tx_dbm(prev_dbm);
  return res;
}
