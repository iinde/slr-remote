// Link quality maths and the adaptive spreading-factor ladder (handheld side),
// plus the camera board's silence watchdog.
#pragma once
#include <stdint.h>

// Demodulation floor (dB SNR) per SF, the same at any bandwidth: 7:-7.5 8:-10 9:-12.5 10:-15 11:-17.5 12:-20
float sf_floor_db(uint8_t sf);
// margin = snr - floor(sf)
float margin_db(float snr, uint8_t sf);
// 0..4 bars: m<0 ->0, m<3 ->1, m<6 ->2, m<10 ->3, else 4
uint8_t bars_from_margin(float m);

struct LinkSample {
  bool acked;
  float snr_up;   // what the camera board measured (from Ack)
  float snr_dn;   // what the handheld measured on the Ack
  uint32_t rtt_ms;
};

class LinkManager {
 public:
  enum class Action : uint8_t { None, RequestSf, Rescue };
  explicit LinkManager(uint8_t sf_start = 7);
  // Feed one ping outcome. Rules (adaptive mode):
  //  - acked & worst-direction margin < 5 dB & sf < 12 -> RequestSf(sf+1)
  //  - 5 consecutive acked pings with worst margin > 12 dB & sf > 7 -> RequestSf(sf - k),
  //    k = max(1, floor((m - 8) / 2.5)) from the weakest margin m of the streak, clamped to
  //    SF7 (each SF step costs ~2.5 dB, so at least 8 dB stays: above the 5 dB step-up line)
  //  - fast path: 2 consecutive acked pings with worst margin > 20 dB, or 1 with > 25 dB -> the same step down
  //  - 3 consecutive misses -> Rescue (caller switches both ends to SF12)
  // Fixed mode never requests changes but still reports Rescue.
  Action onPing(const LinkSample& s);
  uint8_t sf() const { return sf_; }
  uint8_t requestedSf() const { return req_; }
  void confirmSf(uint8_t sf);       // camera acked SetSf, or rescue done: adopt sf, reset streaks
  void setAdaptive(bool on) { adaptive_ = on; }
  bool adaptive() const { return adaptive_; }
  float successPct() const;         // over last 20 pings, 0..100; NAN if none yet
  float lastMarginDb() const { return last_margin_; }  // worst direction, last acked ping
  uint8_t bars() const;             // bars_from_margin(lastMarginDb()), 0 if last ping missed
  uint8_t consecutiveMisses() const { return misses_; }
  // Step-down progress, kept across deep sleep (a wake builds a fresh LinkManager and would
  // otherwise never collect 5 good samples at SF12, where pings are ~72 s apart).
  struct State {
    uint8_t sf;
    uint8_t good;
    float good_min;
    float last_margin;
  };
  State state() const { return State{sf_, good_, good_min_, last_margin_}; }
  // Restores the streak only if it was collected on the SF this manager is on.
  void restore(const State& st);
 private:
  uint8_t sf_, req_;
  bool adaptive_ = true;
  uint8_t misses_ = 0, good_ = 0;
  float last_margin_ = -99.f;
  float good_min_ = 99.f;  // weakest margin in the current step-down streak
  bool last_acked_ = false;
  uint32_t hist_ = 0;   // bit i = ping i acked (newest bit0)
  uint8_t hist_n_ = 0;  // up to 20
};

// Ping cadence, shared by both ends so the camera's silence rule matches what the handheld
// actually sends. Every ping costs a ping and an ack on air, so size from the round trip:
//   normal: max(min_ms, 20 * (ping_air + ack_air))   (~5 % of the hour per board)
//   hunt:   max(hunt_ms, 4 * (ping_air + ack_air))
//   camera silence before rescue: max(floor_ms, 3 * normal interval + ping_air), matching
//   the handheld's own rule (3 missed pings -> rescue)
uint32_t ping_interval_ms(uint32_t ping_air_ms, uint32_t ack_air_ms, uint32_t min_ms);
uint32_t hunt_interval_ms(uint32_t ping_air_ms, uint32_t ack_air_ms, uint32_t hunt_ms);
uint32_t rescue_silence_ms(uint32_t interval_ms, uint32_t ping_air_ms, uint32_t floor_ms);

// Handheld rescue search, probe i (0, 1, 2, ...): even probes are SF12, odd probes sweep
// SF7..11 starting from link_sf (SF7 if link_sf is 12). pending_sf (7..11, 0 = none): an SF
// the camera board may have moved to unseen (a SetSf whose Ack we never heard) is the first
// odd probe, before the sweep.
uint8_t rescue_probe_sf(uint8_t i, uint8_t link_sf, uint8_t pending_sf = 0);

// First-try expiry of a queued command (stop-and-wait queue). base = max(queued_at, end of
// the boot TX hold-off). It must be on air within deadline_ms of reaching the head of the
// queue (head_since, also counted from base), and within max_queue_ms of base whatever is
// ahead of it, so it never fires late.
enum class TxExpiry : uint8_t { None, AtHead, Queued };
TxExpiry tx_first_try_expiry(uint32_t now, uint32_t queued_at, uint32_t head_since,
                             uint32_t holdoff_end, uint32_t deadline_ms, uint32_t max_queue_ms);

// Camera board: decides when unexpected silence should drop it to SF12.
class SilenceWatch {
 public:
  void onValidRx(uint32_t now) { last_ = now; asleep_ = false; }
  void onPeerSleep(uint32_t now) { last_ = now; asleep_ = true; }
  // true if not asleep, current sf != 12, and now - last >= silence_ms
  bool shouldRescue(uint32_t now, uint8_t current_sf, uint32_t silence_ms) const;
 private:
  uint32_t last_ = 0;
  bool asleep_ = false;
};
