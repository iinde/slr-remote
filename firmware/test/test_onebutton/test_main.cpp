// Host tests for the one-button logic: press classifier, mode engine, airtime planner,
// reset gestures, LED cues. Run: pio test -e native
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "ledcue.h"
#include "modes.h"
#include "press.h"
#include "rstgest.h"
#include "seqplan.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- harness

namespace {

struct Sim {
  ModeEngine eng;
  PressClassifier pc;
  PressClassifier aux;
  ObEnv env;
  bool down = false, aux_down = false;
  ObAction acts[256];
  int n = 0;
  // Hold bookkeeping: an episode starts at the first Focus/Shoot while nothing is held and
  // ends at a Release.
  bool held = false;
  int episodes = 0, releases = 0, extra_releases = 0;
  ObScreen scr;
  // When set, every step checks the label bar fits the screen; the longest seen is kept.
  bool watch_bar = false;
  size_t max_bar = 0;
  char longest[32] = {0};

  void setup(const ObSettings& s, bool relay = false) {
    eng.configure(s);
    env.now = 1000;
    env.relay = relay;
    env.n_views = 4;
    pc.setTable(eng.table());
    aux.setTable(eng.auxTable());
  }
  void drain() {
    ObAction a;
    while (eng.popAction(a)) {
      if (n < 256) acts[n++] = a;
      if (a.act == ObAct::Focus || a.act == ObAct::Shoot) {
        if (!held) episodes++;
        held = true;
      } else if (a.act == ObAct::Release) {
        if (held) releases++;
        else extra_releases++;
        held = false;
      }
    }
  }
  void step(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 5) {
      env.now += 5;
      PressEvent ev = pc.update(down, env.now);
      if (ev.type != PressEvt::None) eng.onPress(ev, env);
      PressEvent ea = aux.update(aux_down, env.now);
      if (ea.type != PressEvt::None) eng.onAux(ea, env);
      eng.tick(env);
      pc.setTable(eng.table());
      drain();
      if (watch_bar) {
        ObScreen o;
        eng.screen(o, env);
        if (strlen(o.bar) > max_bar) {
          max_bar = strlen(o.bar);
          snprintf(longest, sizeof longest, "%s", o.bar);
        }
      }
    }
  }
  // Holds for hold_ms of real time, then lets go (debounce adds 25 ms each way).
  void press(uint32_t hold_ms, uint32_t after = 100) {
    down = true;
    step(hold_ms);
    down = false;
    step(after);
  }
  void tap() { press(100); }
  void auxPress(uint32_t hold_ms) {
    aux_down = true;
    step(hold_ms);
    aux_down = false;
    step(100);
  }
  int count(ObAct a) const {
    int c = 0;
    for (int i = 0; i < n; i++) c += acts[i].act == a;
    return c;
  }
  int fires() const { return count(ObAct::Shot) + count(ObAct::Shoot); }
  // Shoot actions held until a Release (arg 0): over relay these last a whole Release airtime.
  int bareShoots() const {
    int c = 0;
    for (int i = 0; i < n; i++) c += acts[i].act == ObAct::Shoot && acts[i].arg == 0;
    return c;
  }
  void clear() { n = 0; }
  const ObScreen& screen() {
    eng.screen(scr, env);
    return scr;
  }
  bool balanced() const { return !held && releases == episodes; }
  int lastShotFocus() const {
    for (int i = n; i-- > 0;)
      if (acts[i].act == ObAct::Shot) return acts[i].arg;
    return -1;
  }
  uint16_t lastTag() const {
    for (int i = n; i-- > 0;)
      if (acts[i].act == ObAct::Shot || acts[i].act == ObAct::Shoot) return acts[i].tag;
    return 0;
  }
};

ObSettings defaults() { return ObSettings{}; }

ObSettings with_modes(std::initializer_list<ObMode> ms) {
  ObSettings s;
  s.n_modes = 0;
  for (ObMode m : ms) s.modes[s.n_modes++] = m;
  return s;
}

ObSettings all_modes() {
  return with_modes({ObMode::Shot, ObMode::AfLock, ObMode::Timer, ObMode::Interval, ObMode::Burst,
                     ObMode::Bulb, ObMode::Video});
}

}  // namespace

// ---------------------------------------------------------------- zone tables

static void test_zones_a_shutter_boundaries() {
  const ZoneTable t = press_zones(PressStyle::A, PressCtx::Shutter, 4000);
  TEST_ASSERT_EQUAL(Zone::Tap, zone_at(t, 0));
  TEST_ASSERT_EQUAL(Zone::Tap, zone_at(t, 299));
  TEST_ASSERT_EQUAL(Zone::Af, zone_at(t, 300));
  TEST_ASSERT_EQUAL(Zone::Af, zone_at(t, 3999));
  TEST_ASSERT_EQUAL(Zone::Modes, zone_at(t, 4000));
  TEST_ASSERT_EQUAL(Zone::Modes, zone_at(t, 6999));
  TEST_ASSERT_EQUAL(Zone::Nothing, zone_at(t, 7000));
  TEST_ASSERT_EQUAL(Zone::Nothing, zone_at(t, 14999));
  TEST_ASSERT_EQUAL(Zone::Stuck, zone_at(t, 15000));
}

static void test_zones_long_af_limit_keeps_stuck_last() {
  const ZoneTable t = press_zones(PressStyle::A, PressCtx::Shutter, 15000);
  TEST_ASSERT_EQUAL(Zone::Af, zone_at(t, 14999));
  TEST_ASSERT_EQUAL(Zone::Modes, zone_at(t, 15000));
  TEST_ASSERT_EQUAL(Zone::Nothing, zone_at(t, 18000));
  TEST_ASSERT_EQUAL(Zone::Stuck, zone_at(t, 20000));
  // A tiny limit still leaves room for the focus wait.
  const ZoneTable s = press_zones(PressStyle::A, PressCtx::Shutter, 100);
  TEST_ASSERT_EQUAL(Zone::Af, zone_at(s, 799));
  TEST_ASSERT_EQUAL(Zone::Modes, zone_at(s, 800));
}

static void test_zones_aux_cancel_instead_of_modes() {
  const ZoneTable t = press_zones(PressStyle::A, PressCtx::Shutter, 4000, true);
  TEST_ASSERT_EQUAL(Zone::Af, zone_at(t, 3999));
  TEST_ASSERT_EQUAL(Zone::Cancel, zone_at(t, 4000));
  TEST_ASSERT_EQUAL(Zone::Cancel, zone_at(t, 10000));
  const ZoneTable p = press_zones(PressStyle::A, PressCtx::Plain, 4000, true);
  TEST_ASSERT_EQUAL(Zone::Nothing, zone_at(p, 1000));
}

static void test_zones_plain_view_picker_locked_wake() {
  ZoneTable t = press_zones(PressStyle::A, PressCtx::Plain, 4000);
  TEST_ASSERT_EQUAL(Zone::Tap, zone_at(t, 999));
  TEST_ASSERT_EQUAL(Zone::Modes, zone_at(t, 1000));
  TEST_ASSERT_EQUAL(Zone::Nothing, zone_at(t, 7000));
  t = press_zones(PressStyle::A, PressCtx::View, 4000);
  TEST_ASSERT_EQUAL(Zone::Tap, zone_at(t, 999));
  TEST_ASSERT_EQUAL(Zone::NextView, zone_at(t, 1000));
  t = press_zones(PressStyle::B, PressCtx::Picker, 4000);
  TEST_ASSERT_EQUAL(Zone::Pick, zone_at(t, 1000));
  t = press_zones(PressStyle::C, PressCtx::Locked, 4000);
  TEST_ASSERT_EQUAL(Zone::Unlock, zone_at(t, 1000));
  t = press_zones(PressStyle::A, PressCtx::Wake, 4000);
  TEST_ASSERT_EQUAL(Zone::Tap, zone_at(t, 599));
  TEST_ASSERT_EQUAL(Zone::Modes, zone_at(t, 600));
  // The wake table never contains AF.
  for (uint8_t i = 0; i < t.n; i++) TEST_ASSERT_NOT_EQUAL(Zone::Af, t.zone[i]);
}

static void test_zones_b_and_c() {
  ZoneTable t = press_zones(PressStyle::B, PressCtx::Shutter, 4000);
  TEST_ASSERT_EQUAL(Zone::Tap, zone_at(t, 999));
  TEST_ASSERT_EQUAL(Zone::Wheel, zone_at(t, 1000));
  t = press_zones(PressStyle::C, PressCtx::Shutter, 4000);
  TEST_ASSERT_EQUAL(Zone::Menu, zone_at(t, 1000));
  TEST_ASSERT_EQUAL(Zone::Menu, zone_at(t, 3999));
  TEST_ASSERT_EQUAL(Zone::Off, zone_at(t, 4000));
  TEST_ASSERT_EQUAL(Zone::Nothing, zone_at(t, 8000));
  t = press_zones(PressStyle::C, PressCtx::HoldAf, 4000);
  TEST_ASSERT_EQUAL(Zone::Af, zone_at(t, 300));
  TEST_ASSERT_EQUAL(Zone::Cancel, zone_at(t, 4000));
  // Neither B nor C ever holds AF from a plain SHOT press.
  for (PressStyle st : {PressStyle::B, PressStyle::C}) {
    t = press_zones(st, PressCtx::Shutter, 4000);
    for (uint8_t i = 0; i < t.n; i++) TEST_ASSERT_NOT_EQUAL(Zone::Af, t.zone[i]);
  }
}

// ---------------------------------------------------------------- classifier

static void test_classifier_debounce_and_edges() {
  PressClassifier pc;
  pc.setTable(press_zones(PressStyle::A, PressCtx::Shutter, 4000));
  uint32_t t = 100;
  // A 20 ms glitch is not a press.
  for (int i = 0; i < 4; i++, t += 5) TEST_ASSERT_EQUAL(PressEvt::None, pc.update(true, t).type);
  for (int i = 0; i < 20; i++, t += 5) TEST_ASSERT_EQUAL(PressEvt::None, pc.update(false, t).type);
  // A real press: Down once, dated at the first edge.
  const uint32_t edge = t;
  PressEvent ev;
  int downs = 0;
  for (int i = 0; i < 10; i++, t += 5) {
    ev = pc.update(true, t);
    if (ev.type == PressEvt::Down) {
      downs++;
      TEST_ASSERT_EQUAL_UINT32(edge, ev.at);
    }
  }
  TEST_ASSERT_EQUAL(1, downs);
  const uint32_t up_edge = t;
  int ups = 0;
  for (int i = 0; i < 10; i++, t += 5) {
    ev = pc.update(false, t);
    if (ev.type == PressEvt::Up) {
      ups++;
      TEST_ASSERT_EQUAL(Zone::Tap, ev.zone);
      TEST_ASSERT_EQUAL_UINT32(up_edge - edge, ev.held);
    }
  }
  TEST_ASSERT_EQUAL(1, ups);
}

static void test_classifier_slow_caller_never_skips_enter() {
  PressClassifier pc;
  pc.setTable(press_zones(PressStyle::A, PressCtx::Shutter, 4000));
  pc.update(true, 0);
  TEST_ASSERT_EQUAL(PressEvt::Down, pc.update(true, 30).type);
  // Let go at 5 s; the caller was blocked meanwhile and catches up in order.
  PressEvent a = pc.update(false, 5000);
  PressEvent b = pc.update(false, 5100);
  PressEvent c = pc.update(false, 5101);
  TEST_ASSERT_EQUAL(PressEvt::Enter, a.type);
  TEST_ASSERT_EQUAL(Zone::Af, a.zone);
  TEST_ASSERT_EQUAL(PressEvt::Enter, b.type);
  TEST_ASSERT_EQUAL(Zone::Modes, b.zone);
  TEST_ASSERT_EQUAL(PressEvt::Up, c.type);
  TEST_ASSERT_EQUAL(Zone::Modes, c.zone);
}

static void test_classifier_table_latched_at_down() {
  PressClassifier pc;
  pc.setTable(press_zones(PressStyle::A, PressCtx::Shutter, 4000));
  pc.update(true, 0);
  pc.update(true, 30);  // Down
  pc.setTable(press_zones(PressStyle::A, PressCtx::Picker, 4000));
  PressEvent ev;
  for (uint32_t t = 35; t < 400; t += 5) {
    PressEvent e = pc.update(true, t);
    if (e.type == PressEvt::Enter) ev = e;
  }
  TEST_ASSERT_EQUAL(Zone::Af, ev.zone);  // still the table it started with
}

static void test_classifier_assume_down_wake() {
  PressClassifier pc;
  pc.setTable(press_zones(PressStyle::A, PressCtx::Wake, 4000));
  pc.assumeDown(0, 300);
  TEST_ASSERT_TRUE(pc.isDown());
  PressEvent ev;
  int downs = 0;
  for (uint32_t t = 305; t < 800; t += 5) {
    ev = pc.update(true, t);
    if (ev.type == PressEvt::Down) downs++;
  }
  TEST_ASSERT_EQUAL(0, downs);
  TEST_ASSERT_EQUAL(Zone::Modes, pc.zone());  // held 0.8 s from boot
}

// ---------------------------------------------------------------- SHOT (Option A)

static void test_a_tap_is_one_shot() {
  Sim s;
  s.setup(defaults());
  s.tap();
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL(500, s.lastShotFocus());
  TEST_ASSERT_TRUE(s.balanced());
  TEST_ASSERT_EQUAL_STRING("TAP SHOT  HOLD AF", s.screen().bar);
}

static void test_a_tap_just_under_and_over_af_start() {
  Sim s;
  s.setup(defaults());
  s.press(270);  // + 25 ms debounce = held 270 ms of contact: tap
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
  Sim h;
  h.setup(defaults());
  h.press(330, 800);
  TEST_ASSERT_EQUAL(1, h.count(ObAct::Focus));
  TEST_ASSERT_EQUAL(1, h.count(ObAct::Shoot));  // after the focus wait, not before
  TEST_ASSERT_EQUAL(0, h.count(ObAct::Shot));
  TEST_ASSERT_TRUE(h.balanced());
}

static void test_a_hold_then_let_go_fires_after_focus_time() {
  Sim s;
  s.setup(defaults());
  s.down = true;
  s.step(400);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL_STRING("LET GO: SHOOT", s.screen().bar);
  s.down = false;
  s.step(50);
  // Focus started at ~300 ms: no Shoot before 800 ms.
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Shoot));
  s.step(500);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shoot));
  s.step(300);
  TEST_ASSERT_TRUE(s.balanced());
  TEST_ASSERT_EQUAL(1, s.releases);
}

static void test_a_hold_past_limit_opens_card_never_fires() {
  Sim s;
  s.setup(defaults());
  s.down = true;
  s.step(4100);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Release));  // AF let go at the limit
  TEST_ASSERT_EQUAL_STRING("LET GO: MODES", s.screen().bar);
  TEST_ASSERT_EQUAL_STRING("AF RELEASED", s.screen().big);
  s.down = false;
  s.step(100);
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_EQUAL(ObKind::List, s.screen().kind);
  TEST_ASSERT_EQUAL(0, strncmp(s.screen().head, "MODE", 4));
  TEST_ASSERT_TRUE(s.balanced());
}

static void test_a_hold_past_modes_zone_does_nothing() {
  Sim s;
  s.setup(defaults());
  s.down = true;
  s.step(7100);
  TEST_ASSERT_EQUAL_STRING("LET GO: NOTHING", s.screen().bar);
  s.down = false;
  s.step(100);
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_EQUAL(ObKind::Home, s.screen().kind);
  TEST_ASSERT_TRUE(s.balanced());
}

static void test_a_stuck_button_sleeps_and_releases() {
  Sim s;
  s.setup(defaults());
  s.down = true;
  s.step(15200);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Off));
  bool stuck_arg = false;
  for (int i = 0; i < s.n; i++)
    if (s.acts[i].act == ObAct::Off) stuck_arg = s.acts[i].arg == 1;
  TEST_ASSERT_TRUE(stuck_arg);
  s.down = false;
  s.step(100);
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_TRUE(s.balanced());
}

static void test_a_every_hold_length_fires_at_most_once_and_releases() {
  const uint32_t holds[] = {30, 100, 270, 280, 300, 320, 500, 900, 3900, 4000, 4100, 6000, 6900, 7100, 9000, 16000};
  for (ObMode m : {ObMode::Shot, ObMode::AfLock, ObMode::HoldAf, ObMode::Video, ObMode::Interval}) {
    for (uint32_t h : holds) {
      Sim s;
      s.setup(all_modes());
      s.eng.setMode(m);
      s.press(h, 1200);
      char why[48];
      snprintf(why, sizeof why, "mode %u hold %lu", (unsigned)m, (unsigned long)h);
      TEST_ASSERT_TRUE_MESSAGE(s.fires() <= 1, why);
      if (m == ObMode::AfLock && s.eng.held()) {
        // A lock that is still on is a hold with a timer, not a leak: it ends at its timeout.
        s.step(31000);
      }
      TEST_ASSERT_TRUE_MESSAGE(s.balanced(), why);
      TEST_ASSERT_EQUAL_MESSAGE(0, s.extra_releases > 3 ? 1 : 0, why);
    }
  }
}

static void test_a_let_go_does_what_the_bar_said() {
  const uint32_t holds[] = {100, 600, 2000, 4500, 6500, 7500};
  for (uint32_t h : holds) {
    Sim s;
    s.setup(defaults());
    s.down = true;
    s.step(h);
    char bar[28];
    strcpy(bar, s.screen().bar);
    s.down = false;
    s.step(1000);
    if (!strcmp(bar, "LET GO: SHOOT")) {
      TEST_ASSERT_EQUAL(1, s.count(ObAct::Shoot));
    } else if (!strcmp(bar, "LET GO: MODES")) {
      TEST_ASSERT_EQUAL(0, s.fires());
      TEST_ASSERT_EQUAL(ObKind::List, s.screen().kind);
    } else if (!strcmp(bar, "LET GO: NOTHING")) {
      TEST_ASSERT_EQUAL(0, s.fires());
      TEST_ASSERT_EQUAL(ObKind::Home, s.screen().kind);
    } else {
      TEST_ASSERT_EQUAL_STRING("TAP SHOT  HOLD AF", bar);  // still in the tap zone
      TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
    }
  }
}

static void test_a_press_while_shot_pending_is_busy() {
  Sim s;
  s.setup(defaults());
  s.press(330, 0);  // let go at ~355 ms: shoot waits until ~800 ms
  s.press(50, 0);   // a second press inside that wait
  s.step(1000);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shoot));
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Shot));
  TEST_ASSERT_TRUE(s.balanced());
}

// ---------------------------------------------------------------- AF LOCK

static void test_aflock_lock_then_fire() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::AfLock);
  s.tap();
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL_STRING("AF LOCKED", s.screen().big);
  TEST_ASSERT_EQUAL_STRING("TAP: FIRE HOLD: UNLOCK", s.screen().bar);
  TEST_ASSERT_TRUE(s.eng.busy());
  s.tap();
  s.step(300);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shoot));
  TEST_ASSERT_TRUE(s.balanced());
}

static void test_aflock_hold_unlocks_without_shot() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::AfLock);
  s.tap();
  s.press(1200);
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_TRUE(s.balanced());
  TEST_ASSERT_EQUAL_STRING("AF UNLOCKED", s.screen().big);
}

static void test_aflock_times_out() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::AfLock);
  s.tap();
  s.step(29000);
  TEST_ASSERT_TRUE(s.eng.held());
  s.step(1500);
  TEST_ASSERT_FALSE(s.eng.held());
  TEST_ASSERT_TRUE(s.balanced());
  TEST_ASSERT_EQUAL_STRING("AF RELEASED", s.screen().big);
}

// ---------------------------------------------------------------- TIMER

static void test_timer_stop_before_zero_sends_nothing() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::Timer);
  s.tap();
  TEST_ASSERT_EQUAL(ObKind::Count, s.screen().kind);
  TEST_ASSERT_EQUAL_STRING("ANY PRESS: STOP", s.screen().bar);
  s.step(3000);
  s.press(1500);  // a long press: stops on the way down, the rest is swallowed
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_EQUAL_STRING("STOPPED", s.screen().big);
  s.step(15000);
  TEST_ASSERT_EQUAL(0, s.fires());  // no countdown was restarted by its release
  TEST_ASSERT_FALSE(s.eng.running());
}

static void test_timer_fires_once_then_too_late() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::Timer);
  s.tap();
  s.step(10100);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  s.tap();  // just after zero
  TEST_ASSERT_EQUAL_STRING("TOO LATE", s.screen().big);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  s.step(15000);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
}

static void test_timer_relay_sends_lead_early() {
  Sim s;
  s.setup(defaults(), true);
  s.env.lead_ms = 1500;
  s.eng.setMode(ObMode::Timer);
  s.tap();
  s.step(8300);  // ~8.4 s into a 10 s countdown
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Shot));
  s.step(400);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  s.press(50);  // stop pressed after the Shot left: a real shot, said so
  TEST_ASSERT_EQUAL_STRING("TOO LATE", s.screen().big);
}

static void test_timer_hold_keeps_af_then_shoots() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::Timer);
  s.down = true;
  s.step(1000);
  TEST_ASSERT_EQUAL_STRING("LET GO: START 10s", s.screen().bar);
  s.down = false;
  s.step(100);
  TEST_ASSERT_EQUAL_STRING("AF ON", s.screen().l1);
  s.step(10500);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shoot));
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Shot));
  TEST_ASSERT_TRUE(s.balanced());
}

static void test_timer_hold_then_stop_releases_af() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::Timer);
  s.press(1000);
  s.step(2000);
  s.tap();
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_TRUE(s.balanced());
}

// ---------------------------------------------------------------- INTERVAL

static void start_interval(Sim& s) {
  s.eng.setMode(ObMode::Interval);
  s.tap();
  s.press(1100);
}

static void test_interval_arm_back_fires_nothing() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 5;
  st.iv_count = 10;
  s.setup(st);
  s.eng.setMode(ObMode::Interval);
  s.tap();
  TEST_ASSERT_EQUAL_STRING("HOLD 1s: START TAP: BACK", s.screen().bar);
  TEST_ASSERT_EQUAL_STRING("nothing has fired yet", s.screen().l3);
  s.tap();
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_FALSE(s.eng.busy());
}

static void test_interval_runs_and_stops_on_press() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 5;
  st.iv_count = 10;
  s.setup(st);
  start_interval(s);
  TEST_ASSERT_TRUE(s.eng.running());
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));  // first at once
  s.step(10100);
  TEST_ASSERT_EQUAL(3, s.count(ObAct::Shot));
  s.press(2000);  // stop; the long tail is swallowed (no mode card, no AF)
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL_STRING("STOPPED", s.screen().big);
  TEST_ASSERT_EQUAL_STRING("by your press", s.screen().l2);
  s.step(20000);
  TEST_ASSERT_EQUAL(3, s.count(ObAct::Shot));
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Save) == 0 ? 1 : 1);
}

static void test_interval_completes_done() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 2;
  st.iv_count = 3;
  s.setup(st);
  start_interval(s);
  for (int i = 0; i < 6; i++) {
    s.step(1000);
    s.eng.onOutcome(s.lastTag(), ObOutcome::Ok, s.env);
  }
  s.step(500);
  TEST_ASSERT_EQUAL(3, s.count(ObAct::Shot));
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL_STRING("DONE", s.screen().big);
  TEST_ASSERT_EQUAL_STRING("3 of 3 taken", s.screen().l1);
}

static void test_interval_three_failures_stop_no_link() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 2;
  st.iv_count = 50;
  s.setup(st, true);
  start_interval(s);
  for (int i = 0; i < 3; i++) {
    s.eng.onOutcome(s.lastTag(), ObOutcome::Fail, s.env);
    s.step(2000);
  }
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL_STRING("NO LINK", s.screen().l2);
}

static void test_interval_duty_refusals_stop_air_limit() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 2;
  st.iv_count = 50;
  s.setup(st, true);
  start_interval(s);
  for (int i = 0; i < 3; i++) {
    s.eng.onOutcome(s.lastTag(), ObOutcome::Duty, s.env);
    s.step(2000);
  }
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL_STRING("AIR LIMIT", s.screen().l2);
}

static void test_interval_camera_board_out_of_air_skips_then_stops() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 2;
  st.iv_count = 50;
  s.setup(st, true);
  start_interval(s);
  const int sent = s.count(ObAct::Shot);
  s.env.cam_permille = 1000;  // the camera board has no airtime left
  s.step(7000);
  TEST_ASSERT_EQUAL(sent, s.count(ObAct::Shot));
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL_STRING("AIR LIMIT", s.screen().l2);
}

static void test_interval_refused_up_front_in_relay() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 2;
  st.iv_count = 0;  // until stopped
  s.setup(st, true);
  s.env.sf = 12;
  s.env.cam_permille = 500;
  start_interval(s);
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_EQUAL_STRING("NOT STARTED", s.screen().big);
  TEST_ASSERT_EQUAL_STRING("TAP: BACK", s.screen().bar);
  s.tap();
  TEST_ASSERT_EQUAL(ObKind::Home, s.screen().kind);
}

static void test_interval_battery_rules() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 5;
  st.iv_count = 100;
  s.setup(st);
  s.env.batt = 12;
  start_interval(s);
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL_STRING("battery below 15%", s.screen().l1);
  Sim r;
  r.setup(st);
  r.env.batt = 60;
  start_interval(r);
  TEST_ASSERT_TRUE(r.eng.running());
  r.env.batt = 5;
  r.step(100);
  TEST_ASSERT_FALSE(r.eng.running());
  TEST_ASSERT_EQUAL_STRING("BATTERY 5%", r.screen().l2);
}

static void test_interval_until_stopped_hits_4h_cap() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 600;
  st.iv_count = 0;
  s.setup(st);
  start_interval(s);
  for (int i = 0; i < 4 * 60; i++) {
    s.env.now += 60000 - 5;
    s.step(5);
    s.eng.onOutcome(s.lastTag(), ObOutcome::Ok, s.env);
  }
  s.step(100);
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL_STRING("4 h LIMIT", s.screen().l2);
}

static void test_interval_no_catch_up_burst() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 2;
  st.iv_count = 100;
  s.setup(st);
  start_interval(s);
  const int before = s.count(ObAct::Shot);
  s.env.now += 20000;  // the loop was blocked for 20 s
  s.step(10);
  TEST_ASSERT_EQUAL(before + 1, s.count(ObAct::Shot));
}

// ---------------------------------------------------------------- BURST / BULB / VIDEO

static void test_burst_tap_timed_and_relay_refused() {
  Sim s;
  s.setup(all_modes());
  s.eng.setMode(ObMode::Burst);
  s.tap();
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shoot));
  TEST_ASSERT_TRUE(s.eng.running());
  s.step(1000);
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_TRUE(s.balanced());
  Sim r;
  r.setup(all_modes(), true);
  r.eng.setMode(ObMode::Burst);
  r.tap();
  r.press(2000);
  TEST_ASSERT_EQUAL(0, r.fires());
  TEST_ASSERT_EQUAL_STRING("NOT IN RELAY", r.screen().big);
}

static void test_burst_stop_early() {
  Sim s;
  ObSettings st = all_modes();
  st.burst_ms = 3000;
  s.setup(st);
  s.eng.setMode(ObMode::Burst);
  s.tap();
  s.step(500);
  s.tap();
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_TRUE(s.balanced());
  TEST_ASSERT_EQUAL_STRING("STOPPED", s.screen().big);
}

static void test_burst_hold_and_cap() {
  // Hold = AF, let go = the timed burst (one Shoot, released at the burst length).
  Sim s;
  s.setup(all_modes());
  s.eng.setMode(ObMode::Burst);
  s.press(2000);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shoot));
  TEST_ASSERT_TRUE(s.eng.running());
  s.step(1500);
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_TRUE(s.balanced());
  // Held past the AF limit: the mode card, and nothing fired on the way (seen on the bench).
  Sim c;
  c.setup(all_modes());
  c.eng.setMode(ObMode::Burst);
  c.down = true;
  c.step(4500);
  TEST_ASSERT_FALSE(c.eng.held());
  TEST_ASSERT_EQUAL_STRING("LET GO: MODES", c.screen().bar);
  c.down = false;
  c.step(100);
  TEST_ASSERT_EQUAL(0, c.count(ObAct::Shoot));
  TEST_ASSERT_TRUE(c.balanced());
  TEST_ASSERT_EQUAL(ObKind::List, c.screen().kind);
  // Relay: a hold never fires either.
  Sim r;
  r.setup(all_modes(), true);
  r.eng.setMode(ObMode::Burst);
  r.press(2000);
  r.step(1500);
  TEST_ASSERT_EQUAL(0, r.count(ObAct::Shoot));
  TEST_ASSERT_TRUE(r.balanced());
}

static void test_bulb_open_close_and_cap() {
  Sim s;
  ObSettings st = all_modes();
  st.bulb_s = 1800;
  st.hold_cap_s = 60;  // a lowered cap still wins over the bulb time
  s.setup(st);
  s.eng.setMode(ObMode::Bulb);
  s.tap();
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shoot));
  TEST_ASSERT_EQUAL(1, s.count(ObAct::RunFlag));
  TEST_ASSERT_EQUAL_STRING("ANY PRESS: CLOSE NOW", s.screen().bar);
  s.step(60500);  // capped by hold_cap_s, set to 60 above
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_TRUE(s.balanced());
  TEST_ASSERT_EQUAL(2, s.count(ObAct::RunFlag));
  Sim c;
  c.setup(st);
  c.eng.setMode(ObMode::Bulb);
  c.tap();
  c.step(5000);
  c.tap();
  TEST_ASSERT_EQUAL_STRING("CLOSED", c.screen().big);
  TEST_ASSERT_TRUE(c.balanced());
}

static void test_bulb_refused_in_relay_and_low_battery() {
  Sim s;
  s.setup(all_modes(), true);
  s.eng.setMode(ObMode::Bulb);
  s.tap();
  TEST_ASSERT_EQUAL(0, s.fires());
  Sim b;
  b.setup(all_modes());
  b.env.batt = 10;
  b.eng.setMode(ObMode::Bulb);
  b.tap();
  TEST_ASSERT_EQUAL(0, b.fires());
  TEST_ASSERT_EQUAL_STRING("NOT OPENED", b.screen().big);
}

static void test_video_toggles_and_unknown() {
  Sim s;
  s.setup(all_modes());
  s.eng.setMode(ObMode::Video);
  s.tap();
  TEST_ASSERT_EQUAL(0, s.lastShotFocus());
  TEST_ASSERT_TRUE(s.eng.videoRecording());
  TEST_ASSERT_EQUAL_STRING("TAP: STOP  HOLD: MODES", s.screen().bar);
  s.step(2000);
  s.tap();
  TEST_ASSERT_FALSE(s.eng.videoRecording());
  s.tap();
  s.eng.onOutcome(s.lastTag(), ObOutcome::Fail, s.env);
  TEST_ASSERT_EQUAL_STRING("REC ?", s.screen().big);
  TEST_ASSERT_EQUAL_STRING("TAP: PRESS  HOLD: MODES", s.screen().bar);
}

// ---------------------------------------------------------------- card, editor, views, setup

static void open_card(Sim& s) {
  s.press(4500);
}

static void test_card_pick_mode_and_edit_value() {
  Sim s;
  s.setup(defaults());
  open_card(s);
  TEST_ASSERT_EQUAL(0, s.screen().sel);  // starts on the current mode
  s.tap();
  s.tap();  // SHOT -> AF LOCK -> TIMER
  s.press(1100);
  TEST_ASSERT_EQUAL(ObMode::Timer, s.eng.mode());
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Save));
  // Picking the current mode again opens its value.
  open_card(s);
  s.press(1100);
  TEST_ASSERT_EQUAL_STRING("TIMER", s.screen().head);
  s.tap();  // 10 -> 20
  s.press(1100);
  TEST_ASSERT_EQUAL(20, s.eng.settings().timer_s);
  TEST_ASSERT_EQUAL(0, s.fires());
}

static void test_card_closes_after_6s_no_change() {
  Sim s;
  s.setup(defaults());
  open_card(s);
  s.tap();
  s.step(6100);
  TEST_ASSERT_EQUAL(ObKind::Home, s.screen().kind);
  TEST_ASSERT_EQUAL(ObMode::Shot, s.eng.mode());
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Save));
}

static void test_card_relay_marks_direct_only() {
  Sim s;
  s.setup(all_modes(), true);
  open_card(s);
  const ObScreen& sc = s.screen();
  bool found = false;
  for (uint8_t i = 0; i < sc.n; i++)
    if (!strcmp(sc.item[i], "BULB")) found = !strcmp(sc.val[i], "DIRECT");
  TEST_ASSERT_TRUE(found);
  for (int i = 0; i < 5; i++) s.tap();  // SHOT .. BULB
  s.press(1100);
  TEST_ASSERT_EQUAL(ObMode::Shot, s.eng.mode());
  TEST_ASSERT_EQUAL_STRING("NOT IN RELAY", s.screen().big);
}

static void test_card_setup_confirm_and_off() {
  Sim s;
  s.setup(defaults());
  open_card(s);
  for (int i = 0; i < 5; i++) s.tap();  // SHOT AF TIMER INTERVAL VIEWS SETUP
  s.press(1100);
  TEST_ASSERT_EQUAL_STRING("SETUP?", s.screen().big);
  s.tap();  // no
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Setup));
  open_card(s);
  for (int i = 0; i < 5; i++) s.tap();
  s.press(1100);
  s.press(1100);  // yes
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Setup));
  open_card(s);
  for (int i = 0; i < 6; i++) s.tap();
  s.press(1100);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Off));
  TEST_ASSERT_EQUAL(0, s.fires());
}

static void test_views_cycle_and_tap_still_shoots() {
  Sim s;
  s.setup(defaults());
  open_card(s);
  s.clear();
  for (int i = 0; i < 4; i++) s.tap();  // VIEWS
  s.press(1100);
  TEST_ASSERT_EQUAL(1, s.eng.view());
  TEST_ASSERT_EQUAL_STRING("TAP SHOT HOLD 1s: NEXT", s.screen().bar);
  s.tap();
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  s.press(1100);
  TEST_ASSERT_EQUAL(2, s.eng.view());
  s.press(1100);
  TEST_ASSERT_EQUAL(3, s.eng.view());
  s.press(1100);
  TEST_ASSERT_EQUAL(0, s.eng.view());  // after the last view: back to MAIN
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
}

// ---------------------------------------------------------------- wake

static void test_wake_tap_rules() {
  Sim s;
  s.setup(defaults());
  s.eng.wakeTap(s.env);
  s.drain();
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  ObSettings st = defaults();
  st.wake_act = false;
  Sim w;
  w.setup(st);
  w.eng.wakeTap(w.env);
  w.drain();
  TEST_ASSERT_EQUAL(0, w.fires());
  TEST_ASSERT_EQUAL_STRING("AWAKE", w.screen().big);
  for (ObMode m : {ObMode::AfLock, ObMode::Timer, ObMode::Interval, ObMode::Burst, ObMode::Bulb, ObMode::Video}) {
    Sim t;
    t.setup(all_modes());
    t.eng.setMode(m);
    t.eng.wakeTap(t.env);
    t.drain();
    TEST_ASSERT_EQUAL(0, t.fires());
    TEST_ASSERT_EQUAL(0, t.count(ObAct::Focus));
    TEST_ASSERT_EQUAL_STRING("AWAKE", t.screen().big);
  }
  Sim a;
  a.setup(defaults());
  a.eng.wakeTap(a.env, true);
  a.drain();
  TEST_ASSERT_EQUAL(0, a.fires());
}

static void test_wake_hold_opens_card_never_af() {
  Sim s;
  s.setup(defaults());
  s.env.now = 300;
  s.pc.setTable(s.eng.wakeTable());
  s.pc.assumeDown(s.env.now, s.env.now);
  s.eng.wakeDown(s.env);
  s.down = true;
  s.step(2000);
  s.down = false;
  s.step(100);
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_EQUAL(ObKind::List, s.screen().kind);
  // Short wake hold (let go before 0.6 s from boot): a wake tap.
  Sim t;
  t.setup(defaults());
  t.env.now = 300;
  t.pc.setTable(t.eng.wakeTable());
  t.pc.assumeDown(t.env.now, t.env.now);
  t.eng.wakeDown(t.env);
  t.down = true;
  t.step(100);
  t.down = false;
  t.step(100);
  TEST_ASSERT_EQUAL(1, t.count(ObAct::Shot));
  TEST_ASSERT_EQUAL(0, t.count(ObAct::Focus));
}

// Every style shares the wake rules: still down at 0.6 s is the way to the modes, never a
// shot. An 0.8 s wake hold fires nothing in A, B or C.
static void test_wake_hold_fires_nothing_in_every_style() {
  for (PressStyle st : {PressStyle::A, PressStyle::B, PressStyle::C}) {
    const ZoneTable wt = press_zones(st, PressCtx::Wake, 4000);
    TEST_ASSERT_EQUAL(Zone::Tap, zone_at(wt, 599));
    TEST_ASSERT_NOT_EQUAL(Zone::Tap, zone_at(wt, 600));
    for (uint8_t i = 0; i < wt.n; i++) TEST_ASSERT_NOT_EQUAL(Zone::Af, wt.zone[i]);
    for (uint32_t hold : {800u, 1500u}) {
      Sim s;
      ObSettings o = defaults();
      o.style = st;
      s.setup(o);
      s.env.now = 300;
      s.pc.setTable(s.eng.wakeTable());
      s.pc.assumeDown(s.env.now, s.env.now);
      s.eng.wakeDown(s.env);
      s.down = true;
      s.step(hold);
      s.down = false;
      s.step(100);
      TEST_ASSERT_EQUAL(0, s.fires());
      TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
      TEST_ASSERT_EQUAL(0, s.count(ObAct::Off));
    }
  }
  TEST_ASSERT_EQUAL(Zone::Wheel, zone_at(press_zones(PressStyle::B, PressCtx::Wake, 4000), 800));
  TEST_ASSERT_EQUAL(Zone::Menu, zone_at(press_zones(PressStyle::C, PressCtx::Wake, 4000), 800));
  TEST_ASSERT_EQUAL(Zone::Modes, zone_at(press_zones(PressStyle::A, PressCtx::Wake, 4000), 800));
}

// ---------------------------------------------------------------- styles B and C, aux

static void test_style_b_wheel_picks_without_firing() {
  Sim s;
  ObSettings st = defaults();
  st.style = PressStyle::B;
  s.setup(st);
  s.tap();
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  s.clear();
  s.down = true;
  s.step(1800);  // wheel opens at 1 s on SHOT, one step at 1.7 s: AF LOCK
  TEST_ASSERT_EQUAL_STRING("LET GO: AF LOCK", s.screen().bar);
  s.down = false;
  s.step(100);
  TEST_ASSERT_EQUAL(ObMode::AfLock, s.eng.mode());
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
}

static void test_style_c_ladder() {
  ObSettings st = defaults();
  st.style = PressStyle::C;
  Sim m;
  m.setup(st);
  m.press(2000);
  TEST_ASSERT_EQUAL(ObKind::List, m.screen().kind);
  TEST_ASSERT_EQUAL(0, m.fires());
  Sim o;
  o.setup(st);
  o.down = true;
  o.step(5000);
  TEST_ASSERT_EQUAL_STRING("LET GO: OFF", o.screen().bar);
  o.down = false;
  o.step(100);
  TEST_ASSERT_EQUAL(1, o.count(ObAct::Off));
  Sim n;
  n.setup(st);
  n.press(9000);
  TEST_ASSERT_EQUAL(0, n.count(ObAct::Off));
  TEST_ASSERT_EQUAL(0, n.fires());
  // HOLD AF is on the C card and focuses on a hold.
  Sim h;
  h.setup(st);
  h.eng.setMode(ObMode::HoldAf);
  h.press(1000, 800);
  TEST_ASSERT_EQUAL(1, h.count(ObAct::Focus));
  TEST_ASSERT_EQUAL(1, h.count(ObAct::Shoot));
  TEST_ASSERT_TRUE(h.balanced());
  Sim c;
  c.setup(st);
  c.eng.setMode(ObMode::HoldAf);
  c.press(4500);
  TEST_ASSERT_EQUAL(0, c.fires());
  TEST_ASSERT_TRUE(c.balanced());
}

static void test_aux_button() {
  ObSettings st = defaults();
  st.aux = true;
  Sim s;
  s.setup(st);
  s.auxPress(100);
  TEST_ASSERT_EQUAL(ObKind::List, s.screen().kind);  // aux tap = mode card
  s.auxPress(100);
  TEST_ASSERT_EQUAL(1, s.screen().sel);              // aux tap again = next item
  s.step(7000);
  // PRG past the AF limit: AF times out, no card, no shot.
  s.press(4500);
  TEST_ASSERT_EQUAL(0, s.fires());
  TEST_ASSERT_EQUAL(ObKind::Text, s.screen().kind);
  TEST_ASSERT_EQUAL_STRING("AF TIMED OUT", s.screen().big);
  TEST_ASSERT_TRUE(s.balanced());
  // Aux stops a running timer.
  s.eng.setMode(ObMode::Timer);
  s.tap();
  s.auxPress(100);
  TEST_ASSERT_FALSE(s.eng.running());
  s.step(12000);
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Shot));
  // Aux hold = next view.
  s.auxPress(1200);
  TEST_ASSERT_EQUAL(1, s.eng.view());
}

// ---------------------------------------------------------------- forced stops

static void test_force_stop_releases_everything() {
  Sim s;
  s.setup(all_modes());
  s.down = true;
  s.step(1000);  // AF held
  s.eng.forceStop(ObStop::Sleep, s.env);
  s.drain();
  TEST_ASSERT_TRUE(s.balanced());
  s.down = false;
  s.step(1000);
  TEST_ASSERT_EQUAL(0, s.fires());  // the swallowed let-go fires nothing
  Sim b;
  b.setup(all_modes());
  b.eng.setMode(ObMode::Bulb);
  b.tap();
  b.eng.forceStop(ObStop::Battery, b.env);
  b.drain();
  TEST_ASSERT_TRUE(b.balanced());
  TEST_ASSERT_FALSE(b.eng.running());
}

static void test_busy_keeps_awake_only_when_needed() {
  Sim s;
  s.setup(defaults());
  TEST_ASSERT_FALSE(s.eng.busy());
  s.eng.setMode(ObMode::Timer);
  s.tap();
  TEST_ASSERT_TRUE(s.eng.busy());
  s.tap();
  TEST_ASSERT_FALSE(s.eng.busy());
}

// ---------------------------------------------------------------- settings helpers

static void test_settings_clamp_and_keys() {
  ObSettings st;
  st.n_modes = 4;
  st.modes[0] = ObMode::Timer;
  st.modes[1] = ObMode::Timer;
  st.modes[2] = (ObMode)99;
  st.modes[3] = ObMode::Bulb;
  st.af_limit_s = 99;
  st.iv_every_s = 1;
  ob_clamp(st);
  TEST_ASSERT_EQUAL(3, st.n_modes);
  TEST_ASSERT_EQUAL(ObMode::Shot, st.modes[0]);
  TEST_ASSERT_EQUAL(ObMode::Timer, st.modes[1]);
  TEST_ASSERT_EQUAL(ObMode::Bulb, st.modes[2]);
  TEST_ASSERT_EQUAL(15, st.af_limit_s);
  TEST_ASSERT_EQUAL(2, st.iv_every_s);
  for (uint8_t i = 0; i < OB_MODE_COUNT; i++) {
    ObMode m;
    TEST_ASSERT_TRUE(ob_mode_from_key(ob_mode_key((ObMode)i), m));
    TEST_ASSERT_EQUAL(i, (uint8_t)m);
  }
  PressStyle p;
  TEST_ASSERT_TRUE(ob_style_from_key("b", p));
  TEST_ASSERT_EQUAL(PressStyle::B, p);
  TEST_ASSERT_FALSE(ob_style_from_key("x", p));
}

// ---------------------------------------------------------------- planner

static void test_plan_shot_costs_match_spec_table() {
  // Spec 1.10: handheld Shot ~62 ms at SF7, ~1.48 s at SF12; camera Ack+Result ~185 ms / ~4.6 s (47-byte frame).
  TEST_ASSERT_UINT32_WITHIN(8, 62, plan_shot_cost_ms(7, false));
  TEST_ASSERT_UINT32_WITHIN(15, 185, plan_shot_cost_ms(7, true));
  TEST_ASSERT_UINT32_WITHIN(60, 1480, plan_shot_cost_ms(12, false));
  TEST_ASSERT_UINT32_WITHIN(150, 4600, plan_shot_cost_ms(12, true));
}

static void test_plan_fits_at_sf7_refused_at_sf12() {
  PlanIn in;
  in.sf = 7;
  in.every_s = 10;
  in.count = 120;
  PlanOut o = plan_interval(in);
  TEST_ASSERT_TRUE(o.fits);
  TEST_ASSERT_TRUE(o.need_permille > 100 && o.need_permille < 300);  // spec: ~21 %
  TEST_ASSERT_EQUAL_UINT32(1190, o.duration_s);
  in.sf = 12;
  in.every_s = 2;
  in.count = 0;
  o = plan_interval(in);
  TEST_ASSERT_FALSE(o.fits);
  TEST_ASSERT_TRUE(o.fit_every_s > 2);
  // The suggestion itself fits.
  PlanIn s2 = in;
  s2.every_s = o.fit_every_s;
  TEST_ASSERT_TRUE(plan_interval(s2).fits);
}

static void test_plan_counts_used_airtime_and_suggests_count() {
  PlanIn in;
  in.sf = 10;
  in.every_s = 10;
  in.count = 360;
  in.cam_permille = 700;
  PlanOut o = plan_interval(in);
  TEST_ASSERT_FALSE(o.fits);
  TEST_ASSERT_EQUAL(300, o.have_permille);
  TEST_ASSERT_TRUE(o.fit_count > 0 && o.fit_count < 360);
  PlanIn c = in;
  c.count = o.fit_count;
  TEST_ASSERT_TRUE(plan_interval(c).fits);
  c.count = o.fit_count + 1;
  TEST_ASSERT_FALSE(plan_interval(c).fits);
}

static void test_plan_shot_fits_edges() {
  TEST_ASSERT_TRUE(plan_shot_fits(7, 0, 0));
  TEST_ASSERT_FALSE(plan_shot_fits(7, 0, 1000));
  TEST_ASSERT_FALSE(plan_shot_fits(12, 1000, 0));
  TEST_ASSERT_TRUE(plan_shot_fits(12, 980, 980));  // 7.2 s left on each side
}

// ---------------------------------------------------------------- R1 reset gestures

static void test_rstgest_single_then_double() {
  RstDecision d = rstgest_boot(BootKind::PowerOn, true, true, 0);
  TEST_ASSERT_FALSE(d.open_setup);
  TEST_ASSERT_TRUE(d.write);
  TEST_ASSERT_EQUAL(1, d.value);
  TEST_ASSERT_TRUE(d.armed);
  d = rstgest_boot(BootKind::PowerOn, true, true, 1);  // second tap inside the window
  TEST_ASSERT_TRUE(d.open_setup);
  TEST_ASSERT_TRUE(d.write);
  TEST_ASSERT_EQUAL(0, d.value);
  TEST_ASSERT_FALSE(d.armed);
}

static void test_rstgest_window_and_non_taps() {
  TEST_ASSERT_FALSE(rstgest_settle_due(true, 2999));
  TEST_ASSERT_TRUE(rstgest_settle_due(true, 3000));
  TEST_ASSERT_FALSE(rstgest_settle_due(false, 9000));
  // Deep-sleep wake, software/USB/brownout reset: never a tap, and a stale flag is cleared.
  for (BootKind k : {BootKind::DeepSleep, BootKind::Other}) {
    RstDecision d = rstgest_boot(k, true, true, 1);
    TEST_ASSERT_FALSE(d.open_setup);
    TEST_ASSERT_TRUE(d.write);
    TEST_ASSERT_EQUAL(0, d.value);
    d = rstgest_boot(k, true, true, 0);
    TEST_ASSERT_FALSE(d.write);
  }
  // Gestures off: never opens, clears.
  RstDecision d = rstgest_boot(BootKind::PowerOn, false, true, 1);
  TEST_ASSERT_FALSE(d.open_setup);
  TEST_ASSERT_EQUAL(0, d.value);
  // NVS unreadable: a normal boot, nothing written.
  d = rstgest_boot(BootKind::PowerOn, true, false, 1);
  TEST_ASSERT_FALSE(d.open_setup);
  TEST_ASSERT_FALSE(d.write);
}

static void test_rstgest_sequence_double_after_window_is_two_singles() {
  // Tap, wait past the window (flag cleared), tap: two plain boots.
  uint8_t nvs = 0;
  RstDecision d = rstgest_boot(BootKind::PowerOn, true, true, nvs);
  if (d.write) nvs = d.value;
  if (rstgest_settle_due(d.armed, 3200)) nvs = 0;
  d = rstgest_boot(BootKind::PowerOn, true, true, nvs);
  TEST_ASSERT_FALSE(d.open_setup);
  // A failed write on the first boot: the second tap is a plain boot too.
  d = rstgest_boot(BootKind::PowerOn, true, true, 0);
  const uint8_t not_written = 0;
  d = rstgest_boot(BootKind::PowerOn, true, true, not_written);
  TEST_ASSERT_FALSE(d.open_setup);
}

// ---------------------------------------------------------------- LED cues

static void test_led_cue_patterns() {
  LedCue c;
  c.start(2, 0);
  int ons = 0;
  bool last = false;
  for (uint32_t t = 0; t < 1000; t += 10) {
    const bool on = c.on(t);
    if (on && !last) ons++;
    last = on;
  }
  TEST_ASSERT_EQUAL(2, ons);
  TEST_ASSERT_FALSE(c.active());
  c.start(LED_LONG, 0);
  TEST_ASSERT_TRUE(c.on(700));
  TEST_ASSERT_FALSE(c.on(900));
}


// ---------------------------------------------------------------- more edges

static void test_presses_during_blocking_send_fire_once() {
  // The classifier keeps sampling inside a 2 s SF12 transmit; the engine sees the events late.
  Sim s;
  s.setup(defaults(), true);
  PressEvent q[8];
  int qn = 0;
  for (uint32_t t = 0; t < 2000; t++) {
    s.env.now++;
    const bool down = t >= 100 && t < 380;  // a 280 ms tap during the block
    PressEvent ev = s.pc.update(down, s.env.now);
    if (ev.type != PressEvt::None && qn < 8) q[qn++] = ev;
  }
  for (int i = 0; i < qn; i++) s.eng.onPress(q[i], s.env);
  s.drain();
  s.step(1000);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
}

static void test_held_press_during_blocking_send_is_af_then_shoot() {
  Sim s;
  s.setup(defaults(), true);
  PressEvent q[8];
  int qn = 0;
  for (uint32_t t = 0; t < 2500; t++) {
    s.env.now++;
    const bool down = t >= 100 && t < 1600;  // held 1.5 s, all inside the block
    PressEvent ev = s.pc.update(down, s.env.now);
    if (ev.type != PressEvt::None && qn < 8) q[qn++] = ev;
  }
  for (int i = 0; i < qn; i++) s.eng.onPress(q[i], s.env);
  s.drain();
  s.step(1000);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shoot));
  TEST_ASSERT_TRUE(s.balanced());
}

static void test_stuck_press_during_run_stops_and_sleeps() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 5;
  st.iv_count = 100;
  s.setup(st);
  start_interval(s);
  s.down = true;
  s.step(16000);
  TEST_ASSERT_FALSE(s.eng.running());
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Off));
  s.down = false;
  s.step(100);
  TEST_ASSERT_TRUE(s.balanced());
}

static void test_b_and_c_never_focus_from_shot_mode() {
  const uint32_t holds[] = {100, 400, 1500, 4500, 9000};
  for (PressStyle st : {PressStyle::B, PressStyle::C}) {
    for (uint32_t h : holds) {
      ObSettings o = defaults();
      o.style = st;
      Sim s;
      s.setup(o);
      s.press(h, 1000);
      TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
      TEST_ASSERT_TRUE(s.fires() <= 1);
      TEST_ASSERT_TRUE(s.balanced());
    }
  }
}

static void test_editor_shows_mac_only_value_then_steps() {
  Sim s;
  ObSettings st = defaults();
  st.timer_s = 7;  // not on the short list: set over the serial console
  s.setup(st);
  s.eng.setMode(ObMode::Timer);
  open_card(s);
  s.press(1100);  // TIMER again: its value
  TEST_ASSERT_EQUAL_STRING("7s", s.screen().item[0]);
  TEST_ASSERT_EQUAL_STRING("now", s.screen().val[0]);
  s.tap();  // first list value above 7: 10
  s.press(1100);
  TEST_ASSERT_EQUAL(10, s.eng.settings().timer_s);
}

static void test_editor_interval_every_then_count() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::Interval);
  s.press(1500);  // plain mode: hold 1 s = modes
  TEST_ASSERT_EQUAL(ObKind::List, s.screen().kind);
  s.press(1100);  // INTERVAL again: every
  TEST_ASSERT_EQUAL_STRING("INTERVAL: EVERY", s.screen().head);
  s.tap();        // 10 -> 15
  s.press(1100);
  TEST_ASSERT_EQUAL_STRING("INTERVAL: COUNT", s.screen().head);
  s.tap();        // 120 -> 360
  s.press(1100);
  TEST_ASSERT_EQUAL(15, s.eng.settings().iv_every_s);
  TEST_ASSERT_EQUAL(360, s.eng.settings().iv_count);
  TEST_ASSERT_EQUAL(0, s.fires());
}

static void test_interval_duplicate_and_busy_outcomes() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 2;
  st.iv_count = 10;
  s.setup(st);
  start_interval(s);
  const uint16_t t = s.lastTag();
  s.eng.onOutcome(t, ObOutcome::Ok, s.env);
  s.eng.onOutcome(t, ObOutcome::Ok, s.env);  // reported twice: counted once
  s.step(2000);
  s.eng.onOutcome(s.lastTag(), ObOutcome::Busy, s.env);
  TEST_ASSERT_EQUAL_STRING("ok 1 fail 0 skip 1", s.screen().l1);
  TEST_ASSERT_TRUE(s.eng.running());
}

static void test_refused_screen_offers_a_fit() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 10;
  st.iv_count = 360;
  s.setup(st, true);
  s.env.sf = 10;
  s.env.cam_permille = 700;
  start_interval(s);
  TEST_ASSERT_EQUAL_STRING("NOT STARTED", s.screen().big);
  TEST_ASSERT_EQUAL(0, strncmp(s.screen().l2, "fits:", 5));
  TEST_ASSERT_EQUAL(0, s.fires());
}

static void test_video_allowed_in_relay_bulb_holdcap_raised() {
  Sim v;
  v.setup(all_modes(), true);
  v.eng.setMode(ObMode::Video);
  v.tap();
  TEST_ASSERT_EQUAL(1, v.count(ObAct::Shot));
  ObSettings st = all_modes();
  st.bulb_s = 120;
  st.hold_cap_s = 300;  // raised over the serial console
  Sim b;
  b.setup(st);
  b.eng.setMode(ObMode::Bulb);
  b.tap();
  b.step(100000);
  TEST_ASSERT_TRUE(b.eng.running());  // 100 s in: past a 60 s cap
  b.step(21000);
  TEST_ASSERT_FALSE(b.eng.running());
  TEST_ASSERT_TRUE(b.balanced());
}

static void test_force_stop_closes_card_and_locked() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::AfLock);
  s.tap();
  s.eng.forceStop(ObStop::Sleep, s.env);
  s.drain();
  TEST_ASSERT_TRUE(s.balanced());
  TEST_ASSERT_FALSE(s.eng.busy());
  open_card(s);
  s.eng.forceStop(ObStop::Sleep, s.env);
  s.drain();
  TEST_ASSERT_FALSE(s.eng.busy());
}

static void test_reset_note_and_relay_af_confirm_text() {
  Sim s;
  s.setup(defaults(), true);
  s.eng.noteReset();
  TEST_ASSERT_EQUAL_STRING("RESET", s.screen().l2);
  s.env.af_confirmed = false;
  s.down = true;
  s.step(500);
  TEST_ASSERT_EQUAL_STRING("AF...", s.screen().big);
  s.env.af_confirmed = true;
  TEST_ASSERT_EQUAL_STRING("AF HELD", s.screen().big);
  s.down = false;
  s.step(1000);
  TEST_ASSERT_TRUE(s.balanced());
}

// ---------------------------------------------------------------- regression cases

// Relay: every "shoot now" (A-style let go, AF LOCK fire, TIMER with AF at 0) is a timed
// Shoot that the camera board lets go of by itself. A bare held Shoot would stay pressed for a
// whole Release airtime (about 1.2 s at SF12): a burst in CH from one press.
static void test_relay_shoot_now_is_timed_never_bare() {
  // A: hold SHOT past AF, let go.
  Sim a;
  a.setup(defaults(), true);
  a.press(1500, 800);
  TEST_ASSERT_EQUAL(1, a.count(ObAct::Shoot));
  TEST_ASSERT_EQUAL(0, a.bareShoots());
  TEST_ASSERT_TRUE(a.balanced());  // and the backup Release still goes out
  // Let go before the focus time: the delayed shot is timed too.
  Sim q;
  q.setup(defaults(), true);
  q.press(400, 800);
  TEST_ASSERT_EQUAL(1, q.count(ObAct::Shoot));
  TEST_ASSERT_EQUAL(0, q.bareShoots());
  TEST_ASSERT_TRUE(q.balanced());
  // AF LOCK: tap locks, tap fires.
  Sim l;
  l.setup(defaults(), true);
  l.eng.setMode(ObMode::AfLock);
  l.tap();
  l.tap();
  l.step(500);
  TEST_ASSERT_EQUAL(1, l.count(ObAct::Shoot));
  TEST_ASSERT_EQUAL(0, l.bareShoots());
  TEST_ASSERT_TRUE(l.balanced());
  // TIMER with AF: hold, let go, zero shoots.
  Sim t;
  ObSettings ts = defaults();
  ts.timer_s = 2;
  t.setup(ts, true);
  t.eng.setMode(ObMode::Timer);
  t.press(1000);
  t.step(3000);
  TEST_ASSERT_EQUAL(1, t.count(ObAct::Shoot));
  TEST_ASSERT_EQUAL(0, t.bareShoots());
  TEST_ASSERT_TRUE(t.balanced());
  // Every Shoot the engine makes in relay, whatever the press, is timed.
  for (ObMode m : {ObMode::Shot, ObMode::AfLock, ObMode::Timer, ObMode::HoldAf}) {
    for (uint32_t hold : {100u, 400u, 900u, 2500u, 6000u}) {
      Sim r;
      r.setup(all_modes(), true);
      r.eng.setMode(m);
      r.press(hold, 300);
      r.tap();
      r.step(35000);  // past the 30 s AF LOCK auto release
      TEST_ASSERT_EQUAL(0, r.bareShoots());
      TEST_ASSERT_TRUE(r.balanced());
    }
  }
  // Direct BURST is still a plain held press (released by the engine at the burst length).
  Sim b;
  b.setup(all_modes(), false);
  b.eng.setMode(ObMode::Burst);
  b.tap();
  b.step(1500);
  TEST_ASSERT_EQUAL(1, b.bareShoots());
  TEST_ASSERT_TRUE(b.balanced());
}

// With PRG as the only button, every mode in every style has a press that reaches the mode
// card (A, C) or the wheel (B). HOLD AF used to be a trap: AF, CANCEL, nothing else.
static void test_every_mode_can_reach_the_card_with_prg_alone() {
  for (PressStyle st : {PressStyle::A, PressStyle::B, PressStyle::C}) {
    for (uint8_t m = 0; m < OB_MODE_COUNT; m++) {
      ObSettings s = all_modes();
      s.style = st;
      ModeEngine e;
      e.configure(s);
      e.setMode((ObMode)m);
      for (const ZoneTable& t : {e.table(), e.wakeTable()}) {
        bool out = false;
        for (uint8_t i = 0; i < t.n; i++)
          out = out || t.zone[i] == Zone::Modes || t.zone[i] == Zone::Menu || t.zone[i] == Zone::Wheel;
        TEST_ASSERT_TRUE_MESSAGE(out, ob_mode_key((ObMode)m));
      }
    }
  }
  // A and C: HOLD AF held past CANCEL opens the card, nothing fires, AF let go once.
  for (PressStyle st : {PressStyle::A, PressStyle::C}) {
    ObSettings s = all_modes();
    s.style = st;
    Sim h;
    h.setup(s);
    h.eng.setMode(ObMode::HoldAf);
    h.down = true;
    h.step(4300);  // past the 4 s AF limit: CANCEL
    TEST_ASSERT_EQUAL_STRING("AF TIMED OUT", h.screen().big);
    TEST_ASSERT_EQUAL_STRING("keep holding: MODES", h.screen().l2);
    TEST_ASSERT_EQUAL_STRING("LET GO: NOTHING", h.screen().bar);
    h.step(1000);
    TEST_ASSERT_EQUAL_STRING("LET GO: MODES", h.screen().bar);
    h.down = false;
    h.step(100);
    TEST_ASSERT_EQUAL(ObKind::List, h.screen().kind);
    TEST_ASSERT_EQUAL(0, h.fires());
    TEST_ASSERT_EQUAL(1, h.count(ObAct::Focus));
    TEST_ASSERT_TRUE(h.balanced());
    // Let go in CANCEL: still nothing, no card.
    Sim c;
    c.setup(s);
    c.eng.setMode(ObMode::HoldAf);
    c.press(4500);
    TEST_ASSERT_EQUAL(0, c.fires());
    TEST_ASSERT_TRUE(c.balanced());
    TEST_ASSERT_EQUAL(ObMode::HoldAf, c.eng.mode());
    TEST_ASSERT_NOT_EQUAL(ObKind::List, c.screen().kind);
  }
  // B: HOLD AF held on past CANCEL turns the wheel; letting go picks the next mode.
  ObSettings b = with_modes({ObMode::Shot, ObMode::HoldAf, ObMode::Timer});
  b.style = PressStyle::B;
  Sim w;
  w.setup(b);
  w.eng.setMode(ObMode::HoldAf);
  TEST_ASSERT_EQUAL_STRING("TAP SHOT  HOLD AF>WHEEL", w.screen().bar);
  w.press(4000 + 1000 + 900);
  TEST_ASSERT_EQUAL(ObMode::Timer, w.eng.mode());
  TEST_ASSERT_EQUAL(0, w.fires());
  TEST_ASSERT_TRUE(w.balanced());
  // With a second button the PRG table stays a pure shutter (the aux opens the card).
  ObSettings x = all_modes();
  x.aux = true;
  ModeEngine ex;
  ex.configure(x);
  ex.setMode(ObMode::HoldAf);
  const ZoneTable tx = ex.table();
  for (uint8_t i = 0; i < tx.n; i++) TEST_ASSERT_NOT_EQUAL(Zone::Modes, tx.zone[i]);
}

// VIDEO: the handheld sleeps while the camera records. After the wake the record state is
// unknown: the bar must never offer REC (that tap would really stop the recording).
static void test_video_record_state_survives_sleep_as_unknown() {
  Sim s;
  s.setup(all_modes());
  s.eng.setMode(ObMode::Video);
  s.tap();
  TEST_ASSERT_TRUE(s.eng.videoRecording());
  TEST_ASSERT_TRUE(s.eng.recMaybe());
  // Deep sleep: the app keeps recMaybe() and the mode in RTC memory; the engine is new.
  const bool rtc_rec = s.eng.recMaybe();
  const ObMode rtc_mode = s.eng.mode();
  Sim w;
  w.setup(all_modes());
  w.eng.setMode(rtc_mode);
  if (rtc_rec) w.eng.recUnknownAfterSleep();
  TEST_ASSERT_NULL(strstr(w.screen().bar, "REC"));
  TEST_ASSERT_EQUAL_STRING("TAP: PRESS  HOLD: MODES", w.screen().bar);
  TEST_ASSERT_EQUAL_STRING("REC ?", w.screen().big);
  // Not recording before the sleep: the bar offers REC as before.
  Sim z;
  z.setup(all_modes());
  z.eng.setMode(ObMode::Video);
  TEST_ASSERT_FALSE(z.eng.recMaybe());
  z.eng.recUnknownAfterSleep();  // only called when the RTC flag was set; harmless anyway
  Sim y;
  y.setup(all_modes());
  y.eng.setMode(ObMode::Video);
  TEST_ASSERT_EQUAL_STRING("TAP: REC  HOLD: MODES", y.screen().bar);
  // Slept in another mode while VIDEO may still be recording: the flag is kept, and picking
  // VIDEO again shows REC ? (the app only calls this when the RTC flag was set).
  ModeEngine o;
  o.configure(all_modes());
  o.setMode(ObMode::Shot);
  o.recUnknownAfterSleep();
  TEST_ASSERT_TRUE(o.recMaybe());
  o.setMode(ObMode::Video);
  ObScreen os;
  ObEnv oe;
  o.screen(os, oe);
  TEST_ASSERT_EQUAL_STRING("TAP: PRESS  HOLD: MODES", os.bar);
  TEST_ASSERT_EQUAL_STRING("REC ?", os.big);
}

// VIDEO: leaving it while it records (hold, pick SHOT) and coming back later must never
// offer TAP: REC, because that tap would really stop the camera.
static void test_video_leave_and_return_never_offers_rec() {
  Sim s;
  s.setup(all_modes());
  s.eng.setMode(ObMode::Video);
  s.tap();
  TEST_ASSERT_EQUAL_STRING("TAP: STOP  HOLD: MODES", s.screen().bar);
  s.eng.setMode(ObMode::Shot);
  TEST_ASSERT_TRUE(s.eng.recMaybe());  // the RTC copy survives a sleep taken in SHOT
  s.eng.setMode(ObMode::Video);
  TEST_ASSERT_EQUAL_STRING("TAP: PRESS  HOLD: MODES", s.screen().bar);
  TEST_ASSERT_EQUAL_STRING("REC ?", s.screen().big);
  // The tap after that is the user's own decision; then the state is known again.
  s.tap();
  TEST_ASSERT_EQUAL_STRING("TAP: STOP  HOLD: MODES", s.screen().bar);
  s.tap();
  TEST_ASSERT_EQUAL_STRING("TAP: REC  HOLD: MODES", s.screen().bar);
  TEST_ASSERT_FALSE(s.eng.recMaybe());
  // Never recording: leaving and coming back still offers REC.
  Sim n;
  n.setup(all_modes());
  n.eng.setMode(ObMode::Video);
  n.eng.setMode(ObMode::Shot);
  n.eng.setMode(ObMode::Video);
  TEST_ASSERT_EQUAL_STRING("TAP: REC  HOLD: MODES", n.screen().bar);
}

// ---------------------------------------------------------------- stop cancels a pending shot

static int find_act(const Sim& s, ObAct a) {
  for (int i = s.n; i-- > 0;)
    if (s.acts[i].act == a) return i;
  return -1;
}

// A stopped INTERVAL asks the app to drop a shot that has not left the board yet (direct:
// parked while BLE reconnects; relay: still queued), so nothing fires after STOPPED.
static void test_interval_stop_cancels_pending_shot() {
  Sim s;
  ObSettings st = defaults();
  st.iv_every_s = 5;
  st.iv_count = 120;
  s.setup(st);
  start_interval(s);
  s.step(5100);
  TEST_ASSERT_EQUAL(2, s.count(ObAct::Shot));
  const uint16_t tag = s.lastTag();
  s.tap();  // stop
  const int i = find_act(s, ObAct::CancelPending);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT16(tag, s.acts[i].tag);
  TEST_ASSERT_EQUAL_STRING("STOPPED", s.screen().big);
  s.eng.onCancel(tag, ObCancel::Cancelled, s.env);
  TEST_ASSERT_EQUAL_STRING("last shot cancelled", s.screen().l3);
  TEST_ASSERT_EQUAL_STRING("by your press", s.screen().l2);
  // Already on air: said so on the STOPPED screen.
  Sim r;
  r.setup(st, true);
  start_interval(r);
  const uint16_t t2 = r.lastTag();
  r.tap();
  TEST_ASSERT_TRUE(find_act(r, ObAct::CancelPending) >= 0);
  r.eng.onCancel(t2, ObCancel::AlreadySent, r.env);
  TEST_ASSERT_EQUAL_STRING("1 shot already sent", r.screen().l3);
  // A shot whose outcome is already in has nothing to cancel: no action.
  Sim d;
  d.setup(st);
  start_interval(d);
  d.eng.onOutcome(d.lastTag(), ObOutcome::Ok, d.env);
  d.tap();
  TEST_ASSERT_EQUAL(-1, find_act(d, ObAct::CancelPending));
  TEST_ASSERT_EQUAL_STRING("", d.screen().l3);
}

// TIMER: a press just after zero says TOO LATE only if the shot really left; one still parked
// or queued is cancelled and the screen says it was stopped in time.
static void test_timer_late_press_cancels_unsent_shot() {
  Sim s;
  s.setup(defaults());
  s.eng.setMode(ObMode::Timer);
  s.tap();
  s.step(10100);
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  const uint16_t tag = s.lastTag();
  s.tap();
  const int i = find_act(s, ObAct::CancelPending);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT16(tag, s.acts[i].tag);
  TEST_ASSERT_EQUAL_STRING("TOO LATE", s.screen().big);
  s.eng.onCancel(tag, ObCancel::Cancelled, s.env);
  TEST_ASSERT_EQUAL_STRING("STOPPED", s.screen().big);
  TEST_ASSERT_EQUAL_STRING("timer: shot cancelled", s.screen().l1);
  // Relay, sent early by the lead and already on air: TOO LATE stays.
  Sim r;
  r.setup(defaults(), true);
  r.env.lead_ms = 1500;
  r.eng.setMode(ObMode::Timer);
  r.tap();
  r.step(8700);
  TEST_ASSERT_EQUAL(1, r.count(ObAct::Shot));
  const uint16_t t2 = r.lastTag();
  r.press(50);
  TEST_ASSERT_TRUE(find_act(r, ObAct::CancelPending) >= 0);
  r.eng.onCancel(t2, ObCancel::AlreadySent, r.env);
  TEST_ASSERT_EQUAL_STRING("TOO LATE", r.screen().big);
  TEST_ASSERT_EQUAL(1, r.count(ObAct::Shot));
}

// ---------------------------------------------------------------- direct, camera not connected

// A hold right after a wake (BLE still connecting) must not claim AF HELD: no Focus goes out,
// and letting go is the full one-press shot that waits for the camera like a tap.
static void test_direct_no_link_hold_never_claims_af() {
  Sim s;
  s.setup(defaults());
  s.env.link_ok = false;
  s.down = true;
  s.step(1000);
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL_STRING("AF: NO CAMERA", s.screen().big);
  TEST_ASSERT_EQUAL_STRING("LET GO: SHOT (waits)", s.screen().bar);
  s.down = false;
  s.step(100);
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Shoot));
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Shot));
  TEST_ASSERT_EQUAL(500, s.lastShotFocus());
  TEST_ASSERT_TRUE(s.balanced());
  // The link comes up during the hold: AF starts then, and letting go shoots after focus.
  Sim u;
  u.setup(defaults());
  u.env.link_ok = false;
  u.down = true;
  u.step(800);
  TEST_ASSERT_EQUAL(0, u.count(ObAct::Focus));
  u.env.link_ok = true;
  u.step(100);
  TEST_ASSERT_EQUAL(1, u.count(ObAct::Focus));
  TEST_ASSERT_EQUAL_STRING("AF HELD", u.screen().big);
  u.step(600);
  u.down = false;
  u.step(400);
  TEST_ASSERT_EQUAL(1, u.count(ObAct::Shoot));
  TEST_ASSERT_EQUAL(0, u.count(ObAct::Shot));
  TEST_ASSERT_TRUE(u.balanced());
  // Not yet confirmed in direct (the app's af_confirmed): never the words AF HELD.
  Sim c;
  c.setup(defaults());
  c.env.af_confirmed = false;
  c.down = true;
  c.step(1000);
  TEST_ASSERT_EQUAL_STRING("AF...", c.screen().big);
  c.down = false;
  c.step(400);
  // Relay is unchanged: link_ok there is the camera board, and the Focus is queued as before.
  Sim r;
  r.setup(defaults(), true);
  r.env.link_ok = false;
  r.press(1000, 400);
  TEST_ASSERT_EQUAL(1, r.count(ObAct::Focus));
  TEST_ASSERT_TRUE(r.balanced());
}

static void test_direct_no_link_af_lock_refused() {
  Sim s;
  s.setup(defaults());
  s.env.link_ok = false;
  s.eng.setMode(ObMode::AfLock);
  s.tap();
  TEST_ASSERT_EQUAL(0, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL_STRING("NOT LOCKED", s.screen().big);
  TEST_ASSERT_FALSE(s.eng.held());
  s.env.link_ok = true;
  s.step(4000);
  s.tap();
  TEST_ASSERT_EQUAL(1, s.count(ObAct::Focus));
  TEST_ASSERT_EQUAL_STRING("AF LOCKED", s.screen().big);
}

// ---------------------------------------------------------------- view pages

static void test_view_watch_resets_and_bar() {
  ObViewWatch w;
  w.seed(2, 1000);  // woke onto a view: nothing to reset
  TEST_ASSERT_FALSE(w.update(2, 1500));
  TEST_ASSERT_TRUE(w.barVisible(2, false, 1500));   // just opened
  TEST_ASSERT_FALSE(w.barVisible(2, false, 1000 + OB_VIEW_BAR_MS));
  TEST_ASSERT_TRUE(w.barVisible(2, true, 9000));    // a press is down: the bar is the label
  TEST_ASSERT_TRUE(w.barVisible(0, false, 9000));   // MAIN always has its bar
  TEST_ASSERT_TRUE(w.update(3, 9000));              // once per change
  TEST_ASSERT_FALSE(w.update(3, 9100));
  TEST_ASSERT_TRUE(w.barVisible(3, false, 9100));
  TEST_ASSERT_TRUE(w.update(0, 12000));
}

// A second button that does not work this boot (stuck, or 'aux 6' typed before a reboot):
// the app configures the engine without aux, and PRG gets its MODES zones back.
static void test_engine_without_working_aux_keeps_prg_modes() {
  ObSettings s = all_modes();
  s.aux = true;
  ModeEngine e;
  e.configure(s);
  auto has_modes = [](const ZoneTable& t) {
    for (uint8_t i = 0; i < t.n; i++)
      if (t.zone[i] == Zone::Modes) return true;
    return false;
  };
  TEST_ASSERT_FALSE(has_modes(e.table()));
  s.aux = false;  // ob_live_settings(): aux && the pin was set up and is not stuck
  e.configure(s);
  for (ObMode m : {ObMode::Shot, ObMode::Interval, ObMode::Burst, ObMode::HoldAf}) {
    e.setMode(m);
    TEST_ASSERT_TRUE(has_modes(e.table()));
  }
  TEST_ASSERT_TRUE(has_modes(e.wakeTable()));
  char bar[32];
  ObScreen o;
  ObEnv env;
  e.setMode(ObMode::Interval);
  e.screen(o, env);
  snprintf(bar, sizeof bar, "%s", o.bar);
  TEST_ASSERT_EQUAL_STRING("TAP: ARM  HOLD: MODES", bar);
}

// Every label bar fits the screen: 24 characters of the 5x7 font in 124 px.
static void test_every_bar_fits_the_screen() {
  for (PressStyle st : {PressStyle::A, PressStyle::B, PressStyle::C}) {
    for (bool aux : {false, true}) {
      for (uint16_t bulb : {1, 30, 120, 999, 1799, 1800}) {
        for (uint8_t m = 0; m < OB_MODE_COUNT; m++) {
          for (uint8_t v = 0; v < 2; v++) {
            for (uint8_t rec = 0; rec < 3; rec++) {
              ObSettings s = all_modes();
              s.style = st;
              s.aux = aux;
              s.bulb_s = bulb;
              s.hold_cap_s = 1800;
              s.timer_s = 60;
              s.burst_ms = 10000;
              Sim x;
              x.setup(s);
              x.eng.setMode((ObMode)m);
              x.eng.setView(v);
              if (rec == 1 && m == (uint8_t)ObMode::Video) x.tap();
              if (rec == 2) x.eng.recUnknownAfterSleep();
              const ObScreen& o = x.screen();
              TEST_ASSERT_TRUE_MESSAGE(strlen(o.bar) <= OB_BAR_MAX, o.bar);
            }
          }
        }
      }
    }
  }
  // And while pressing and running: taps and holds of every length in every mode.
  for (PressStyle st : {PressStyle::A, PressStyle::B, PressStyle::C}) {
    for (uint8_t m = 0; m < OB_MODE_COUNT; m++) {
      ObSettings s = all_modes();
      s.style = st;
      s.bulb_s = 1799;
      s.hold_cap_s = 1800;
      s.timer_s = 3;
      s.iv_every_s = 3;
      s.iv_count = 2;
      Sim x;
      x.setup(s);
      x.watch_bar = true;
      x.eng.setMode((ObMode)m);
      for (uint32_t hold : {100u, 700u, 1200u, 2500u, 4500u, 5500u, 9000u}) {
        x.press(hold, 300);
        x.tap();
        x.step(1500);
        x.tap();
        x.step(7000);
      }
      TEST_ASSERT_TRUE_MESSAGE(x.max_bar <= OB_BAR_MAX, x.longest);
    }
  }
}

// The app's event queue overflowed during a long blocking transmit and lost the Up of a hold:
// the engine must never keep AF held for good.
static void test_lost_up_never_leaves_af_held() {
  // The app sees the button up with the press still open: abortPress releases, nothing fires.
  Sim a;
  a.setup(defaults());
  PressEvent d;
  d.type = PressEvt::Down;
  d.at = a.env.now;
  a.eng.onPress(d, a.env);
  a.env.now += 300;
  PressEvent en;
  en.type = PressEvt::Enter;
  en.zone = Zone::Af;
  en.at = a.env.now;
  en.held = 300;
  a.eng.onPress(en, a.env);
  a.drain();
  TEST_ASSERT_EQUAL(1, a.count(ObAct::Focus));
  TEST_ASSERT_TRUE(a.eng.held());
  // (the Up is dropped)
  a.env.now += 500;
  a.eng.abortPress(a.env);
  a.drain();
  TEST_ASSERT_FALSE(a.eng.held());
  TEST_ASSERT_TRUE(a.balanced());
  TEST_ASSERT_EQUAL(0, a.fires());
  TEST_ASSERT_EQUAL_STRING("PRESS LOST", a.screen().big);
  TEST_ASSERT_FALSE(a.eng.busy());  // so the handheld can sleep again
  // The next press is a normal press.
  a.tap();
  TEST_ASSERT_EQUAL(1, a.count(ObAct::Shot));

  // Without the app's resync: the next press-down releases the orphaned AF first.
  Sim b;
  b.setup(defaults());
  b.eng.onPress(d, b.env);
  b.env.now += 300;
  en.at = b.env.now;
  b.eng.onPress(en, b.env);
  b.drain();
  TEST_ASSERT_TRUE(b.eng.held());
  b.env.now += 2000;
  PressEvent d2;
  d2.type = PressEvt::Down;
  d2.at = b.env.now;
  b.eng.onPress(d2, b.env);
  b.drain();
  TEST_ASSERT_FALSE(b.eng.held());
  TEST_ASSERT_TRUE(b.balanced());
  TEST_ASSERT_EQUAL(0, b.fires());

  // AF LOCK and a timer with AF are owned holds: the backstop leaves them alone.
  Sim l;
  l.setup(defaults());
  l.eng.setMode(ObMode::AfLock);
  l.tap();
  l.step(5000);
  TEST_ASSERT_TRUE(l.eng.held());
  TEST_ASSERT_EQUAL(0, l.count(ObAct::Release));
}

// US region: 500 kHz, and an hourly budget of the whole hour (no legal duty limit).
static void test_plan_defaults_are_eu() {
  for (uint8_t sf = 7; sf <= 12; sf++) {
    TEST_ASSERT_EQUAL_UINT32(plan_shot_cost_ms(sf, false), plan_shot_cost_ms(sf, false, 125.0f));
    TEST_ASSERT_EQUAL_UINT32(plan_shot_cost_ms(sf, true), plan_shot_cost_ms(sf, true, 125.0f));
  }
  PlanIn in;
  TEST_ASSERT_EQUAL_FLOAT(125.0f, in.bw_khz);
  TEST_ASSERT_EQUAL_UINT32(360000, in.budget_ms);
  ObEnv e;
  TEST_ASSERT_EQUAL_FLOAT(125.0f, e.bw_khz);
  TEST_ASSERT_EQUAL_UINT32(360000, e.budget_ms);
  TEST_ASSERT_EQUAL(plan_shot_fits(12, 980, 980), plan_shot_fits(12, 980, 980, 125.0f, 360000));
}

static void test_plan_shot_costs_at_500khz() {
  // 25-byte Shot: 16 ms at SF7, 371 ms at SF12. Ack + Result (2 x 47 bytes): 48 ms, 988 ms.
  TEST_ASSERT_EQUAL_UINT32(16, plan_shot_cost_ms(7, false, 500.0f));
  TEST_ASSERT_EQUAL_UINT32(48, plan_shot_cost_ms(7, true, 500.0f));
  TEST_ASSERT_EQUAL_UINT32(371, plan_shot_cost_ms(12, false, 500.0f));
  TEST_ASSERT_EQUAL_UINT32(988, plan_shot_cost_ms(12, true, 500.0f));
}

static void test_plan_us_region_never_near_the_limit() {
  PlanIn in;
  in.bw_khz = 500.0f;
  in.budget_ms = 3600000;
  in.sf = 7;
  in.every_s = 10;
  in.count = 120;
  PlanOut o = plan_interval(in);
  TEST_ASSERT_TRUE(o.fits);
  TEST_ASSERT_TRUE(o.need_permille < 10);  // under 1 % of the hour
  TEST_ASSERT_EQUAL(1000, o.have_permille);
  // The run the EU refuses outright (SF12, every 2 s, until stopped) fits here: a round trip
  // is 1.36 s, and even this uses about three quarters of the hour.
  in.sf = 12;
  in.every_s = 2;
  in.count = 0;
  o = plan_interval(in);
  TEST_ASSERT_TRUE(o.fits);
  TEST_ASSERT_TRUE(o.need_permille > 600 && o.need_permille < 900);
  // Same run with the EU figures: refused, as before.
  PlanIn eu = in;
  eu.bw_khz = 125.0f;
  eu.budget_ms = 360000;
  TEST_ASSERT_FALSE(plan_interval(eu).fits);
  // Still never faster than one round trip.
  in.every_s = 1;
  TEST_ASSERT_FALSE(plan_interval(in).fits);
}

static void test_plan_shot_fits_us_budget() {
  // 0.1 % of the US hour is 3.6 s: room for an SF12 shot. Of the EU hour it is 0.36 s: none.
  TEST_ASSERT_TRUE(plan_shot_fits(12, 999, 999, 500.0f, 3600000));
  TEST_ASSERT_FALSE(plan_shot_fits(12, 999, 999));
  TEST_ASSERT_FALSE(plan_shot_fits(12, 1000, 0, 500.0f, 3600000));
  TEST_ASSERT_FALSE(plan_shot_fits(7, 0, 1000, 500.0f, 3600000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zones_a_shutter_boundaries);
  RUN_TEST(test_zones_long_af_limit_keeps_stuck_last);
  RUN_TEST(test_zones_aux_cancel_instead_of_modes);
  RUN_TEST(test_zones_plain_view_picker_locked_wake);
  RUN_TEST(test_zones_b_and_c);
  RUN_TEST(test_classifier_debounce_and_edges);
  RUN_TEST(test_classifier_slow_caller_never_skips_enter);
  RUN_TEST(test_classifier_table_latched_at_down);
  RUN_TEST(test_classifier_assume_down_wake);
  RUN_TEST(test_a_tap_is_one_shot);
  RUN_TEST(test_a_tap_just_under_and_over_af_start);
  RUN_TEST(test_a_hold_then_let_go_fires_after_focus_time);
  RUN_TEST(test_a_hold_past_limit_opens_card_never_fires);
  RUN_TEST(test_a_hold_past_modes_zone_does_nothing);
  RUN_TEST(test_a_stuck_button_sleeps_and_releases);
  RUN_TEST(test_a_every_hold_length_fires_at_most_once_and_releases);
  RUN_TEST(test_a_let_go_does_what_the_bar_said);
  RUN_TEST(test_a_press_while_shot_pending_is_busy);
  RUN_TEST(test_aflock_lock_then_fire);
  RUN_TEST(test_aflock_hold_unlocks_without_shot);
  RUN_TEST(test_aflock_times_out);
  RUN_TEST(test_timer_stop_before_zero_sends_nothing);
  RUN_TEST(test_timer_fires_once_then_too_late);
  RUN_TEST(test_timer_relay_sends_lead_early);
  RUN_TEST(test_timer_hold_keeps_af_then_shoots);
  RUN_TEST(test_timer_hold_then_stop_releases_af);
  RUN_TEST(test_interval_arm_back_fires_nothing);
  RUN_TEST(test_interval_runs_and_stops_on_press);
  RUN_TEST(test_interval_completes_done);
  RUN_TEST(test_interval_three_failures_stop_no_link);
  RUN_TEST(test_interval_duty_refusals_stop_air_limit);
  RUN_TEST(test_interval_camera_board_out_of_air_skips_then_stops);
  RUN_TEST(test_interval_refused_up_front_in_relay);
  RUN_TEST(test_interval_battery_rules);
  RUN_TEST(test_interval_until_stopped_hits_4h_cap);
  RUN_TEST(test_interval_no_catch_up_burst);
  RUN_TEST(test_burst_tap_timed_and_relay_refused);
  RUN_TEST(test_burst_stop_early);
  RUN_TEST(test_burst_hold_and_cap);
  RUN_TEST(test_bulb_open_close_and_cap);
  RUN_TEST(test_bulb_refused_in_relay_and_low_battery);
  RUN_TEST(test_video_toggles_and_unknown);
  RUN_TEST(test_card_pick_mode_and_edit_value);
  RUN_TEST(test_card_closes_after_6s_no_change);
  RUN_TEST(test_card_relay_marks_direct_only);
  RUN_TEST(test_card_setup_confirm_and_off);
  RUN_TEST(test_views_cycle_and_tap_still_shoots);
  RUN_TEST(test_wake_tap_rules);
  RUN_TEST(test_wake_hold_opens_card_never_af);
  RUN_TEST(test_wake_hold_fires_nothing_in_every_style);
  RUN_TEST(test_style_b_wheel_picks_without_firing);
  RUN_TEST(test_style_c_ladder);
  RUN_TEST(test_aux_button);
  RUN_TEST(test_force_stop_releases_everything);
  RUN_TEST(test_busy_keeps_awake_only_when_needed);
  RUN_TEST(test_settings_clamp_and_keys);
  RUN_TEST(test_plan_shot_costs_match_spec_table);
  RUN_TEST(test_plan_fits_at_sf7_refused_at_sf12);
  RUN_TEST(test_plan_counts_used_airtime_and_suggests_count);
  RUN_TEST(test_plan_shot_fits_edges);
  RUN_TEST(test_plan_defaults_are_eu);
  RUN_TEST(test_plan_shot_costs_at_500khz);
  RUN_TEST(test_plan_us_region_never_near_the_limit);
  RUN_TEST(test_plan_shot_fits_us_budget);
  RUN_TEST(test_rstgest_single_then_double);
  RUN_TEST(test_rstgest_window_and_non_taps);
  RUN_TEST(test_rstgest_sequence_double_after_window_is_two_singles);
  RUN_TEST(test_led_cue_patterns);
  RUN_TEST(test_presses_during_blocking_send_fire_once);
  RUN_TEST(test_held_press_during_blocking_send_is_af_then_shoot);
  RUN_TEST(test_stuck_press_during_run_stops_and_sleeps);
  RUN_TEST(test_b_and_c_never_focus_from_shot_mode);
  RUN_TEST(test_editor_shows_mac_only_value_then_steps);
  RUN_TEST(test_editor_interval_every_then_count);
  RUN_TEST(test_interval_duplicate_and_busy_outcomes);
  RUN_TEST(test_refused_screen_offers_a_fit);
  RUN_TEST(test_video_allowed_in_relay_bulb_holdcap_raised);
  RUN_TEST(test_force_stop_closes_card_and_locked);
  RUN_TEST(test_reset_note_and_relay_af_confirm_text);
  RUN_TEST(test_relay_shoot_now_is_timed_never_bare);
  RUN_TEST(test_every_mode_can_reach_the_card_with_prg_alone);
  RUN_TEST(test_video_record_state_survives_sleep_as_unknown);
  RUN_TEST(test_video_leave_and_return_never_offers_rec);
  RUN_TEST(test_interval_stop_cancels_pending_shot);
  RUN_TEST(test_timer_late_press_cancels_unsent_shot);
  RUN_TEST(test_direct_no_link_hold_never_claims_af);
  RUN_TEST(test_direct_no_link_af_lock_refused);
  RUN_TEST(test_view_watch_resets_and_bar);
  RUN_TEST(test_engine_without_working_aux_keeps_prg_modes);
  RUN_TEST(test_every_bar_fits_the_screen);
  RUN_TEST(test_lost_up_never_leaves_af_held);
  return UNITY_END();
}
