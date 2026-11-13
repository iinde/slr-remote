#include "buttons.h"

namespace {
constexpr uint32_t CLICK_DEBOUNCE_MS = 25;
constexpr uint32_t LONG_MS = 1000;
constexpr uint32_t VLONG_MS = 4000;
constexpr uint32_t STAGE_DEBOUNCE_MS = 15;

// Generic debouncer step: returns true if the stable state changed.
bool debounce(bool in, uint32_t now, bool& raw, uint32_t& since, bool& stable, uint32_t ms) {
  if (in != raw) {
    raw = in;
    since = now;
  }
  if (raw != stable && (uint32_t)(now - since) >= ms) {
    stable = raw;
    return true;
  }
  return false;
}
}  // namespace

BtnEvent ClickButton::update(bool pressed, uint32_t now) {
  if (debounce(pressed, now, raw_, raw_since_, stable_, CLICK_DEBOUNCE_MS)) {
    if (stable_) {
      down_at_ = raw_since_;  // hold time counts from the first edge
      long_sent_ = vlong_sent_ = false;
      return BtnEvent::None;
    }
    return long_sent_ ? BtnEvent::None : BtnEvent::Click;
  }
  if (!stable_) return BtnEvent::None;
  const uint32_t held = now - down_at_;
  if (!long_sent_ && held >= LONG_MS) {
    long_sent_ = true;
    return BtnEvent::Long;
  }
  if (long_sent_ && !vlong_sent_ && held >= VLONG_MS) {
    vlong_sent_ = true;
    return BtnEvent::VeryLong;
  }
  return BtnEvent::None;
}

StageEvent TwoStage::update(bool half, bool full, uint32_t now) {
  debounce(half, now, h_raw_, h_since_, h_, STAGE_DEBOUNCE_MS);
  debounce(full, now, f_raw_, f_since_, f_, STAGE_DEBOUNCE_MS);
  const uint8_t lvl = f_ ? 2 : (h_ ? 1 : 0);
  if (lvl == level_) return StageEvent::None;
  const uint8_t prev = level_;
  level_ = lvl;
  if (lvl == 0) return StageEvent::AllUp;
  if (lvl == 2) return StageEvent::FullDown;
  return prev == 0 ? StageEvent::HalfDown : StageEvent::FullUp;
}
