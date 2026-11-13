#include "shotseq.h"

namespace {
constexpr uint32_t HOLD_MS = 200;  // timing::SHUTTER_HOLD_MS (config.h is outside lib/core)
bool due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
}  // namespace

void ShotSequencer::push(CamCmd c, uint32_t at) {
  if (n_ < sizeof(q_) / sizeof(q_[0])) q_[n_++] = Step{c, at};
}

void ShotSequencer::clear() {
  n_ = 0;
  oneshot_ = false;
  timed_ = false;
}

bool ShotSequencer::busy() const { return oneshot_ && n_ > 0; }

bool ShotSequencer::releaseQueued() const {
  for (uint8_t i = 0; i < n_; i++)
    if (q_[i].cmd == CamCmd::Release) return true;
  return false;
}

bool ShotSequencer::startShot(uint32_t now, uint16_t focus_ms) {
  if (busy()) return false;
  clear();
  oneshot_ = true;
  focus_ms_ = focus_ms;
  af_locked_ = false;
  push(CamCmd::FocusPress, now);
  push(CamCmd::ShutterPress, now + focus_ms);
  push(CamCmd::Release, now + focus_ms + HOLD_MS);
  return true;
}

void ShotSequencer::half(uint32_t now) {
  clear();
  push(CamCmd::FocusPress, now);
}

void ShotSequencer::full(uint32_t now) {
  clear();
  if (!focusing_) push(CamCmd::FocusPress, now);
  push(CamCmd::ShutterPress, now);
}

void ShotSequencer::fullTimed(uint32_t now, uint16_t hold_ms) {
  clear();
  timed_ = true;
  hold_ms_ = hold_ms;
  if (!focusing_) push(CamCmd::FocusPress, now);
  push(CamCmd::ShutterPress, now);
  push(CamCmd::Release, now + hold_ms);
}

void ShotSequencer::release(uint32_t now) {
  clear();
  push(CamCmd::Release, now);
}

void ShotSequencer::stepDone(CamCmd done, uint32_t now) {
  if (!oneshot_ && !timed_) return;  // two-stage steps run when the user says, not on a timer
  const uint32_t hold = timed_ ? hold_ms_ : HOLD_MS;
  for (uint8_t i = 0; i < n_; i++) {
    if (timed_ && done == CamCmd::FocusPress) break;  // no AF wait: the shutter follows at once
    if (done == CamCmd::FocusPress && q_[i].cmd == CamCmd::ShutterPress) {
      if (!af_locked_ && due(now + focus_ms_, q_[i].at)) q_[i].at = now + focus_ms_;
      if (i + 1 < n_ && q_[i + 1].cmd == CamCmd::Release && due(q_[i].at + HOLD_MS, q_[i + 1].at))
        q_[i + 1].at = q_[i].at + HOLD_MS;
      return;
    }
    if (done == CamCmd::ShutterPress && q_[i].cmd == CamCmd::Release) {
      if (due(now + hold, q_[i].at)) q_[i].at = now + hold;
      return;
    }
  }
}

void ShotSequencer::afLocked(uint32_t now) {
  if (!busy()) return;
  af_locked_ = true;
  for (uint8_t i = 0; i < n_; i++) {
    if (q_[i].cmd == CamCmd::ShutterPress) {
      q_[i].at = now;
      if (i + 1 < n_) q_[i + 1].at = now + HOLD_MS;
      return;
    }
  }
}

bool ShotSequencer::poll(uint32_t now, CamCmd& out) {
  if (n_ == 0 || !due(now, q_[0].at)) return false;
  out = q_[0].cmd;
  for (uint8_t i = 1; i < n_; i++) q_[i - 1] = q_[i];
  n_--;
  if (out == CamCmd::Release) focusing_ = shutter_ = false;
  else focusing_ = true;  // FocusPress or ShutterPress: shutter button is down
  if (out == CamCmd::ShutterPress) shutter_ = true;
  if (n_ == 0) oneshot_ = timed_ = false;
  return true;
}
