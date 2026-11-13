// One-button mode engine (docs/specs/2026-11-08-one-button-ux.md). Pure logic: press events,
// the clock and a little link/battery state go in; a short list of camera actions and a
// screen description come out. The app turns actions into its existing command() calls, so
// queueing, stages, BUSY and CONNECTING are unchanged.
//
// Hard guarantees (tested in test/test_onebutton):
//  - one press never fires twice (one terminal action per press);
//  - every Focus/Shoot is followed by exactly one Release on every exit path (let go, AF
//    limit, stop, timeout, stuck, forceStop for sleep/off/battery);
//  - anything running (timer, interval, burst, bulb) stops on the press-down itself, the
//    rest of that press is swallowed, and the screen says STOPPED and why;
//  - repeating relay modes are costed before they start and per shot (seqplan.h).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "press.h"
#include "seqplan.h"

enum class ObMode : uint8_t { Shot = 0, AfLock, Timer, Interval, Burst, Bulb, Video, HoldAf, Count };
constexpr uint8_t OB_MODE_COUNT = (uint8_t)ObMode::Count;
const char* ob_mode_name(ObMode m);  // "SHOT", "AF LOCK", ...
const char* ob_mode_key(ObMode m);   // "shot", "aflock", "timer", "interval", "burst", ...
bool ob_mode_from_key(const char* key, ObMode& out);
const char* ob_style_key(PressStyle s);  // "classic", "a", "b", "c"
bool ob_style_from_key(const char* key, PressStyle& out);

constexpr uint8_t OB_MAX_MODES = 8;
constexpr uint32_t OB_SHUTTER_HOLD_MS = 200;   // = timing::SHUTTER_HOLD_MS
constexpr uint32_t OB_CARD_IDLE_MS = 6000;     // mode card / value editor close with no change
constexpr uint32_t OB_ARM_IDLE_MS = 15000;
constexpr uint32_t OB_LATE_MS = 1500;          // a press this soon after a timer fired = TOO LATE
constexpr uint8_t OB_ARM_MIN_BATT = 15;        // INTERVAL and BULB refuse to start below this
constexpr uint8_t OB_STOP_BATT = 5;            // anything running stops at this
constexpr uint8_t OB_FAIL_STREAK = 3;
constexpr uint8_t OB_BAR_MAX = 24;             // label bar chars that fit the screen (5 px each)          // interval: 3 failed / refused shots in a row stop it

struct ObSettings {
  PressStyle style = PressStyle::A;
  uint8_t af_limit_s = 4;     // A: hold this long and AF lets go, nothing fires (2..15)
  uint8_t aflock_s = 30;      // AF LOCK auto release (5..120)
  uint8_t timer_s = 10;       // TIMER countdown (1..60)
  uint16_t iv_every_s = 10;   // INTERVAL every (2..3600)
  uint16_t iv_count = 120;    // INTERVAL shots, 0 = until stopped (never past 4 h)
  uint16_t burst_ms = 1000;   // BURST tap (200..10000)
  uint16_t bulb_s = 120;      // BULB (1..1800)
  uint16_t hold_cap_s = 1800;  // direct BULB/BURST hard cap. Bench-tested: the X-E5 lets go by itself when the remote dies
  uint16_t focus_ms = 500;    // one-press focus wait (Settings.focus_ms)
  bool wake_act = true;       // a wake tap in SHOT takes the photo (false: wake only)
  bool aux = false;           // second button fitted (R2/R2b): PRG is a pure shutter
  uint8_t n_modes = 4;        // modes on the card, in order (SHOT is always first)
  ObMode modes[OB_MAX_MODES] = {ObMode::Shot, ObMode::AfLock, ObMode::Timer, ObMode::Interval};
};
// Puts every value back inside its range (never refuses: serial.cpp refuses with a reason).
void ob_clamp(ObSettings& s);

struct ObEnv {
  uint32_t now = 0;
  bool relay = false;
  bool link_ok = true;        // direct: camera BLE ready; relay: camera board answering
  uint8_t sf = 7;
  uint16_t hh_permille = 0;   // handheld airtime used (relay)
  uint16_t cam_permille = 0;  // camera board airtime used, from its last Ack (relay)
  uint8_t batt = 255;         // handheld %, 255 = unknown (USB)
  uint32_t lead_ms = 0;       // relay: a timed Shot leaves this much before zero
  uint8_t n_views = 1;        // MAIN + views
  bool af_confirmed = true;   // relay: the camera board has acked the Focus
};

enum class ObAct : uint8_t {
  Shot,     // one-press focus -> wait -> shoot -> release. arg = focus ms. tag set
  Focus,    // half-press, held
  Shoot,    // full-press. arg = max hold ms: the camera end lets go by itself after it (a
            // timed "shoot now", so a slow or lost relay Release never makes a CH burst);
            // 0 = held until Release (BURST, BULB). Either way a Release follows. tag set
  Release,  // let go of everything
  Off,      // deep sleep now. arg 1 = stuck button
  Setup,    // restart into the setup menu
  Save,     // settings or mode changed: persist (settings())
  Led,      // arg = LedCue pattern
  RunFlag,  // arg 1: a run started (INTERVAL, BULB), 0: it ended. Survives a reset in NVS
  CancelPending,  // a run was stopped: drop the shot with this tag if it has not left the
                  // board yet (direct: parked waiting for the camera; relay: still queued,
                  // never on air). The app answers at once with onCancel(tag, ...)
};

// The app's answer to CancelPending.
enum class ObCancel : uint8_t {
  Cancelled,    // it had not left the board: dropped, it will never fire
  AlreadySent,  // it is on air or with the camera: it may still fire
  None,         // nothing of it is pending any more (already settled or never sent)
};
struct ObAction {
  ObAct act;
  uint16_t arg;
  uint16_t tag;  // Shot/Shoot: report the outcome with onOutcome(tag, ...)
};

enum class ObOutcome : uint8_t { Ok, Fail, Duty, Busy, Unknown };

enum class ObStop : uint8_t { ByPress, Done, AirLimit, NoLink, NoCamera, Battery, HoldCap, TimeLimit, Reset, Sleep };
const char* ob_stop_text(ObStop r);

enum class ObKind : uint8_t {
  Home,   // the shot-status main page (app draws the stage), plus the bar
  Text,   // headline, up to three lines, optional progress
  Count,  // a very big number or time
  List,   // header, scrolling list with a cursor
};

constexpr uint8_t OB_LIST_MAX = 12;
struct ObScreen {
  ObKind kind = ObKind::Home;
  char title[16] = {0};   // top-left tag: mode and value
  char big[20] = {0};
  char right[16] = {0};   // small text beside the headline
  char l1[32] = {0}, l2[32] = {0}, l3[32] = {0};
  int16_t progress = -1;  // 0..1000, -1 = none
  char bar[28] = {0};     // the label bar: what a tap / hold / letting go does now
  bool alert = false;
  char head[28] = {0};    // List header
  uint8_t n = 0, sel = 0;
  char item[OB_LIST_MAX][14];
  char val[OB_LIST_MAX][16];
};

// The app's view of the engine's view page (the engine changes it by itself). A change
// starts a fresh HUNT best; a view page shows the label bar only while a press is down or
// for OB_VIEW_BAR_MS after it opened (its own bottom rows sit where the bar goes).
constexpr uint32_t OB_VIEW_BAR_MS = 2500;
struct ObViewWatch {
  uint8_t last = 0;
  uint32_t at = 0;
  void seed(uint8_t v, uint32_t now) {
    last = v;
    at = now;
  }
  // true once per change of view
  bool update(uint8_t v, uint32_t now) {
    if (v == last) return false;
    seed(v, now);
    return true;
  }
  bool barVisible(uint8_t v, bool press_active, uint32_t now) const {
    return v == 0 || press_active || now - at < OB_VIEW_BAR_MS;
  }
};

class ModeEngine {
 public:
  void configure(const ObSettings& s);
  const ObSettings& settings() const { return s_; }
  ObMode mode() const { return mode_; }
  void setMode(ObMode m);
  uint8_t view() const { return view_; }
  void setView(uint8_t v) { view_ = v; }

  // The zone table for the next PRG press (call after every engine call).
  ZoneTable table() const;
  // Wake: the press that woke the board. Still down: classifier assumeDown(e.now) with
  // wakeTable(), then wakeDown(), then feed its events (0.6 s counts from e.now).
  // Already let go during boot: wakeTap(). aux: the second button woke us (wake only).
  ZoneTable wakeTable() const;
  void wakeDown(const ObEnv& e);
  void wakeTap(const ObEnv& e, bool aux = false);

  void onPress(const PressEvent& ev, const ObEnv& e);
  // Second button (R2/R2b): its own classifier with auxTable().
  ZoneTable auxTable() const;
  void onAux(const PressEvent& ev, const ObEnv& e);
  void tick(const ObEnv& e);
  void onOutcome(uint16_t tag, ObOutcome o, const ObEnv& e);
  // The app's answer to ObAct::CancelPending (called straight after it ran the action).
  void onCancel(uint16_t tag, ObCancel r, const ObEnv& e);
  // Stop everything now (sleep, off, battery, stuck): releases anything held. Says why.
  void forceStop(ObStop why, const ObEnv& e);
  // A run was cut by a reset (found from the NVS run flag at boot).
  void noteReset();
  // The button is up but this press never got its Up (the app's event queue overflowed):
  // close it without firing, release anything it held, and say so.
  void abortPress(const ObEnv& e);
  // The app consumed the current press itself (for example to dismiss an alert screen).
  void swallowPress() { p_.swallow = true; }

  bool popAction(ObAction& out);
  void screen(ObScreen& out, const ObEnv& e) const;

  bool running() const;   // timer, interval, burst, bulb
  bool held() const { return af_held_ || shut_held_; }
  // Keep the board awake: something runs, is held, is locked, or a card/arm screen is open.
  bool busy() const;
  bool pressActive() const { return p_.active; }
  bool videoRecording() const { return rec_; }
  // VIDEO: we sent a start (or do not know). Kept across a deep sleep by the app, which then
  // calls recUnknownAfterSleep(): the camera may still be recording, so the screen says
  // REC ? and the tap is a plain PRESS, never a REC that would really stop it.
  // Kept whatever the mode: leaving VIDEO (or sleeping in another mode) while it may be
  // recording keeps the flag, and coming back to VIDEO shows REC ? (never TAP: REC).
  bool recMaybe() const { return rec_ || rec_unknown_ || rec_left_; }
  void recUnknownAfterSleep();

 private:
  enum class Scr : uint8_t { Home, Card, Editor, Arm, Refused, Locked, TimerRun, IntervalRun, BurstRun, BulbOpen, Confirm };
  enum class Ed : uint8_t { Timer, AfLock, IvEvery, IvCount, Burst, Bulb };
  enum class Item : uint8_t { Mode, Views, Setup, Off };
  struct CardItem {
    Item kind;
    ObMode mode;
  };
  struct Press {
    bool active = false, swallow = false, used = false, wake = false;
    bool af = false, shutter = false, wheel = false, aux = false;
    bool nolink = false;  // direct: reached AF with no camera link, so no Focus went out
    PressCtx ctx = PressCtx::Shutter;
    Zone zone = Zone::Tap;
    uint32_t down_at = 0, af_at = 0, shut_at = 0, wheel_at = 0;
    uint8_t wheel_base = 0;
  };
  struct Msg {
    bool on = false;
    uint32_t until = 0;  // 0 = until the next press
    char big[20] = {0};
    char l1[28] = {0}, l2[28] = {0}, l3[28] = {0};
  };

  void push(ObAct a, uint16_t arg = 0, uint16_t tag = 0);
  uint16_t newTag();
  PressCtx ctx() const;
  void onDown(const PressEvent& ev, const ObEnv& e);
  void onEnter(const PressEvent& ev, const ObEnv& e);
  void onUp(const PressEvent& ev, const ObEnv& e);
  void tap(const ObEnv& e);
  void modeTap(const ObEnv& e);
  void wakeTapAction(const ObEnv& e);
  void afLetGo(const ObEnv& e);
  void shootNow(const ObEnv& e);
  bool emitShot(uint16_t focus, const ObEnv& e, uint16_t* tag_out = nullptr);
  void releaseHeld();
  void startAf(const ObEnv& e);
  void cancelPending(uint8_t ctx);
  // AF or the shutter is held but nothing owns the hold any more (no press, no AF LOCK, no
  // timer or run, no timed shot pending): a lost Up. Release it.
  bool orphanHeld() const;
  bool shotPending() const { return shoot_due_ || release_due_; }
  void stopRun(ObStop why, const ObEnv& e);
  void setMsg(const char* big, const char* l1 = "", const char* l2 = "", uint32_t until = 0);
  void openCard(const ObEnv& e, bool from_af);
  uint8_t buildCard(CardItem* out) const;
  void pickCard(uint8_t i, const ObEnv& e);
  void openEditor(Ed what, const ObEnv& e);
  uint8_t edList(Ed what, uint32_t* out) const;
  uint32_t edValue(Ed what) const;
  void edSet(Ed what, uint32_t v);
  void openArm(const ObEnv& e);
  void startInterval(const ObEnv& e);
  void ivTick(const ObEnv& e);
  uint32_t lastOutcomeWaitMs(const ObEnv& e) const;  // INTERVAL: wait for the last outcome
  PlanOut plan(const ObEnv& e) const;
  void barHome(char* out, size_t cap) const;
  void titleFor(char* out, size_t cap) const;
  uint32_t afLimitMs() const { return (uint32_t)s_.af_limit_s * 1000; }
  uint32_t capMs(uint32_t want_ms) const;

  ObSettings s_;
  ObMode mode_ = ObMode::Shot;
  Scr scr_ = Scr::Home;
  uint8_t view_ = 0;
  Press p_;
  Msg msg_;
  ObAction q_[12];
  uint8_t qn_ = 0;
  uint16_t tag_ = 0;
  uint16_t pending_tag_ = 0;  // the last Shot/Shoot with no outcome yet (CancelPending)
  uint8_t cancel_ctx_ = 0;    // what onCancel amends: 1 a stopped run, 2 a TOO LATE timer

  bool af_held_ = false, shut_held_ = false;
  bool shoot_due_ = false, release_due_ = false;
  uint32_t shoot_at_ = 0, release_at_ = 0;
  uint32_t last_input_ = 0;
  bool late_ = false;
  uint32_t late_until_ = 0;

  // AF LOCK
  uint32_t lock_until_ = 0;
  // TIMER
  uint32_t timer_end_ = 0, timer_len_ = 0;
  bool timer_af_ = false, timer_fired_ = false;
  // INTERVAL
  uint16_t iv_every_ = 0, iv_count_ = 0, iv_k_ = 0, iv_ok_ = 0, iv_fail_ = 0, iv_skip_ = 0, iv_unk_ = 0;
  uint8_t iv_streak_fail_ = 0, iv_streak_skip_ = 0, iv_out_ = 0;
  uint16_t iv_first_tag_ = 0;
  uint16_t iv_seen_[8] = {0};  // tags already counted
  uint8_t iv_seen_i_ = 0;
  uint32_t iv_start_ = 0, iv_next_ = 0, iv_done_wait_ = 0;
  // BURST / BULB
  uint32_t run_start_ = 0, run_end_ = 0;
  // VIDEO
  bool rec_ = false, rec_unknown_ = false;
  bool rec_left_ = false;  // left VIDEO (or slept elsewhere) while it may have been recording
  uint32_t rec_since_ = 0;
  uint16_t video_tag_ = 0;
  // card / editor / arm
  uint8_t card_sel_ = 0;
  bool card_from_af_ = false;
  Ed ed_ = Ed::Timer;
  int8_t ed_sel_ = -1;  // -1 = the stored value (may be a Mac-only value off the list)
  PlanOut plan_{};
  bool plan_relay_ = false;
  uint8_t plan_batt_ = 255;
  uint8_t refuse_kind_ = 0;  // 0 airtime, 1 battery, 2 relay
};
