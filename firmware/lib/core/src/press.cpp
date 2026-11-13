#include "press.h"

namespace {

struct Builder {
  ZoneTable t;
  Builder() { t.n = 1; t.from[0] = 0; t.zone[0] = Zone::Tap; }
  // Adds a zone starting at ms (ignored if not after the last one, or the table is full).
  Builder& at(uint32_t ms, Zone z) {
    if (t.n < ZONE_MAX && ms > t.from[t.n - 1]) {
      t.from[t.n] = ms;
      t.zone[t.n] = z;
      t.n++;
    }
    return *this;
  }
  // The stuck guard always comes last, at least 2 s after the last real zone.
  ZoneTable stuck() {
    uint32_t s = PRESS_STUCK_MS;
    if (t.from[t.n - 1] + 2000 > s) s = t.from[t.n - 1] + 2000;
    at(s, Zone::Stuck);
    return t;
  }
};

}  // namespace

ZoneTable press_zones(PressStyle st, PressCtx ctx, uint32_t L, bool aux) {
  if (L < PRESS_AF_START_MS + 500) L = PRESS_AF_START_MS + 500;
  Builder b;
  // Shared by every style.
  switch (ctx) {
    case PressCtx::View: return b.at(PRESS_HOLD_MS, Zone::NextView).stuck();
    case PressCtx::Picker: return b.at(PRESS_HOLD_MS, Zone::Pick).stuck();
    case PressCtx::Locked: return b.at(PRESS_HOLD_MS, Zone::Unlock).stuck();
    case PressCtx::HoldAf: {
      // AF, then CANCEL (AF lets go, nothing fires) at the limit. With PRG as the only
      // button, holding on past CANCEL is the way out of HOLD AF: the mode card (A, C) or
      // the wheel (B). With a second button fitted that button opens the card instead.
      b.at(PRESS_AF_START_MS, Zone::Af).at(L, Zone::Cancel);
      if (aux) return b.stuck();
      const uint32_t out = L + PRESS_HOLDAF_EXIT_MS;
      if (st == PressStyle::B) return b.at(out, Zone::Wheel).stuck();
      return b.at(out, Zone::Modes).at(out + PRESS_MODES_SPAN_MS, Zone::Nothing).stuck();
    }
    case PressCtx::Wake:
      // The press that woke the board (every style shares the wake rules, spec 1.3): a tap,
      // or still down at 0.6 s = the style's own way to the modes. Never AF, never a shot.
      if (aux) return b.at(PRESS_WAKE_HOLD_MS, Zone::Nothing).stuck();
      switch (st) {
        case PressStyle::Classic: return b.stuck();
        case PressStyle::B: return b.at(PRESS_WAKE_HOLD_MS, Zone::Wheel).stuck();
        case PressStyle::C:
          return b.at(PRESS_WAKE_HOLD_MS, Zone::Menu)
              .at(PRESS_C_OFF_MS, Zone::Off)
              .at(PRESS_C_NOTHING_MS, Zone::Nothing)
              .stuck();
        default:
          return b.at(PRESS_WAKE_HOLD_MS, Zone::Modes).at(PRESS_PLAIN_NOTHING_MS, Zone::Nothing).stuck();
      }
    default: break;
  }
  switch (st) {
    case PressStyle::Classic: return b.stuck();
    case PressStyle::B: return b.at(PRESS_HOLD_MS, Zone::Wheel).stuck();
    case PressStyle::C:
      return b.at(PRESS_HOLD_MS, Zone::Menu)
          .at(PRESS_C_OFF_MS, Zone::Off)
          .at(PRESS_C_NOTHING_MS, Zone::Nothing)
          .stuck();
    case PressStyle::A: break;
  }
  switch (ctx) {
    case PressCtx::Shutter:
      b.at(PRESS_AF_START_MS, Zone::Af);
      if (aux) return b.at(L, Zone::Cancel).stuck();
      return b.at(L, Zone::Modes).at(L + PRESS_MODES_SPAN_MS, Zone::Nothing).stuck();
    case PressCtx::Burst: {
      const uint32_t cap = PRESS_AF_START_MS + PRESS_BURST_CAP_MS;
      b.at(PRESS_AF_START_MS, Zone::Shutter);
      if (aux) return b.at(cap, Zone::Cancel).stuck();
      return b.at(cap, Zone::Modes).at(cap + PRESS_MODES_SPAN_MS, Zone::Nothing).stuck();
    }
    case PressCtx::Plain:
    default:
      if (aux) return b.at(PRESS_HOLD_MS, Zone::Nothing).stuck();
      return b.at(PRESS_HOLD_MS, Zone::Modes).at(PRESS_PLAIN_NOTHING_MS, Zone::Nothing).stuck();
  }
}

Zone zone_at(const ZoneTable& t, uint32_t held) {
  uint8_t i = 0;
  while (i + 1 < t.n && held >= t.from[i + 1]) i++;
  return t.zone[i];
}

void PressClassifier::assumeDown(uint32_t down_at, uint32_t now) {
  raw_ = stable_ = true;
  raw_since_ = now;
  down_at_ = down_at;
  cur_ = next_;
  zi_ = 0;
}

PressEvent PressClassifier::update(bool pressed, uint32_t now) {
  PressEvent e;
  if (pressed != raw_) {
    raw_ = pressed;
    raw_since_ = now;
  }
  if (raw_ != stable_ && (uint32_t)(now - raw_since_) >= PRESS_DEBOUNCE_MS) {
    if (!raw_) {
      // Let go. Zones it reached before the opening edge but that a slow caller has not
      // been told about yet come first, in order, so Up never skips an Enter.
      const uint32_t held = raw_since_ - down_at_;
      if (zi_ + 1 < cur_.n && held >= cur_.from[zi_ + 1]) {
        zi_++;
        e.type = PressEvt::Enter;
        e.zone = cur_.zone[zi_];
        e.at = down_at_ + cur_.from[zi_];
        e.held = held;
        return e;
      }
    }
    stable_ = raw_;
    if (stable_) {
      down_at_ = raw_since_;  // hold time counts from the first edge
      cur_ = next_;
      zi_ = 0;
      e.type = PressEvt::Down;
      e.zone = cur_.zone[0];
      e.at = down_at_;
      return e;
    }
    // Up: the zone it was let go in, by the time of the first opening edge.
    const uint32_t held = raw_since_ - down_at_;
    e.type = PressEvt::Up;
    e.zone = zone_at(cur_, held);
    e.at = raw_since_;
    e.held = held;
    return e;
  }
  if (!stable_) return e;
  // While held: one zone step per call, in order, so none is skipped. An opening edge that
  // is still being debounced stops the clock at that edge (a 280 ms tap must not reach AF
  // during its own release debounce).
  const uint32_t held = (raw_ ? now : raw_since_) - down_at_;
  if (zi_ + 1 < cur_.n && held >= cur_.from[zi_ + 1]) {
    zi_++;
    e.type = PressEvt::Enter;
    e.zone = cur_.zone[zi_];
    e.at = down_at_ + cur_.from[zi_];
    e.held = held;
  }
  return e;
}
