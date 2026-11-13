#include "linkmgr.h"

#include <math.h>

namespace {
constexpr float STEP_UP_BELOW_DB = 5.f;
constexpr float STEP_DOWN_ABOVE_DB = 12.f;
constexpr uint8_t STEP_DOWN_STREAK = 5;
constexpr float STEP_DOWN_KEEP_DB = 8.f;  // margin left after a (multi-step) descent
constexpr float DB_PER_SF = 2.5f;
constexpr uint8_t RESCUE_MISSES = 3;
constexpr uint8_t HIST_LEN = 20;
constexpr uint8_t SF_LO = 7, SF_HI = 12;
}  // namespace

float sf_floor_db(uint8_t sf) {
  if (sf < SF_LO) sf = SF_LO;
  if (sf > SF_HI) sf = SF_HI;
  return -7.5f - 2.5f * (float)(sf - SF_LO);
}

float margin_db(float snr, uint8_t sf) { return snr - sf_floor_db(sf); }

uint8_t bars_from_margin(float m) {
  if (!(m >= 0.f)) return 0;  // also catches NAN
  if (m < 3.f) return 1;
  if (m < 6.f) return 2;
  if (m < 10.f) return 3;
  return 4;
}

LinkManager::LinkManager(uint8_t sf_start) : sf_(sf_start), req_(sf_start) {}

LinkManager::Action LinkManager::onPing(const LinkSample& s) {
  hist_ = ((hist_ << 1) | (s.acked ? 1u : 0u)) & ((1u << HIST_LEN) - 1);
  if (hist_n_ < HIST_LEN) hist_n_++;

  if (!s.acked) {
    last_acked_ = false;
    good_ = 0;
    if (misses_ < 255) misses_++;
    if (misses_ >= RESCUE_MISSES) {
      req_ = SF_HI;
      return Action::Rescue;
    }
    return Action::None;
  }

  misses_ = 0;
  last_acked_ = true;
  const float up = margin_db(s.snr_up, sf_), dn = margin_db(s.snr_dn, sf_);
  last_margin_ = up < dn ? up : dn;
  if (!adaptive_) return Action::None;

  if (last_margin_ < STEP_UP_BELOW_DB) {
    good_ = 0;
    if (sf_ >= SF_HI) return Action::None;
    req_ = (uint8_t)(sf_ + 1);
    return Action::RequestSf;
  }
  if (last_margin_ > STEP_DOWN_ABOVE_DB) {
    if (good_ == 0 || last_margin_ < good_min_) good_min_ = last_margin_;
    if (good_ < 255) good_++;
    // A very strong link (over 20 dB to spare on 2 pings running) needs no long streak: after a
    // rescue or a reboot both ends sit on SF12, and at SF12 pings are slow (minutes for 5).
    const bool strong = (good_ >= 2 && good_min_ > 20.f) || good_min_ > 25.f;   // > 25 dB: one ping is enough
    if ((good_ >= STEP_DOWN_STREAK || strong) && sf_ > SF_LO) {
      int steps = (int)floorf((good_min_ - STEP_DOWN_KEEP_DB) / DB_PER_SF);
      if (steps < 1) steps = 1;
      if (steps > sf_ - SF_LO) steps = sf_ - SF_LO;
      good_ = 0;
      good_min_ = 99.f;
      req_ = (uint8_t)(sf_ - steps);
      return Action::RequestSf;
    }
    return Action::None;
  }
  good_ = 0;
  return Action::None;
}

void LinkManager::confirmSf(uint8_t sf) {
  sf_ = req_ = sf;
  misses_ = 0;
  good_ = 0;
  good_min_ = 99.f;
}

void LinkManager::restore(const State& st) {
  if (st.sf != sf_) return;
  good_ = st.good < STEP_DOWN_STREAK ? st.good : (uint8_t)(STEP_DOWN_STREAK - 1);
  good_min_ = st.good_min;
  last_margin_ = st.last_margin;
}

float LinkManager::successPct() const {
  if (hist_n_ == 0) return NAN;  // nothing tried yet: not "0 % success"
  uint8_t ok = 0;
  for (uint8_t i = 0; i < hist_n_; i++) ok += (hist_ >> i) & 1u;
  return 100.f * ok / hist_n_;
}

uint8_t LinkManager::bars() const { return last_acked_ ? bars_from_margin(last_margin_) : 0; }

uint32_t ping_interval_ms(uint32_t ping_air_ms, uint32_t ack_air_ms, uint32_t min_ms) {
  const uint32_t a = 20 * (ping_air_ms + ack_air_ms);
  return a > min_ms ? a : min_ms;
}

uint32_t hunt_interval_ms(uint32_t ping_air_ms, uint32_t ack_air_ms, uint32_t hunt_ms) {
  const uint32_t a = 4 * (ping_air_ms + ack_air_ms);
  return a > hunt_ms ? a : hunt_ms;
}

uint32_t rescue_silence_ms(uint32_t interval_ms, uint32_t ping_air_ms, uint32_t floor_ms) {
  const uint32_t a = 3 * interval_ms + ping_air_ms;
  return a > floor_ms ? a : floor_ms;
}

bool SilenceWatch::shouldRescue(uint32_t now, uint8_t current_sf, uint32_t silence_ms) const {
  if (asleep_ || current_sf == SF_HI) return false;
  return (uint32_t)(now - last_) >= silence_ms;
}

uint8_t rescue_probe_sf(uint8_t i, uint8_t link_sf, uint8_t pending_sf) {
  if ((i & 1) == 0) return SF_HI;
  const uint8_t span = SF_HI - SF_LO;  // 5 SFs below 12
  const uint8_t base = (link_sf >= SF_LO && link_sf < SF_HI) ? link_sf : SF_LO;
  uint8_t k = (uint8_t)(i / 2);
  if (pending_sf >= SF_LO && pending_sf < SF_HI) {
    if (k == 0) return pending_sf;
    k--;
  }
  return (uint8_t)(SF_LO + (base - SF_LO + k) % span);
}

TxExpiry tx_first_try_expiry(uint32_t now, uint32_t queued_at, uint32_t head_since,
                             uint32_t holdoff_end, uint32_t deadline_ms, uint32_t max_queue_ms) {
  const uint32_t base = (int32_t)(queued_at - holdoff_end) >= 0 ? queued_at : holdoff_end;
  const uint32_t head = (int32_t)(head_since - base) >= 0 ? head_since : base;
  if ((int32_t)(now - (head + deadline_ms)) >= 0) return TxExpiry::AtHead;
  if ((int32_t)(now - (base + max_queue_ms)) >= 0) return TxExpiry::Queued;
  return TxExpiry::None;
}
