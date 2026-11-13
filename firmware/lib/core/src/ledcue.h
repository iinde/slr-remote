// LED cues (spec 1.7): n short blinks, or one long one. Non-blocking: ask on() each loop.
#pragma once
#include <stdint.h>

constexpr uint8_t LED_LONG = 0;  // pattern 0 = one long blink (stopped)

class LedCue {
 public:
  // pattern: 1..9 short blinks, LED_LONG = one 800 ms blink. Replaces any running cue.
  void start(uint8_t pattern, uint32_t now) {
    pat_ = pattern > 9 ? 9 : pattern;
    t0_ = now;
    active_ = true;
  }
  bool on(uint32_t now) {
    if (!active_) return false;
    const uint32_t t = now - t0_;
    if (pat_ == LED_LONG) {
      if (t >= LONG_MS) active_ = false;
      return active_;
    }
    if (t >= (uint32_t)pat_ * PERIOD_MS) {
      active_ = false;
      return false;
    }
    return (t % PERIOD_MS) < ON_MS;
  }
  bool active() const { return active_; }
 private:
  static constexpr uint32_t ON_MS = 120, PERIOD_MS = 300, LONG_MS = 800;
  uint8_t pat_ = 0;
  uint32_t t0_ = 0;
  bool active_ = false;
};
