#include "modes.h"

#include <stdio.h>
#include <string.h>

#include "ledcue.h"

namespace {

bool due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }

template <typename T>
T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// "30s", "2m", "1h"; minutes only when whole.
void fmt_dur(char* b, size_t cap, uint32_t s) {
  if (s >= 3600 && s % 3600 == 0) snprintf(b, cap, "%luh", (unsigned long)(s / 3600));
  else if (s >= 60 && s % 60 == 0) snprintf(b, cap, "%lum", (unsigned long)(s / 60));
  else snprintf(b, cap, "%lus", (unsigned long)s);
}

// "m:ss" (or "h:mm:ss")
void fmt_clock(char* b, size_t cap, uint32_t s) {
  if (s >= 3600)
    snprintf(b, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
             (unsigned long)(s % 60));
  else snprintf(b, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

void fmt_tenths(char* b, size_t cap, uint32_t ms) {
  snprintf(b, cap, "%lu.%lus", (unsigned long)(ms / 1000), (unsigned long)(ms / 100 % 10));
}

void copy(char* dst, size_t cap, const char* s) {
  snprintf(dst, cap, "%s", s ? s : "");
}

const char* const kModeName[] = {"SHOT", "AF LOCK", "TIMER", "INTERVAL", "BURST", "BULB", "VIDEO", "HOLD AF"};
const char* const kModeKey[] = {"shot", "aflock", "timer", "interval", "burst", "bulb", "video", "holdaf"};

bool direct_only(ObMode m) { return m == ObMode::Burst || m == ObMode::Bulb; }

}  // namespace

const char* ob_mode_name(ObMode m) {
  return (uint8_t)m < OB_MODE_COUNT ? kModeName[(uint8_t)m] : "?";
}
const char* ob_mode_key(ObMode m) { return (uint8_t)m < OB_MODE_COUNT ? kModeKey[(uint8_t)m] : "?"; }
bool ob_mode_from_key(const char* k, ObMode& out) {
  for (uint8_t i = 0; i < OB_MODE_COUNT; i++)
    if (k && !strcmp(k, kModeKey[i])) {
      out = (ObMode)i;
      return true;
    }
  return false;
}

const char* ob_style_key(PressStyle s) {
  switch (s) {
    case PressStyle::A: return "a";
    case PressStyle::B: return "b";
    case PressStyle::C: return "c";
    default: return "classic";
  }
}
bool ob_style_from_key(const char* k, PressStyle& out) {
  if (!k) return false;
  if (!strcmp(k, "classic")) out = PressStyle::Classic;
  else if (!strcmp(k, "a")) out = PressStyle::A;
  else if (!strcmp(k, "b")) out = PressStyle::B;
  else if (!strcmp(k, "c")) out = PressStyle::C;
  else return false;
  return true;
}

const char* ob_stop_text(ObStop r) {
  switch (r) {
    case ObStop::ByPress: return "by your press";
    case ObStop::Done: return "";
    case ObStop::AirLimit: return "AIR LIMIT";
    case ObStop::NoLink: return "NO LINK";
    case ObStop::NoCamera: return "NO CAMERA";
    case ObStop::Battery: return "BATTERY 5%";
    case ObStop::HoldCap: return "HOLD CAP";
    case ObStop::TimeLimit: return "4 h LIMIT";
    case ObStop::Reset: return "RESET";
    case ObStop::Sleep: return "SLEEP";
  }
  return "";
}

void ob_clamp(ObSettings& s) {
  if ((uint8_t)s.style > (uint8_t)PressStyle::C) s.style = PressStyle::A;
  s.af_limit_s = clampv<uint8_t>(s.af_limit_s, 2, 15);
  s.aflock_s = clampv<uint8_t>(s.aflock_s, 5, 120);
  s.timer_s = clampv<uint8_t>(s.timer_s, 1, 60);
  s.iv_every_s = clampv<uint16_t>(s.iv_every_s, 2, 3600);
  if (s.iv_count > 9999) s.iv_count = 9999;
  s.burst_ms = clampv<uint16_t>(s.burst_ms, 200, 10000);
  s.bulb_s = clampv<uint16_t>(s.bulb_s, 1, 1800);
  s.hold_cap_s = clampv<uint16_t>(s.hold_cap_s, 1, 1800);
  if (s.focus_ms > 5000) s.focus_ms = 5000;
  // Modes: known, no repeats, SHOT first and always present.
  ObMode out[OB_MAX_MODES];
  uint8_t n = 0;
  out[n++] = ObMode::Shot;
  for (uint8_t i = 0; i < s.n_modes && i < OB_MAX_MODES; i++) {
    const ObMode m = s.modes[i];
    if ((uint8_t)m >= OB_MODE_COUNT) continue;
    bool seen = false;
    for (uint8_t k = 0; k < n; k++) seen = seen || out[k] == m;
    if (!seen && n < OB_MAX_MODES) out[n++] = m;
  }
  s.n_modes = n;
  for (uint8_t i = 0; i < OB_MAX_MODES; i++) s.modes[i] = i < n ? out[i] : ObMode::Shot;
}

// ---------------------------------------------------------------- setup

void ModeEngine::configure(const ObSettings& s) {
  s_ = s;
  ob_clamp(s_);
}

void ModeEngine::setMode(ObMode m) {
  if ((uint8_t)m >= OB_MODE_COUNT) m = ObMode::Shot;
  if (m != mode_) {
    // Leaving VIDEO while it may be recording: the camera keeps recording. Remember that, so
    // coming back says REC ? and the tap is a plain PRESS, never a REC that would stop it.
    if (mode_ == ObMode::Video && (rec_ || rec_unknown_)) rec_left_ = true;
    rec_ = rec_unknown_ = false;
    if (m == ObMode::Video && rec_left_) {
      rec_unknown_ = true;
      rec_left_ = false;
    }
  }
  mode_ = m;
}

void ModeEngine::recUnknownAfterSleep() {
  rec_ = false;
  if (mode_ == ObMode::Video) rec_unknown_ = true;
  else rec_left_ = true;  // slept in another mode: REC ? once VIDEO is picked again
}

void ModeEngine::push(ObAct a, uint16_t arg, uint16_t tag) {
  if (qn_ == sizeof(q_) / sizeof(q_[0])) {
    // Full: drop the oldest LED cue or Save first, never a camera action.
    for (uint8_t i = 0; i < qn_; i++) {
      if (q_[i].act == ObAct::Led || q_[i].act == ObAct::Save) {
        for (uint8_t k = i + 1; k < qn_; k++) q_[k - 1] = q_[k];
        qn_--;
        break;
      }
    }
    if (qn_ == sizeof(q_) / sizeof(q_[0])) return;
  }
  q_[qn_++] = ObAction{a, arg, tag};
}

bool ModeEngine::popAction(ObAction& out) {
  if (!qn_) return false;
  out = q_[0];
  for (uint8_t i = 1; i < qn_; i++) q_[i - 1] = q_[i];
  qn_--;
  return true;
}

uint16_t ModeEngine::newTag() {
  if (++tag_ == 0) tag_ = 1;
  return tag_;
}

bool ModeEngine::running() const {
  return scr_ == Scr::TimerRun || scr_ == Scr::IntervalRun || scr_ == Scr::BurstRun ||
         scr_ == Scr::BulbOpen;
}

bool ModeEngine::busy() const {
  return running() || held() || shotPending() || p_.active || scr_ == Scr::Locked ||
         scr_ == Scr::Arm || scr_ == Scr::Card || scr_ == Scr::Editor || scr_ == Scr::Confirm ||
         scr_ == Scr::Refused;
}

uint32_t ModeEngine::capMs(uint32_t want_ms) const {
  const uint32_t cap = (uint32_t)s_.hold_cap_s * 1000;
  return want_ms < cap ? want_ms : cap;
}

void ModeEngine::setMsg(const char* big, const char* l1, const char* l2, uint32_t until) {
  msg_.on = true;
  msg_.until = until;
  copy(msg_.big, sizeof msg_.big, big);
  copy(msg_.l1, sizeof msg_.l1, l1);
  copy(msg_.l2, sizeof msg_.l2, l2);
  msg_.l3[0] = 0;
}

// ---------------------------------------------------------------- press tables

PressCtx ModeEngine::ctx() const {
  switch (scr_) {
    case Scr::Card:
    case Scr::Editor:
    case Scr::Arm:
    case Scr::Refused:
    case Scr::Confirm: return PressCtx::Picker;
    case Scr::Locked: return PressCtx::Locked;
    case Scr::TimerRun:
    case Scr::IntervalRun:
    case Scr::BurstRun:
    case Scr::BulbOpen: return PressCtx::Plain;  // the down edge stops it; the rest is swallowed
    case Scr::Home: break;
  }
  if (view_ > 0) return PressCtx::View;
  switch (mode_) {
    case ObMode::Shot:
    case ObMode::AfLock:
    case ObMode::Timer:
    // BURST holds like SHOT (bench result): AF while held, let go = the timed burst, past
    // the AF limit = the mode card with nothing fired. 'Burst while held' kept firing all the
    // way to the 10 s cap, so the only way to the card cost a 10 s burst.
    case ObMode::Burst: return PressCtx::Shutter;
    case ObMode::HoldAf: return PressCtx::HoldAf;
    default: return PressCtx::Plain;
  }
}

ZoneTable ModeEngine::table() const { return press_zones(s_.style, ctx(), afLimitMs(), s_.aux); }
ZoneTable ModeEngine::wakeTable() const {
  return press_zones(s_.style, PressCtx::Wake, afLimitMs(), s_.aux);
}
ZoneTable ModeEngine::auxTable() const {
  return press_zones(PressStyle::A, PressCtx::View, afLimitMs(), false);
}

// ---------------------------------------------------------------- held / release

void ModeEngine::releaseHeld() {
  shoot_due_ = false;
  release_due_ = false;
  if (af_held_ || shut_held_) push(ObAct::Release);
  af_held_ = shut_held_ = false;
}

void ModeEngine::startAf(const ObEnv& e) {
  push(ObAct::Focus);
  af_held_ = true;
  p_.af = true;
  p_.nolink = false;
  p_.af_at = e.now;
  push(ObAct::Led, 1);
}

void ModeEngine::cancelPending(uint8_t ctx) {
  if (!pending_tag_) return;
  cancel_ctx_ = ctx;
  push(ObAct::CancelPending, 0, pending_tag_);
}

void ModeEngine::onCancel(uint16_t tag, ObCancel r, const ObEnv& e) {
  (void)e;
  if (!tag || tag != pending_tag_) return;
  const uint8_t ctx = cancel_ctx_;
  cancel_ctx_ = 0;
  if (r != ObCancel::AlreadySent) pending_tag_ = 0;
  if (ctx == 1) {
    if (r == ObCancel::AlreadySent) copy(msg_.l3, sizeof msg_.l3, "1 shot already sent");
    else if (r == ObCancel::Cancelled) copy(msg_.l3, sizeof msg_.l3, "last shot cancelled");
  } else if (ctx == 2 && r == ObCancel::Cancelled) {
    // The timer's shot never left the board: it was stopped in time after all.
    setMsg("STOPPED", "timer: shot cancelled", "nothing fired");
  }
}

bool ModeEngine::orphanHeld() const {
  if (!held() || p_.active || shotPending()) return false;
  return scr_ != Scr::Locked && scr_ != Scr::TimerRun && scr_ != Scr::BurstRun && scr_ != Scr::BulbOpen;
}

void ModeEngine::abortPress(const ObEnv& e) {
  if (!p_.active) return;
  const Press p = p_;
  p_.active = false;
  if (p.af || p.shutter) releaseHeld();
  if (!p.swallow && !p.used) setMsg("PRESS LOST", "nothing fired", "", e.now + 3000);
}

void ModeEngine::shootNow(const ObEnv& e) {
  if (mode_ == ObMode::Burst) {
    // Let go after AF in BURST: the shutter goes down on top of the held AF for the burst
    // length; the BurstRun tick (or any press) releases both.
    if (e.relay) {
      releaseHeld();
      setMsg("NOT IN RELAY", "BURST is direct only", "nothing fired");
      return;
    }
    push(ObAct::Shoot, 0, newTag());
    shut_held_ = true;
    scr_ = Scr::BurstRun;
    run_start_ = e.now;
    run_end_ = e.now + capMs(s_.burst_ms);
    push(ObAct::Led, 1);
    return;
  }
  // Timed: the end that writes to the camera lets go after OB_SHUTTER_HOLD_MS by itself. Over
  // relay our Release frame comes a whole airtime later (or not at all), and a held shutter
  // in CH drive keeps shooting until it lands. The Release below is the backup.
  pending_tag_ = newTag();
  push(ObAct::Shoot, (uint16_t)OB_SHUTTER_HOLD_MS, pending_tag_);
  shut_held_ = true;
  release_due_ = true;
  release_at_ = e.now + OB_SHUTTER_HOLD_MS;
  push(ObAct::Led, 2);
}

bool ModeEngine::emitShot(uint16_t focus, const ObEnv& e, uint16_t* tag_out) {
  (void)e;
  if (shotPending()) {
    setMsg("BUSY", "last shot still running", "nothing fired", e.now + 2000);
    return false;
  }
  const uint16_t t = newTag();
  pending_tag_ = t;
  push(ObAct::Shot, focus, t);
  push(ObAct::Led, 2);
  if (tag_out) *tag_out = t;
  return true;
}

// ---------------------------------------------------------------- events

void ModeEngine::onPress(const PressEvent& ev, const ObEnv& e) {
  switch (ev.type) {
    case PressEvt::Down: onDown(ev, e); break;
    case PressEvt::Enter: onEnter(ev, e); break;
    case PressEvt::Up: onUp(ev, e); break;
    default: break;
  }
}

void ModeEngine::onDown(const PressEvent& ev, const ObEnv& e) {
  // A hold from a press whose Up never arrived: let go before this press means anything.
  if (p_.active && (p_.af || p_.shutter)) releaseHeld();
  p_ = Press{};
  if (orphanHeld()) releaseHeld();
  p_.active = true;
  p_.down_at = ev.at;
  p_.ctx = ctx();
  last_input_ = e.now;
  // Rule 3: stop wins, on the press itself. The rest of this press does nothing.
  if (running()) {
    if (scr_ == Scr::TimerRun && timer_fired_) {
      scr_ = Scr::Home;
      setMsg("TOO LATE", "shot was already sent", "at 0. It is a real shot.");
      cancelPending(2);  // still parked or queued: it can be stopped after all
    } else {
      stopRun(ObStop::ByPress, e);
    }
    p_.swallow = true;
    return;
  }
  if (late_ && !due(e.now, late_until_)) {
    late_ = false;
    setMsg("TOO LATE", "shot was already sent", "at 0. It is a real shot.");
    cancelPending(2);
    p_.swallow = true;
    return;
  }
  late_ = false;
  if (shotPending()) {
    setMsg("BUSY", "last shot still running", "nothing fired", e.now + 2000);
    p_.swallow = true;
    return;
  }
  if (scr_ == Scr::Home) msg_.on = false;
}

void ModeEngine::onEnter(const PressEvent& ev, const ObEnv& e) {
  if (!p_.active) return;
  p_.zone = ev.zone;
  if (ev.zone == Zone::Stuck) {
    forceStop(ObStop::HoldCap, e);
    setMsg("BUTTON STUCK", "nothing will fire", "going to sleep");
    p_.used = true;
    push(ObAct::Off, 1);
    return;
  }
  if (p_.swallow || p_.used) return;
  switch (ev.zone) {
    case Zone::Af:
      if (!p_.af && !held()) {
        // Direct with no camera link yet (just woken, BLE still connecting): no Focus can go
        // out, so none is claimed. AF starts if the link comes up during the hold (tick);
        // otherwise letting go is the full one-press shot, which waits for the camera.
        if (!e.relay && !e.link_ok) p_.nolink = true;
        else startAf(e);
      }
      break;
    case Zone::Shutter:
      if (!p_.shutter && !held()) {
        if (e.relay) {
          // Relay BURST is not offered (a lost Release could keep shooting).
          setMsg("NOT IN RELAY", "BURST is direct only", "nothing fired");
          p_.used = true;
          break;
        }
        push(ObAct::Shoot, 0, newTag());
        shut_held_ = true;
        p_.shutter = true;
        p_.shut_at = e.now;
        push(ObAct::Led, 1);
      }
      break;
    case Zone::Modes:
    case Zone::Cancel:
    case Zone::Nothing:
    case Zone::Menu:
    case Zone::Off:
      // Past the AF limit or the burst cap: let go of the camera now, nothing fires.
      if (p_.af || p_.shutter) releaseHeld();
      break;
    case Zone::Pick:
      p_.used = true;
      switch (scr_) {
        case Scr::Card: pickCard(card_sel_, e); break;
        case Scr::Editor: {
          uint32_t list[12];
          const uint8_t n = edList(ed_, list);
          if (ed_sel_ >= 0 && ed_sel_ < n) edSet(ed_, list[ed_sel_]);
          if (ed_ == Ed::IvEvery) {
            openEditor(Ed::IvCount, e);
          } else {
            scr_ = Scr::Home;
            push(ObAct::Save);
          }
          break;
        }
        case Scr::Arm: startInterval(e); break;
        case Scr::Refused: scr_ = Scr::Home; break;
        case Scr::Confirm:
          scr_ = Scr::Home;
          push(ObAct::Setup);
          break;
        default: break;
      }
      break;
    case Zone::NextView:
      p_.used = true;
      view_ = e.n_views > 1 ? (uint8_t)((view_ + 1) % e.n_views) : 0;
      break;
    case Zone::Unlock:
      p_.used = true;
      releaseHeld();
      scr_ = Scr::Home;
      setMsg("AF UNLOCKED", "no shot");
      push(ObAct::Led, LED_LONG);
      break;
    case Zone::Wheel: {
      p_.wheel = true;
      p_.wheel_at = e.now;
      CardItem items[OB_LIST_MAX];
      const uint8_t n = buildCard(items);
      p_.wheel_base = 0;
      for (uint8_t i = 0; i < n; i++)
        if (items[i].kind == Item::Mode && items[i].mode == mode_) p_.wheel_base = i;
      break;
    }
    default: break;
  }
}

void ModeEngine::onUp(const PressEvent& ev, const ObEnv& e) {
  if (!p_.active) return;
  const Press p = p_;
  p_.active = false;
  if (p.swallow || p.used) return;
  switch (ev.zone) {
    case Zone::Tap:
      if (p.wake) wakeTapAction(e);
      else tap(e);
      break;
    case Zone::Af:
      if (p.af) afLetGo(e);
      else if (p.nolink && mode_ != ObMode::Timer) emitShot(s_.focus_ms, e);  // waits for the camera
      else tap(e);  // the AF step was never acted on: it was a quick press
      break;
    case Zone::Shutter:
      if (p.shutter) {
        char b[12];
        fmt_tenths(b, sizeof b, e.now - p.shut_at);
        releaseHeld();
        setMsg("BURST DONE", b, "shutter released", e.now + 4000);
        push(ObAct::Led, LED_LONG);
      } else {
        tap(e);
      }
      break;
    case Zone::Modes:
    case Zone::Menu:
      if (p.af || p.shutter) releaseHeld();
      if (p.shutter) setMsg("STOPPED", ob_stop_text(ObStop::HoldCap), "shutter released");
      openCard(e, p.af);
      break;
    case Zone::Off:
      releaseHeld();
      push(ObAct::Off, 0);
      break;
    case Zone::Wheel: {
      CardItem items[OB_LIST_MAX];
      const uint8_t n = buildCard(items);
      if (!n) break;
      const uint32_t steps = (e.now - p.wheel_at) / 700;
      pickCard((uint8_t)((p.wheel_base + steps) % n), e);
      break;
    }
    case Zone::Cancel:
      releaseHeld();
      if (p.af) setMsg("AF TIMED OUT", "nothing fired", "", e.now + 4000);
      else if (p.shutter) setMsg("STOPPED", ob_stop_text(ObStop::HoldCap), "shutter released");
      break;
    default:
      releaseHeld();
      break;
  }
}

void ModeEngine::onAux(const PressEvent& ev, const ObEnv& e) {
  if (!s_.aux) return;
  if (ev.type == PressEvt::Down) {
    last_input_ = e.now;
    if (running()) {
      stopRun(ObStop::ByPress, e);
      p_.aux = true;  // the aux press that stopped it is used up (Up below sees it)
    }
    return;
  }
  if (ev.type == PressEvt::Enter && ev.zone == Zone::NextView) {
    if (p_.aux) return;
    if (scr_ == Scr::Card || scr_ == Scr::Editor) scr_ = Scr::Home;
    if (scr_ == Scr::Home) view_ = e.n_views > 1 ? (uint8_t)((view_ + 1) % e.n_views) : 0;
    p_.aux = true;
    return;
  }
  if (ev.type == PressEvt::Up) {
    const bool used = p_.aux;
    p_.aux = false;
    if (used || ev.zone != Zone::Tap) return;
    if (scr_ == Scr::Card) {
      CardItem items[OB_LIST_MAX];
      const uint8_t n = buildCard(items);
      card_sel_ = n ? (uint8_t)((card_sel_ + 1) % n) : 0;
      last_input_ = e.now;
    } else if (scr_ == Scr::Home && !held() && !p_.active) {
      openCard(e, false);
    }
  }
}

void ModeEngine::wakeDown(const ObEnv& e) {
  p_ = Press{};
  p_.active = true;
  p_.wake = true;
  p_.ctx = PressCtx::Wake;
  p_.down_at = e.now;  // counted from when the app first saw it down (boot time varies)
  last_input_ = e.now;
}

void ModeEngine::wakeTap(const ObEnv& e, bool aux) {
  if (aux) {
    setMsg("AWAKE", "the second button only", "wakes: nothing fired");
    return;
  }
  wakeTapAction(e);
}

void ModeEngine::wakeTapAction(const ObEnv& e) {
  // A wake tap acts only if the mode's tap is a single quick photo; otherwise it just wakes.
  if ((mode_ == ObMode::Shot || mode_ == ObMode::HoldAf) && s_.wake_act) {
    modeTap(e);
    return;
  }
  setMsg("AWAKE", "your press only woke me", "nothing was fired");
}

void ModeEngine::afLetGo(const ObEnv& e) {
  if (!af_held_) return;
  if (mode_ == ObMode::Timer) {
    // Let go starts the countdown with AF kept on; zero = shoot.
    scr_ = Scr::TimerRun;
    timer_len_ = (uint32_t)s_.timer_s * 1000;
    timer_end_ = e.now + timer_len_;
    timer_af_ = true;
    timer_fired_ = false;
    return;
  }
  // Never sooner than the focus time after AF started.
  const uint32_t ready = p_.af_at + s_.focus_ms;
  if (due(e.now, ready)) {
    shootNow(e);
  } else {
    shoot_due_ = true;
    shoot_at_ = ready;
  }
}

void ModeEngine::tap(const ObEnv& e) {
  switch (scr_) {
    case Scr::Home: modeTap(e); break;
    case Scr::Card: {
      CardItem items[OB_LIST_MAX];
      const uint8_t n = buildCard(items);
      card_sel_ = n ? (uint8_t)((card_sel_ + 1) % n) : 0;
      break;
    }
    case Scr::Editor: {
      uint32_t list[12];
      const uint8_t n = edList(ed_, list);
      if (ed_sel_ < 0) {
        // From a value off the list: to the first list value above it.
        const uint32_t cur = edValue(ed_);
        ed_sel_ = 0;
        for (uint8_t i = 0; i < n; i++)
          if (list[i] > cur) {
            ed_sel_ = (int8_t)i;
            break;
          }
      } else {
        ed_sel_ = (int8_t)((ed_sel_ + 1) % n);
      }
      break;
    }
    case Scr::Arm:
      scr_ = Scr::Home;
      setMsg("NOT STARTED", "nothing has fired", "", e.now + 3000);
      break;
    case Scr::Refused:
    case Scr::Confirm: scr_ = Scr::Home; break;
    case Scr::Locked:
      // Fire now: focus is already held, so no focus wait.
      shootNow(e);
      scr_ = Scr::Home;
      setMsg("FIRED", "AF LOCK: shoot + release", "", e.now + 3000);
      break;
    default: break;
  }
}

void ModeEngine::modeTap(const ObEnv& e) {
  switch (mode_) {
    case ObMode::Shot:
    case ObMode::HoldAf: emitShot(s_.focus_ms, e); break;
    case ObMode::AfLock:
      if (held()) break;
      if (!e.relay && !e.link_ok) {
        // Nothing can hold AF on a camera we are not connected to: never claim LOCKED.
        setMsg("NOT LOCKED", "NO CAMERA: connecting", "nothing was sent", e.now + 3000);
        break;
      }
      push(ObAct::Focus);
      af_held_ = true;
      scr_ = Scr::Locked;
      lock_until_ = e.now + (uint32_t)s_.aflock_s * 1000;
      push(ObAct::Led, 1);
      break;
    case ObMode::Timer:
      scr_ = Scr::TimerRun;
      timer_len_ = (uint32_t)s_.timer_s * 1000;
      timer_end_ = e.now + timer_len_;
      timer_af_ = false;
      timer_fired_ = false;
      break;
    case ObMode::Interval: openArm(e); break;
    case ObMode::Burst:
      if (e.relay) {
        setMsg("NOT IN RELAY", "BURST is direct only", "nothing fired");
        break;
      }
      if (held()) break;
      push(ObAct::Shoot, 0, newTag());
      shut_held_ = true;
      scr_ = Scr::BurstRun;
      run_start_ = e.now;
      run_end_ = e.now + capMs(s_.burst_ms);
      push(ObAct::Led, 1);
      break;
    case ObMode::Bulb:
      if (e.relay) {
        setMsg("NOT IN RELAY", "BULB is direct only", "nothing fired");
        break;
      }
      if (e.batt != 255 && e.batt < OB_ARM_MIN_BATT) {
        setMsg("NOT OPENED", "battery below 15%", "nothing fired");
        break;
      }
      if (held()) break;
      push(ObAct::Shoot, 0, newTag());
      shut_held_ = true;
      scr_ = Scr::BulbOpen;
      run_start_ = e.now;
      run_end_ = e.now + capMs((uint32_t)s_.bulb_s * 1000);
      push(ObAct::RunFlag, 1);
      push(ObAct::Led, 1);
      break;
    case ObMode::Video: {
      uint16_t t = 0;
      if (emitShot(0, e, &t)) {
        video_tag_ = t;
        rec_ = !rec_;
        rec_unknown_ = false;
        rec_since_ = e.now;
      }
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------- stop / tick

void ModeEngine::stopRun(ObStop why, const ObEnv& e) {
  releaseHeld();
  char a[28], b[12];
  switch (scr_) {
    case Scr::IntervalRun:
      if (iv_count_) snprintf(a, sizeof a, "%u of %u taken", iv_ok_, iv_count_);
      else snprintf(a, sizeof a, "%u taken", iv_ok_);
      setMsg(why == ObStop::Done ? "DONE" : "STOPPED", a, ob_stop_text(why));
      push(ObAct::RunFlag, 0);
      if (why != ObStop::Done) cancelPending(1);  // a shot still waiting must not fire now
      break;
    case Scr::TimerRun:
      if (timer_fired_) {
        setMsg("STOPPED", "timer: shot at 0", ob_stop_text(why));
        cancelPending(1);
      } else {
        setMsg("STOPPED", "timer: nothing fired", ob_stop_text(why));
      }
      break;
    case Scr::BurstRun:
      fmt_tenths(b, sizeof b, e.now - run_start_);
      snprintf(a, sizeof a, "burst %s", b);
      setMsg("STOPPED", a, ob_stop_text(why));
      break;
    case Scr::BulbOpen: {
      char t1[12], t2[12];
      fmt_clock(t1, sizeof t1, (e.now - run_start_) / 1000);
      fmt_clock(t2, sizeof t2, (run_end_ - run_start_) / 1000);
      snprintf(a, sizeof a, "%s of %s", t1, t2);
      setMsg(why == ObStop::ByPress ? "CLOSED" : "STOPPED", a, ob_stop_text(why));
      push(ObAct::RunFlag, 0);
      break;
    }
    default: return;
  }
  scr_ = Scr::Home;
  push(ObAct::Led, LED_LONG);
}

void ModeEngine::forceStop(ObStop why, const ObEnv& e) {
  if (running()) stopRun(why, e);
  releaseHeld();
  if (scr_ != Scr::Home) scr_ = Scr::Home;
  p_.swallow = true;
}

void ModeEngine::noteReset() { setMsg("STOPPED", "the last run was cut", ob_stop_text(ObStop::Reset)); }

void ModeEngine::tick(const ObEnv& e) {
  const uint32_t now = e.now;
  if (shoot_due_ && due(now, shoot_at_)) {
    shoot_due_ = false;
    shootNow(e);
  }
  if (release_due_ && due(now, release_at_)) {
    release_due_ = false;
    releaseHeld();
  }
  if (orphanHeld()) releaseHeld();  // backstop: never left holding with no owner
  // A hold reached AF while the camera was not connected: start AF once it is.
  if (p_.active && p_.nolink && !p_.af && p_.zone == Zone::Af && !p_.swallow && !p_.used &&
      e.link_ok && !held())
    startAf(e);
  if (running() && e.batt != 255 && e.batt <= OB_STOP_BATT) {
    stopRun(ObStop::Battery, e);
    return;
  }
  switch (scr_) {
    case Scr::Locked:
      if (due(now, lock_until_)) {
        // The lock ran out under a press that began as "tap = fire": that press must do
        // nothing on release, or its tap would land on Home and start a fresh lock.
        if (p_.active) p_.swallow = true;
        releaseHeld();
        scr_ = Scr::Home;
        setMsg("AF RELEASED", "timeout, no shot");
        push(ObAct::Led, LED_LONG);
      }
      break;
    case Scr::TimerRun: {
      const uint32_t lead = e.relay && !timer_af_ ? e.lead_ms : 0;
      if (!timer_fired_ && due(now + lead, timer_end_)) {
        timer_fired_ = true;
        if (timer_af_) shootNow(e);
        else emitShot(s_.focus_ms, e);
      }
      if (timer_fired_ && due(now, timer_end_)) {
        scr_ = Scr::Home;
        late_ = true;
        late_until_ = now + OB_LATE_MS;
        setMsg("FIRED", "timer reached 0", "", now + 4000);
      }
      break;
    }
    case Scr::IntervalRun: ivTick(e); break;
    case Scr::BurstRun:
      if (due(now, run_end_)) {
        char b[12], a[28];
        fmt_tenths(b, sizeof b, run_end_ - run_start_);
        snprintf(a, sizeof a, "shutter held %s", b);
        releaseHeld();
        scr_ = Scr::Home;
        setMsg("BURST DONE", a, "", now + 4000);
        push(ObAct::Led, LED_LONG);
      }
      break;
    case Scr::BulbOpen:
      if (due(now, run_end_)) {
        char t[12], a[28];
        fmt_clock(t, sizeof t, (run_end_ - run_start_) / 1000);
        snprintf(a, sizeof a, "open %s, closed", t);
        releaseHeld();
        scr_ = Scr::Home;
        setMsg("BULB DONE", a);
        push(ObAct::RunFlag, 0);
        push(ObAct::Led, LED_LONG);
      }
      break;
    case Scr::Card:
    case Scr::Editor:
      if (!p_.active && now - last_input_ >= OB_CARD_IDLE_MS) scr_ = Scr::Home;
      break;
    case Scr::Arm:
    case Scr::Refused:
    case Scr::Confirm:
      if (!p_.active && now - last_input_ >= OB_ARM_IDLE_MS) scr_ = Scr::Home;
      break;
    default: break;
  }
  if (msg_.on && msg_.until && due(now, msg_.until)) msg_.on = false;
}

void ModeEngine::onOutcome(uint16_t tag, ObOutcome o, const ObEnv& e) {
  if (tag && tag == pending_tag_) pending_tag_ = 0;
  if (tag && tag == video_tag_ && (o == ObOutcome::Fail || o == ObOutcome::Unknown ||
                                   o == ObOutcome::Duty || o == ObOutcome::Busy)) {
    rec_unknown_ = true;
  }
  if (scr_ != Scr::IntervalRun || !tag || (uint16_t)(tag - iv_first_tag_) > 60000) return;
  // Each shot counts once, whatever the app reports twice.
  for (uint8_t i = 0; i < sizeof(iv_seen_) / sizeof(iv_seen_[0]); i++)
    if (iv_seen_[i] == tag) return;
  iv_seen_[iv_seen_i_++ % (sizeof(iv_seen_) / sizeof(iv_seen_[0]))] = tag;
  if (iv_out_) iv_out_--;
  switch (o) {
    case ObOutcome::Ok:
      iv_ok_++;
      iv_streak_fail_ = iv_streak_skip_ = 0;
      break;
    case ObOutcome::Unknown:
      iv_unk_++;
      iv_streak_fail_ = iv_streak_skip_ = 0;
      break;
    case ObOutcome::Fail:
      iv_fail_++;
      iv_streak_skip_ = 0;
      if (++iv_streak_fail_ >= OB_FAIL_STREAK) stopRun(e.relay ? ObStop::NoLink : ObStop::NoCamera, e);
      break;
    case ObOutcome::Duty:
      iv_skip_++;
      if (++iv_streak_skip_ >= OB_FAIL_STREAK) stopRun(ObStop::AirLimit, e);
      break;
    case ObOutcome::Busy:
      iv_skip_++;
      break;
  }
}

// ---------------------------------------------------------------- interval

PlanOut ModeEngine::plan(const ObEnv& e) const {
  PlanIn in;
  in.sf = e.sf;
  in.hh_permille = e.hh_permille;
  in.cam_permille = e.cam_permille;
  in.every_s = s_.iv_every_s;
  in.count = s_.iv_count;
  return plan_interval(in);
}

void ModeEngine::openArm(const ObEnv& e) {
  scr_ = Scr::Arm;
  last_input_ = e.now;
  plan_relay_ = e.relay;
  plan_batt_ = e.batt;
  plan_ = plan(e);
}

void ModeEngine::startInterval(const ObEnv& e) {
  plan_relay_ = e.relay;
  plan_batt_ = e.batt;
  if (e.batt != 255 && e.batt < OB_ARM_MIN_BATT) {
    refuse_kind_ = 1;
    scr_ = Scr::Refused;
    last_input_ = e.now;
    return;
  }
  if (e.relay) {
    plan_ = plan(e);
    if (!plan_.fits) {
      refuse_kind_ = 0;
      scr_ = Scr::Refused;
      last_input_ = e.now;
      return;
    }
  }
  scr_ = Scr::IntervalRun;
  iv_every_ = s_.iv_every_s;
  iv_count_ = s_.iv_count;
  iv_k_ = iv_ok_ = iv_fail_ = iv_skip_ = iv_unk_ = 0;
  iv_streak_fail_ = iv_streak_skip_ = iv_out_ = 0;
  memset(iv_seen_, 0, sizeof iv_seen_);
  iv_seen_i_ = 0;
  iv_first_tag_ = (uint16_t)(tag_ + 1 == 0 ? 1 : tag_ + 1);
  iv_start_ = e.now;
  iv_next_ = e.now;  // first shot at once
  iv_done_wait_ = 0;
  push(ObAct::RunFlag, 1);
}

// The longest the app can take to report a relayed shot's outcome: every send try, then the
// wait for the Result, then the camera-event window and its grace. Mirrors app_handheld.cpp
// (MAX_TRIES 4, ack_timeout = frame + Ack + 150, result_wait_ms = 6200 + focus + 2 Acks,
// CAM_EVENT_WINDOW_MS 2000, grace = 2 Acks + 500). Never under the old 10 s; direct keeps it.
uint32_t ModeEngine::lastOutcomeWaitMs(const ObEnv& e) const {
  const uint32_t floor_ms = 10000;
  if (!e.relay) return floor_ms;
  // Always sized for SF12, not e.sf: a try can go out on SF12 (unconfirmed SF, rescue) and the
  // answering SF is adopted, so the rest of the exchange is then at SF12 too. The run still
  // ends the moment the outcome arrives, so the longer bound costs nothing on a fast SF.
  const uint8_t sf = 12;
  const uint32_t shot = plan_shot_cost_ms(sf, false);  // one Shot frame
  const uint32_t acks2 = plan_shot_cost_ms(sf, true);  // two Ack-sized frames
  const uint32_t w = 4 * (shot + acks2 / 2 + 150) + (6200 + s_.focus_ms + acks2) + 2000 + (acks2 + 500);
  return w > floor_ms ? w : floor_ms;
}

void ModeEngine::ivTick(const ObEnv& e) {
  const uint32_t now = e.now;
  if (now - iv_start_ >= PLAN_MAX_RUN_S * 1000UL) {
    stopRun(ObStop::TimeLimit, e);
    return;
  }
  if (iv_count_ && iv_k_ >= iv_count_) {
    if (!iv_out_ || due(now, iv_done_wait_)) stopRun(ObStop::Done, e);
    return;
  }
  if (!due(now, iv_next_)) return;
  if (e.relay && !plan_shot_fits(e.sf, e.hh_permille, e.cam_permille)) {
    iv_skip_++;
    if (++iv_streak_skip_ >= OB_FAIL_STREAK) {
      iv_k_++;
      stopRun(ObStop::AirLimit, e);
      return;
    }
  } else if (shotPending()) {
    iv_skip_++;
  } else {
    if (emitShot(s_.focus_ms, e)) iv_out_++;
    else iv_skip_++;
  }
  iv_k_++;
  iv_next_ += (uint32_t)iv_every_ * 1000;
  // Fell behind (a long blocking send): never fire a burst of catch-up shots.
  if (due(now, iv_next_)) iv_next_ = now + (uint32_t)iv_every_ * 1000;
  // How long the last outcome may take differs with the SF: a fixed 10 s ended an SF12
  // relay run one short ("n-1 of n taken") although the last frame had fired.
  if (iv_count_ && iv_k_ >= iv_count_) iv_done_wait_ = now + lastOutcomeWaitMs(e);
}

// ---------------------------------------------------------------- card / editor

uint8_t ModeEngine::buildCard(CardItem* out) const {
  uint8_t n = 0;
  bool has_holdaf = false;
  for (uint8_t i = 0; i < s_.n_modes && n < OB_LIST_MAX - 3; i++) {
    const ObMode m = s_.modes[i];
    if (m == ObMode::HoldAf) has_holdaf = true;
    out[n++] = CardItem{Item::Mode, m};
    // C: focus-then-shoot is its own mode, right after SHOT.
    if (i == 0 && s_.style == PressStyle::C && n < OB_LIST_MAX - 3) {
      bool listed = false;
      for (uint8_t k = 0; k < s_.n_modes; k++) listed = listed || s_.modes[k] == ObMode::HoldAf;
      if (!listed) out[n++] = CardItem{Item::Mode, ObMode::HoldAf};
    }
  }
  (void)has_holdaf;
  out[n++] = CardItem{Item::Views, ObMode::Shot};
  out[n++] = CardItem{Item::Setup, ObMode::Shot};
  out[n++] = CardItem{Item::Off, ObMode::Shot};
  return n;
}

void ModeEngine::openCard(const ObEnv& e, bool from_af) {
  CardItem items[OB_LIST_MAX];
  const uint8_t n = buildCard(items);
  card_sel_ = 0;
  for (uint8_t i = 0; i < n; i++)
    if (items[i].kind == Item::Mode && items[i].mode == mode_) card_sel_ = i;
  card_from_af_ = from_af;
  view_ = 0;
  scr_ = Scr::Card;
  last_input_ = e.now;
}

void ModeEngine::pickCard(uint8_t i, const ObEnv& e) {
  CardItem items[OB_LIST_MAX];
  const uint8_t n = buildCard(items);
  if (i >= n) {
    scr_ = Scr::Home;
    return;
  }
  const CardItem it = items[i];
  scr_ = Scr::Home;
  switch (it.kind) {
    case Item::Mode:
      if (e.relay && direct_only(it.mode)) {
        setMsg("NOT IN RELAY", ob_mode_name(it.mode), "is direct only for now");
        return;
      }
      if (it.mode == mode_) {
        switch (it.mode) {
          case ObMode::Timer: openEditor(Ed::Timer, e); break;
          case ObMode::AfLock: openEditor(Ed::AfLock, e); break;
          case ObMode::Interval: openEditor(Ed::IvEvery, e); break;
          case ObMode::Burst: openEditor(Ed::Burst, e); break;
          case ObMode::Bulb: openEditor(Ed::Bulb, e); break;
          default: break;
        }
        return;
      }
      setMode(it.mode);
      push(ObAct::Save);
      push(ObAct::Led, (uint16_t)(i + 1));
      break;
    case Item::Views: view_ = e.n_views > 1 ? 1 : 0; break;
    case Item::Setup:
      scr_ = Scr::Confirm;
      last_input_ = e.now;
      break;
    case Item::Off: push(ObAct::Off, 0); break;
  }
}

uint8_t ModeEngine::edList(Ed what, uint32_t* out) const {
  static const uint32_t kTimer[] = {2, 5, 10, 20};
  static const uint32_t kLock[] = {10, 30, 60};
  static const uint32_t kEvery[] = {2, 3, 5, 10, 15, 30, 60};
  static const uint32_t kCount[] = {10, 30, 60, 120, 360, 0};
  static const uint32_t kBurst[] = {500, 1000, 2000, 3000};
  static const uint32_t kBulb[] = {30, 60, 120, 240, 480, 900, 1800};
  const uint32_t* src = kTimer;
  uint8_t n = 4;
  switch (what) {
    case Ed::Timer: src = kTimer; n = 4; break;
    case Ed::AfLock: src = kLock; n = 3; break;
    case Ed::IvEvery: src = kEvery; n = 7; break;
    case Ed::IvCount: src = kCount; n = 6; break;
    case Ed::Burst: src = kBurst; n = 4; break;
    case Ed::Bulb: src = kBulb; n = 7; break;
  }
  for (uint8_t i = 0; i < n; i++) out[i] = src[i];
  return n;
}

uint32_t ModeEngine::edValue(Ed what) const {
  switch (what) {
    case Ed::Timer: return s_.timer_s;
    case Ed::AfLock: return s_.aflock_s;
    case Ed::IvEvery: return s_.iv_every_s;
    case Ed::IvCount: return s_.iv_count;
    case Ed::Burst: return s_.burst_ms;
    case Ed::Bulb: return s_.bulb_s;
  }
  return 0;
}

void ModeEngine::edSet(Ed what, uint32_t v) {
  switch (what) {
    case Ed::Timer: s_.timer_s = (uint8_t)v; break;
    case Ed::AfLock: s_.aflock_s = (uint8_t)v; break;
    case Ed::IvEvery: s_.iv_every_s = (uint16_t)v; break;
    case Ed::IvCount: s_.iv_count = (uint16_t)v; break;
    case Ed::Burst: s_.burst_ms = (uint16_t)v; break;
    case Ed::Bulb: s_.bulb_s = (uint16_t)v; break;
  }
  ob_clamp(s_);
}

void ModeEngine::openEditor(Ed what, const ObEnv& e) {
  ed_ = what;
  ed_sel_ = -1;
  uint32_t list[12];
  const uint8_t n = edList(what, list);
  const uint32_t cur = edValue(what);
  for (uint8_t i = 0; i < n; i++)
    if (list[i] == cur) ed_sel_ = (int8_t)i;
  scr_ = Scr::Editor;
  last_input_ = e.now;
}

// ---------------------------------------------------------------- screen

void ModeEngine::titleFor(char* out, size_t cap) const {
  char v[12];
  switch (mode_) {
    case ObMode::Timer: snprintf(out, cap, "TIMER %us", s_.timer_s); break;
    case ObMode::Burst:
      fmt_tenths(v, sizeof v, s_.burst_ms);
      snprintf(out, cap, "BURST %s", v);
      break;
    case ObMode::Bulb:
      fmt_dur(v, sizeof v, s_.bulb_s);
      snprintf(out, cap, "BULB %s", v);
      break;
    default: copy(out, cap, ob_mode_name(mode_)); break;
  }
}

// Every label bar fits OB_BAR_MAX characters (the 5x7 font across 124 px): checked for every
// style, mode and state in test_onebutton.
void ModeEngine::barHome(char* out, size_t cap) const {
  if (view_ > 0) {
    snprintf(out, cap, "TAP %s HOLD 1s: NEXT",
             mode_ == ObMode::Shot || mode_ == ObMode::HoldAf ? "SHOT" : "MODE");
    return;
  }
  const char* hold = "";
  switch (s_.style) {
    case PressStyle::B: hold = "HOLD: WHEEL"; break;
    case PressStyle::C: hold = "HOLD: MENU"; break;
    default:
      switch (mode_) {
        case ObMode::Shot:
        case ObMode::AfLock:
        case ObMode::Timer:
        case ObMode::Burst: hold = "HOLD AF"; break;
        default: hold = s_.aux ? "" : "HOLD: MODES"; break;
      }
  }
  // HOLD AF (every style): a hold is AF first; held on past the AF limit it is the way out.
  if (mode_ == ObMode::HoldAf)
    hold = s_.aux ? "HOLD AF" : s_.style == PressStyle::B ? "HOLD AF>WHEEL" : "HOLD AF>MODES";
  char v[12];
  switch (mode_) {
    case ObMode::Shot:
    case ObMode::HoldAf: snprintf(out, cap, "TAP SHOT  %s", hold); break;
    case ObMode::AfLock: snprintf(out, cap, "TAP LOCK  %s", hold); break;
    case ObMode::Timer: snprintf(out, cap, "TAP %us  %s", s_.timer_s, hold); break;
    case ObMode::Interval: snprintf(out, cap, "TAP: ARM  %s", hold); break;
    case ObMode::Burst:
      fmt_tenths(v, sizeof v, capMs(s_.burst_ms));
      snprintf(out, cap, "TAP %s  %s", v, hold);
      break;
    case ObMode::Bulb:
      fmt_dur(v, sizeof v, capMs((uint32_t)s_.bulb_s * 1000) / 1000);
      snprintf(out, cap, "OPEN %s  %s", v, hold);
      break;
    case ObMode::Video:
      if (rec_unknown_) snprintf(out, cap, "TAP: PRESS  %s", hold);
      else snprintf(out, cap, "TAP: %s  %s", rec_ ? "STOP" : "REC", hold);
      break;
    default: snprintf(out, cap, "TAP  %s", hold); break;
  }
}

void ModeEngine::screen(ObScreen& o, const ObEnv& e) const {
  o = ObScreen{};
  const uint32_t now = e.now;
  titleFor(o.title, sizeof o.title);
  barHome(o.bar, sizeof o.bar);
  char a[10], b[10];

  // A press in progress owns the screen: the bar says what letting go does now.
  if (p_.active && !p_.swallow && !p_.used && p_.zone != Zone::Tap) {
    const uint32_t held = now - p_.down_at;
    switch (p_.zone) {
      case Zone::Af: {
        o.kind = ObKind::Text;
        if (!p_.af && p_.nolink) {
          copy(o.big, sizeof o.big, "AF: NO CAMERA");
          copy(o.l1, sizeof o.l1, "connecting...");
          copy(o.l2, sizeof o.l2, "AF starts when it is in");
          if (mode_ == ObMode::Timer) snprintf(o.bar, sizeof o.bar, "LET GO: START %us", s_.timer_s);
          else copy(o.bar, sizeof o.bar, "LET GO: SHOT (waits)");
          return;
        }
        copy(o.big, sizeof o.big, !e.af_confirmed ? "AF..." : "AF HELD");
        fmt_tenths(o.right, sizeof o.right, held);
        const uint32_t L = afLimitMs();
        o.progress = (int16_t)(held >= L ? 1000 : held * 1000 / L);
        snprintf(o.l1, sizeof o.l1, "past %us: AF off,", s_.af_limit_s);
        if (s_.aux) copy(o.l2, sizeof o.l2, "no shot");
        else if (p_.ctx == PressCtx::HoldAf)
          copy(o.l2, sizeof o.l2, s_.style == PressStyle::B ? "no shot, then wheel" : "no shot, then modes");
        else copy(o.l2, sizeof o.l2, "no shot, mode card");
        if (e.relay && e.sf >= 10) copy(o.l3, sizeof o.l3, "tap is faster");
        if (mode_ == ObMode::Timer) snprintf(o.bar, sizeof o.bar, "LET GO: START %us", s_.timer_s);
        else copy(o.bar, sizeof o.bar, "LET GO: SHOOT");
        return;
      }
      case Zone::Shutter:
        o.kind = ObKind::Text;
        copy(o.big, sizeof o.big, "SHUTTER ON");
        fmt_tenths(o.right, sizeof o.right, now - p_.shut_at);
        o.progress = (int16_t)((now - p_.shut_at) * 1000 / PRESS_BURST_CAP_MS);
        if (o.progress > 1000) o.progress = 1000;
        copy(o.l1, sizeof o.l1, "burst while held");
        copy(o.l2, sizeof o.l2, "cap 10s");
        copy(o.bar, sizeof o.bar, "LET GO: STOP");
        return;
      case Zone::Modes:
      case Zone::Cancel:
      case Zone::Nothing:
        if (s_.style == PressStyle::C && !p_.af) break;  // C ladder below
        o.kind = ObKind::Text;
        if (p_.af) copy(o.big, sizeof o.big, p_.zone == Zone::Cancel ? "AF TIMED OUT" : "AF RELEASED");
        else if (p_.shutter) copy(o.big, sizeof o.big, "HOLD CAP");
        else copy(o.big, sizeof o.big, p_.zone == Zone::Modes ? "MODES" : "NOTHING");
        copy(o.l1, sizeof o.l1, "nothing will fire");
        o.progress = 1000;
        if (p_.zone == Zone::Modes) {
          copy(o.l2, sizeof o.l2, "keep holding: NOTHING");
          copy(o.bar, sizeof o.bar, "LET GO: MODES");
        } else if (p_.zone == Zone::Cancel && p_.ctx == PressCtx::HoldAf && !s_.aux) {
          // HOLD AF: holding on is the way to the mode card (or the wheel in B).
          copy(o.l2, sizeof o.l2, s_.style == PressStyle::B ? "keep holding: WHEEL" : "keep holding: MODES");
          copy(o.bar, sizeof o.bar, "LET GO: NOTHING");
        } else {
          copy(o.bar, sizeof o.bar, "LET GO: NOTHING");
        }
        return;
      case Zone::Wheel: {
        CardItem items[OB_LIST_MAX];
        const uint8_t n = buildCard(items);
        o.kind = ObKind::List;
        copy(o.head, sizeof o.head, "HOLDING  let go = pick");
        o.n = n;
        o.sel = (uint8_t)((p_.wheel_base + (now - p_.wheel_at) / 700) % (n ? n : 1));
        for (uint8_t i = 0; i < n; i++) {
          o.item[i][0] = o.val[i][0] = 0;
          if (items[i].kind == Item::Mode) copy(o.item[i], sizeof o.item[i], ob_mode_name(items[i].mode));
          else copy(o.item[i], sizeof o.item[i], items[i].kind == Item::Views ? "VIEWS" : items[i].kind == Item::Setup ? "SETUP" : "OFF");
        }
        snprintf(o.bar, sizeof o.bar, "LET GO: %s", o.item[o.sel]);
        return;
      }
      case Zone::Stuck:
        o.kind = ObKind::Text;
        copy(o.big, sizeof o.big, "BUTTON STUCK");
        copy(o.l1, sizeof o.l1, "nothing will fire");
        copy(o.bar, sizeof o.bar, "LET GO");
        return;
      default: break;
    }
    // C ladder: what letting go does at each step.
    if (p_.zone == Zone::Menu || p_.zone == Zone::Off || p_.zone == Zone::Nothing) {
      o.kind = ObKind::List;
      copy(o.head, sizeof o.head, "HOLDING  let go = pick");
      static const char* const kLadder[] = {"SHOOT", "MENU", "OFF", "NOTHING"};
      static const char* const kAt[] = {"", "1s", "4s", "8s"};
      o.n = 4;
      for (uint8_t i = 0; i < 4; i++) {
        copy(o.item[i], sizeof o.item[i], kLadder[i]);
        copy(o.val[i], sizeof o.val[i], kAt[i]);
      }
      o.sel = p_.zone == Zone::Menu ? 1 : p_.zone == Zone::Off ? 2 : 3;
      snprintf(o.bar, sizeof o.bar, "LET GO: %s", kLadder[o.sel]);
      return;
    }
  }

  switch (scr_) {
    case Scr::Card: {
      CardItem items[OB_LIST_MAX];
      const uint8_t n = buildCard(items);
      o.kind = ObKind::List;
      const uint32_t left = (OB_CARD_IDLE_MS - (now - last_input_ > OB_CARD_IDLE_MS ? OB_CARD_IDLE_MS : now - last_input_) + 999) / 1000;
      snprintf(o.head, sizeof o.head, "MODE%s  %lu", card_from_af_ ? "  AF off, no shot" : "",
               (unsigned long)left);
      o.n = n;
      o.sel = card_sel_;
      for (uint8_t i = 0; i < n; i++) {
        o.val[i][0] = 0;
        if (items[i].kind != Item::Mode) {
          copy(o.item[i], sizeof o.item[i],
               items[i].kind == Item::Views ? "VIEWS" : items[i].kind == Item::Setup ? "SETUP" : "OFF");
          continue;
        }
        const ObMode m = items[i].mode;
        copy(o.item[i], sizeof o.item[i], ob_mode_name(m));
        switch (m) {
          case ObMode::AfLock: snprintf(o.val[i], sizeof o.val[i], "%us", s_.aflock_s); break;
          case ObMode::Timer: snprintf(o.val[i], sizeof o.val[i], "%us", s_.timer_s); break;
          case ObMode::Interval:
            fmt_dur(a, sizeof a, s_.iv_every_s);
            if (s_.iv_count) snprintf(o.val[i], sizeof o.val[i], "%sx%u", a, s_.iv_count);
            else snprintf(o.val[i], sizeof o.val[i], "%s x-", a);
            break;
          case ObMode::Burst: fmt_tenths(o.val[i], sizeof o.val[i], s_.burst_ms); break;
          case ObMode::Bulb: fmt_dur(o.val[i], sizeof o.val[i], s_.bulb_s); break;
          default: break;
        }
        if (e.relay && direct_only(m)) copy(o.val[i], sizeof o.val[i], "DIRECT");
      }
      copy(o.bar, sizeof o.bar, "TAP NEXT     HOLD PICK");
      return;
    }
    case Scr::Editor: {
      uint32_t list[12];
      const uint8_t n = edList(ed_, list);
      o.kind = ObKind::List;
      static const char* const kEd[] = {"TIMER", "AF LOCK TIME", "INTERVAL: EVERY", "INTERVAL: COUNT", "BURST", "BULB"};
      copy(o.head, sizeof o.head, kEd[(uint8_t)ed_]);
      uint8_t k = 0;
      if (ed_sel_ < 0) {  // a Mac-only value: shown as typed, at the top
        const uint32_t v = edValue(ed_);
        if (ed_ == Ed::Burst) fmt_tenths(o.item[k], sizeof o.item[k], v);
        else if (ed_ == Ed::IvCount) snprintf(o.item[k], sizeof o.item[k], "%lu", (unsigned long)v);
        else fmt_dur(o.item[k], sizeof o.item[k], v);
        copy(o.val[k], sizeof o.val[k], "now");
        k++;
      }
      for (uint8_t i = 0; i < n && k < OB_LIST_MAX; i++, k++) {
        o.val[k][0] = 0;
        if (ed_ == Ed::Burst) fmt_tenths(o.item[k], sizeof o.item[k], list[i]);
        else if (ed_ == Ed::IvCount) {
          if (list[i]) snprintf(o.item[k], sizeof o.item[k], "%lu shots", (unsigned long)list[i]);
          else copy(o.item[k], sizeof o.item[k], "until stop");
        } else {
          fmt_dur(o.item[k], sizeof o.item[k], list[i]);
        }
        if (list[i] == edValue(ed_)) copy(o.val[k], sizeof o.val[k], "now");
      }
      o.n = k;
      o.sel = ed_sel_ < 0 ? 0 : (uint8_t)ed_sel_;
      copy(o.bar, sizeof o.bar, "TAP NEXT     HOLD KEEP");
      return;
    }
    case Scr::Arm: {
      o.kind = ObKind::Text;
      copy(o.title, sizeof o.title, "INTERVAL");
      fmt_dur(a, sizeof a, s_.iv_every_s);
      if (s_.iv_count) snprintf(o.big, sizeof o.big, "%s x %u", a, s_.iv_count);
      else snprintf(o.big, sizeof o.big, "%s x until", a);
      fmt_dur(o.right, sizeof o.right, plan_.duration_s >= 60 ? plan_.duration_s / 60 * 60 : plan_.duration_s);
      if (plan_relay_)
        snprintf(o.l1, sizeof o.l1, "needs AIR %u%% have %u%%", (plan_.need_permille + 9) / 10,
                 plan_.have_permille / 10);
      else copy(o.l1, sizeof o.l1, "direct: no air limit");
      fmt_dur(b, sizeof b, plan_.duration_s >= 60 ? plan_.duration_s / 60 * 60 : plan_.duration_s);
      if (plan_batt_ == 255) snprintf(o.l2, sizeof o.l2, "ends in ~%s", b);
      else snprintf(o.l2, sizeof o.l2, "ends in ~%s  batt %u%%", b, plan_batt_);
      copy(o.l3, sizeof o.l3, "nothing has fired yet");
      copy(o.bar, sizeof o.bar, "HOLD 1s: START TAP: BACK");
      return;
    }
    case Scr::Refused:
      o.kind = ObKind::Text;
      copy(o.title, sizeof o.title, "INTERVAL");
      copy(o.big, sizeof o.big, "NOT STARTED");
      if (refuse_kind_ == 1) {
        copy(o.l1, sizeof o.l1, "battery below 15%");
        copy(o.l2, sizeof o.l2, "charge, then arm again");
      } else {
        snprintf(o.l1, sizeof o.l1, "needs AIR %u%% have %u%%", (plan_.need_permille + 9) / 10,
                 plan_.have_permille / 10);
        if (plan_.fit_every_s && plan_.fit_count) {
          fmt_dur(a, sizeof a, plan_.fit_every_s);
          snprintf(o.l2, sizeof o.l2, "fits: every %s, or %u", a, plan_.fit_count);
        } else if (plan_.fit_every_s) {
          fmt_dur(a, sizeof a, plan_.fit_every_s);
          snprintf(o.l2, sizeof o.l2, "fits: every %s", a);
        } else if (plan_.fit_count) {
          snprintf(o.l2, sizeof o.l2, "fits: %u shots", plan_.fit_count);
        } else {
          copy(o.l2, sizeof o.l2, "nothing fits this hour");
        }
      }
      copy(o.l3, sizeof o.l3, refuse_kind_ == 1 || !(plan_.fit_count && plan_.fit_every_s)
                                  ? "Nothing was sent." : "shots. Nothing was sent.");
      copy(o.bar, sizeof o.bar, "TAP: BACK");
      return;
    case Scr::Confirm:
      o.kind = ObKind::Text;
      copy(o.big, sizeof o.big, "SETUP?");
      copy(o.l1, sizeof o.l1, "restarts into the");
      copy(o.l2, sizeof o.l2, "setup menu");
      copy(o.bar, sizeof o.bar, "HOLD: YES   TAP: NO");
      return;
    case Scr::Locked: {
      o.kind = ObKind::Text;
      copy(o.big, sizeof o.big, "AF LOCKED");
      const uint32_t left = due(now, lock_until_) ? 0 : lock_until_ - now;
      snprintf(o.right, sizeof o.right, "%lus", (unsigned long)((left + 999) / 1000));
      const uint32_t total = (uint32_t)s_.aflock_s * 1000;
      o.progress = (int16_t)(total ? (uint64_t)left * 1000 / total : 0);
      copy(o.l1, sizeof o.l1, "tap fires at once");
      copy(o.l2, sizeof o.l2, "auto release at 0");
      copy(o.bar, sizeof o.bar, "TAP: FIRE HOLD: UNLOCK");
      return;
    }
    case Scr::TimerRun: {
      o.kind = ObKind::Count;
      const uint32_t left = due(now, timer_end_) ? 0 : timer_end_ - now;
      snprintf(o.big, sizeof o.big, "%lu", (unsigned long)((left + 999) / 1000));
      if (timer_af_) copy(o.l1, sizeof o.l1, "AF ON");
      if (timer_fired_) copy(o.l1, sizeof o.l1, "SHOT SENT");
      copy(o.bar, sizeof o.bar, "ANY PRESS: STOP");
      return;
    }
    case Scr::IntervalRun: {
      o.kind = ObKind::Text;
      if (iv_count_) snprintf(o.big, sizeof o.big, "%u/%u", iv_k_, iv_count_);
      else snprintf(o.big, sizeof o.big, "%u", iv_k_);
      const uint32_t next = due(now, iv_next_) ? 0 : iv_next_ - now;
      snprintf(o.right, sizeof o.right, "next %lus", (unsigned long)((next + 999) / 1000));
      if (iv_count_) o.progress = (int16_t)((uint32_t)iv_k_ * 1000 / iv_count_);
      else o.progress = (int16_t)((uint64_t)(now - iv_start_) * 1000 / (PLAN_MAX_RUN_S * 1000UL));
      snprintf(o.l1, sizeof o.l1, "ok %u fail %u skip %u", iv_ok_, iv_fail_, iv_skip_);
      if (e.relay) {
        const uint16_t used = e.hh_permille > e.cam_permille ? e.hh_permille : e.cam_permille;
        snprintf(o.l2, sizeof o.l2, "AIR %u%% left", used >= 1000 ? 0 : (1000 - used) / 10);
      } else if (iv_unk_) {
        snprintf(o.l2, sizeof o.l2, "unknown %u", iv_unk_);
      }
      copy(o.bar, sizeof o.bar, "ANY PRESS: STOP");
      return;
    }
    case Scr::BurstRun:
      o.kind = ObKind::Count;
      snprintf(o.big, sizeof o.big, "%lu.%lu", (unsigned long)((now - run_start_) / 1000),
               (unsigned long)((now - run_start_) / 100 % 10));
      copy(o.l1, sizeof o.l1, "SHUTTER ON");
      o.progress = (int16_t)((uint64_t)(now - run_start_) * 1000 / (run_end_ - run_start_ ? run_end_ - run_start_ : 1));
      if (o.progress > 1000) o.progress = 1000;
      copy(o.bar, sizeof o.bar, "ANY PRESS: STOP");
      return;
    case Scr::BulbOpen: {
      o.kind = ObKind::Count;
      fmt_clock(o.big, sizeof o.big, (now - run_start_) / 1000);
      fmt_clock(a, sizeof a, (run_end_ - run_start_) / 1000);
      snprintf(o.l1, sizeof o.l1, "OPEN  of %s", a);
      o.progress = (int16_t)((uint64_t)(now - run_start_) * 1000 / (run_end_ - run_start_ ? run_end_ - run_start_ : 1));
      if (o.progress > 1000) o.progress = 1000;
      copy(o.bar, sizeof o.bar, "ANY PRESS: CLOSE NOW");
      return;
    }
    case Scr::Home: break;
  }

  if (msg_.on) {
    o.kind = ObKind::Text;
    copy(o.big, sizeof o.big, msg_.big);
    copy(o.l1, sizeof o.l1, msg_.l1);
    copy(o.l2, sizeof o.l2, msg_.l2);
    copy(o.l3, sizeof o.l3, msg_.l3);
    return;
  }
  if (view_ > 0) {
    o.kind = ObKind::Home;  // the app draws the view page; only the bar is ours
    return;
  }
  if (mode_ == ObMode::Video && (rec_ || rec_unknown_)) {
    o.kind = ObKind::Text;
    if (rec_unknown_) {
      copy(o.big, sizeof o.big, "REC ?");
      copy(o.l1, sizeof o.l1, "REC STATE UNKNOWN:");
      copy(o.l2, sizeof o.l2, "check the camera");
    } else {
      fmt_clock(a, sizeof a, (now - rec_since_) / 1000);
      snprintf(o.big, sizeof o.big, "REC? %s", a);
      copy(o.l1, sizeof o.l1, "we sent start; check");
      copy(o.l2, sizeof o.l2, "the camera");
    }
    return;
  }
  o.kind = ObKind::Home;
}
