// One-button press classifier (docs/specs/2026-11-08-one-button-ux.md 1.3).
// Turns raw samples of one active-low contact into Down / Enter(zone) / Up(zone) events.
// What a zone means is decided by a ZoneTable, chosen by the mode engine for the screen the
// press started on. A table is latched at the press-down edge, so a table change while the
// finger is down never changes what the running press means.
#pragma once
#include <stdint.h>

enum class PressStyle : uint8_t { Classic = 0, A = 1, B = 2, C = 3 };

// What reaching or letting go in a zone means.
enum class Zone : uint8_t {
  Tap,       // let go here = the screen's tap action
  Af,        // focus held, let go = shoot (A: SHOT, AF LOCK, TIMER; C: HOLD AF)
  Shutter,   // shutter held (BURST hold), let go = release
  Modes,     // let go = mode card (A)
  Menu,      // let go = mode card (C ladder, labelled MENU)
  Off,       // let go = deep sleep (C ladder)
  Nothing,   // let go = nothing
  Cancel,    // AF timed out and released; let go = nothing (C HOLD AF, aux fitted)
  Pick,      // reached: pick now (card, value editor, arm, yes/no)
  NextView,  // reached: next view now
  Wheel,     // reached: B wheel opens and steps by itself; let go = pick
  Unlock,    // reached: AF LOCK unlocks now, no shot
  Stuck,     // reached: stuck-button guard
};

constexpr uint8_t ZONE_MAX = 8;
struct ZoneTable {
  uint8_t n = 1;                 // zones in use
  uint32_t from[ZONE_MAX] = {0}; // from[0] = 0, strictly rising
  Zone zone[ZONE_MAX] = {Zone::Tap};
};

// What the screen the press starts on does with a hold.
enum class PressCtx : uint8_t {
  Shutter,     // SHOT, AF LOCK unlocked, TIMER: hold = AF
  HoldAf,      // HOLD AF mode: hold = AF, past the limit CANCEL, then modes (or wheel)
  Burst,       // BURST: hold = shutter held, capped
  Plain,       // INTERVAL, BULB, VIDEO: hold 1 s = modes
  View,        // LINK / HUNT / GPS / RANGE views: hold 1 s = next view
  Picker,      // card, value editor, arm, yes/no: hold 1 s = pick
  Locked,      // AF LOCK locked: tap = fire, hold 1 s = unlock
  Wake,        // the press that woke the board: hold 0.6 s = modes, never AF
};

constexpr uint32_t PRESS_DEBOUNCE_MS = 25;
constexpr uint32_t PRESS_AF_START_MS = 300;    // A: tap zone ends, half-press starts
constexpr uint32_t PRESS_HOLD_MS = 1000;       // pick, next view, plain modes
constexpr uint32_t PRESS_MODES_SPAN_MS = 3000; // A: L .. L+3 s = modes
constexpr uint32_t PRESS_STUCK_MS = 15000;     // stuck-button guard
constexpr uint32_t PRESS_BURST_CAP_MS = 10000; // BURST hold cap (from the shutter press)
constexpr uint32_t PRESS_WAKE_HOLD_MS = 600;   // hold-to-wake threshold
constexpr uint32_t PRESS_C_OFF_MS = 4000;      // C ladder
constexpr uint32_t PRESS_C_NOTHING_MS = 8000;
constexpr uint32_t PRESS_PLAIN_NOTHING_MS = 7000;
constexpr uint32_t PRESS_HOLDAF_EXIT_MS = 1000;  // HOLD AF: CANCEL lasts this long, then modes

// aux: a second button is fitted (R2/R2b), so PRG is a pure shutter: the AF limit ends in
// Cancel ("AF TIMED OUT") instead of opening the mode card.
ZoneTable press_zones(PressStyle st, PressCtx ctx, uint32_t af_limit_ms, bool aux = false);
Zone zone_at(const ZoneTable& t, uint32_t held_ms);

enum class PressEvt : uint8_t { None, Down, Enter, Up };
struct PressEvent {
  PressEvt type = PressEvt::None;
  Zone zone = Zone::Tap;  // Enter: the zone reached. Up: the zone let go in
  uint32_t at = 0;        // when it happened (Down: the first edge)
  uint32_t held = 0;      // ms held at this event
};

class PressClassifier {
 public:
  // Used for the next press (latched at its down edge).
  void setTable(const ZoneTable& t) { next_ = t; }
  // The board woke with the button already down (deep-sleep wake). down_at: best guess of
  // when it went down (0 = at boot). No Down event is emitted for it.
  void assumeDown(uint32_t down_at, uint32_t now);
  // One event per call at most; call often (every loop and from blocking waits).
  PressEvent update(bool pressed, uint32_t now);
  bool isDown() const { return stable_; }
  uint32_t heldMs(uint32_t now) const { return stable_ ? now - down_at_ : 0; }
  Zone zone() const { return cur_.zone[zi_]; }
 private:
  ZoneTable next_, cur_;
  bool raw_ = false, stable_ = false;
  uint32_t raw_since_ = 0, down_at_ = 0;
  uint8_t zi_ = 0;
};
