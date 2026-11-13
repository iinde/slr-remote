// Handheld role. Buttons -> camera commands, either straight over BLE (Direct) or as
// authenticated LoRa frames to the camera board (Relay). The screen only ever claims
// what came back: Sent -> Delivered (LoRa Ack) -> Accepted (BLE write acked) -> CAM EVT.
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <math.h>
#include <string.h>

#include "app.h"
#include "airtime.h"
#include "app_internal.h"
#include "bootgest.h"
#include "buttons.h"
#include "cam_exec.h"
#include "config.h"
#include "duty.h"
#include "utc.h"
#include "fuji.h"
#include "geo.h"
#include "gps.h"
#include "hw.h"
#include "ledcue.h"
#include "linebuf.h"
#include "linkmgr.h"
#include "modes.h"
#include "press.h"
#include "proto.h"
#include "radio.h"
#include "rangelog_fmt.h"
#include "store.h"
#include "ui.h"

namespace {

constexpr uint8_t MAX_TRIES = 4;              // first send + 3 retries
constexpr uint32_t TX_DEADLINE_MS = 3000;     // at the queue head and not on air by then = fail
constexpr uint32_t MAX_QUEUE_WAIT_MS = 15000;  // cap on waiting behind other commands
constexpr uint32_t RELEASE_GIVE_UP_MS = 30000;  // Release can't fire anything: keep trying
constexpr uint32_t TX_FAIL_BACKOFF_MS = 250;  // after a failed transmit: never busy-loop the radio
constexpr uint32_t RADIO_REINIT_GAP_MS = 10000;  // K5: between radio re-inits while TX faults last
constexpr uint16_t CAM_DUTY_HELD_PERMILLE = 750;  // camera board near its 80 % ping-Ack cut-off
constexpr uint32_t CAM_DUTY_HELD_MAX_MS = 600000;  // trust that report this long at most
// X13: only this close to its limit can the camera board fail to Ack a command (priority Ack).
constexpr uint16_t CAM_DUTY_SPENT_PERMILLE = 950;
constexpr uint16_t HUNT_DUTY_STOP_PERMILLE = 700;
constexpr uint32_t PEER_CTR_SKIP = 256;       // past a rebooted camera's counter reservation
constexpr uint32_t ANTENNA_HINT_MS = 6000;
constexpr uint8_t EVQ = 8;
// One-button press events: a press is 2 to 4 events, and an SF12 transmit (1.5 to 2 s) can
// hold several presses. If this still fills, ob_inputs() resyncs the engine with the button.
constexpr uint8_t PEVQ = 32;
// Delivered, no Result: stop waiting. Camera board worst case: focus, shutter and release
// writes at up to 2 s each (fuji CMD_WAIT_MS) + the shutter hold; relay adds 2 Result airtimes.
constexpr uint32_t RESULT_WAIT_MS = 3 * 2000 + timing::SHUTTER_HOLD_MS;
constexpr uint32_t DEFER_SHOT_MS = 10000;     // direct: wait this long for the BLE link
constexpr uint32_t CAM_FIX_FRESH_MS = 60000;
constexpr uint32_t UI_PERIOD_MS = 100;
constexpr uint32_t BATT_PERIOD_MS = 10000;
constexpr uint32_t GPS_PERIOD_MS = 250;
constexpr uint32_t TOAST_MS = 2000;
constexpr uint32_t FIXED_SF_RETRY_MS = 10000;
// K1: wait before going back to a fixed SF that just lost the link; doubles per failure.
constexpr uint32_t FIXED_SF_BACKOFF_MS = 30000;
constexpr uint32_t FIXED_SF_BACKOFF_MAX_MS = 600000;
constexpr uint8_t ACK_FRAME_LEN = PROTO_HDR + ACK_PAYLOAD_LEN + PROTO_TAG;
constexpr uint8_t PING_FRAME_LEN = PROTO_HDR + PING_PAYLOAD_LEN + PROTO_TAG;
constexpr uint8_t QCAP = 8;      // the last slot is kept for Release / Sleep
constexpr uint8_t LOGQ = 4;      // range-log rows waiting for an idle moment to be written
// The camera board's own worst case for one command (fuji CMD_WAIT_MS per BLE write).
constexpr uint32_t CAM_CMD_WAIT_MS = 2000;
constexpr uint32_t STUCK_SWITCH_MS = 120000;  // two-stage contact held this long = stuck
constexpr uint32_t STUCK_SLEEP_GRACE_MS = 5000;  // shutdown: then sleep with a pin still LOW
// S7: direct, OFF with a Release the camera has not acknowledged: keep retrying this long
// (time for the BLE link to come back) before sleeping anyway.
constexpr uint32_t DIRECT_RELEASE_WAIT_MS = 15000;
constexpr uint32_t WAKE_STAB_UP_MS = 30;

// Survive deep sleep (zeroed at power-on): a rescue sweep continues across wakes, and the
// "peer SF unconfirmed" flag set when a Sleep frame went unanswered.
RTC_DATA_ATTR uint8_t rtc_rescue_i = 0;
RTC_DATA_ATTR bool rtc_sf_unconfirmed = false;
// An SF the camera board may have switched to unseen (our SetSf went out, its Ack never came).
RTC_DATA_ATTR uint8_t rtc_pending_sf = 0;
// K1: back-off before returning to a fixed SF that failed (0 = none), and the fixed SF it
// belongs to. Kept across deep sleep, or every wake would flap back to the dead SF once.
RTC_DATA_ATTR uint32_t rtc_fixed_backoff = 0;
RTC_DATA_ATTR uint8_t rtc_fixed_for = 0;
// K1: awake time already waited of that back-off when we went to sleep. Without it every wake
// started the wait again and a press-shoot-sleep user never got back to the fixed SF.
RTC_DATA_ATTR uint32_t rtc_fixed_waited = 0;
// Adaptive-SF step-down streak: a wake/press/sleep cycle gives one or two samples, so the
// streak must outlive deep sleep or the link never climbs back down from SF12.
RTC_DATA_ATTR bool rtc_link_valid = false;
RTC_DATA_ATTR LinkManager::State rtc_link;
// Page and range-log state survive idle sleep: a range walk wakes every minute or two, and
// each wake press must be logged like the last one (and ROWS/SUCCESS keep counting).
RTC_DATA_ATTR uint8_t rtc_page_i = 0;
RTC_DATA_ATTR bool rtc_range_toggle = false;
RTC_DATA_ATTR uint16_t rtc_range_n = 0, rtc_range_ok = 0;
// One button: every mode survives deep sleep (spec 1.3); a power-on keeps only SHOT/AF LOCK.
RTC_DATA_ATTR bool rtc_ob_valid = false;
RTC_DATA_ATTR uint8_t rtc_ob_mode = 0;
RTC_DATA_ATTR bool rtc_ob_rec = false;  // VIDEO: a recording may be running on the camera
constexpr uint16_t FOCUS_DEFAULT = 0xFFFF;  // command(): use Settings.focus_ms
constexpr uint32_t DIM_AFTER_MS = 15000;    // a run on a quiet screen: dim, then dark
constexpr uint32_t DARK_AFTER_MS = 60000;
constexpr uint8_t DIM_CONTRAST = 8;

const Page RELAY_PAGES[] = {Page::Main, Page::Link, Page::Hunt, Page::Gps, Page::Range};
const Page DIRECT_PAGES[] = {Page::Main, Page::Link, Page::Gps, Page::Range};

struct TxCmd {
  MsgType type;
  uint8_t pl[6];     // Shot: focus u16 + echo u32; Focus: echo u32; Shoot: echo u32
                     // [+ u16 max hold] (echo filled per try)
  uint8_t len;
  bool prio;
  bool has_seq;
  uint16_t seq;
  uint32_t id;      // full counter of the first send (Acks/Results must match it)
  uint8_t tries;
  uint8_t fed_at;   // Release: tries count at which a missed block was last fed to the link
  bool headed;      // has been the queue head (head_since valid)
  bool duty_refused;  // the last attempt was refused by the duty governor
  bool reask;        // NO RESULT follow-up: same id resent, the camera answers from its cache
  uint32_t queued_at, last_tx, head_since, max_wait;
  uint16_t track;  // 0 = not a tracked shot
};

// One user press (Shot / Shoot / Focus) from press to final stage.
struct Track {
  uint16_t id = 0;
  bool open = false;
  bool late = false;
  MsgType type = MsgType::Shot;
  bool has_seq = false;
  uint16_t seq = 0;
  uint32_t cmd_id = 0;  // full id of the command (relay), for matching Acks/Results
  ShotStage stage = ShotStage::Idle;
  uint8_t err = 0;
  uint32_t press_ms = 0, stage_ms = 0;
  uint16_t rtt = 0;
  uint8_t tries = 0, sf = 0;
  int8_t tx_dbm = 0, ble = 0;
  bool counted = false;  // counted in shot_count (reached Sent)
  bool reasked = false;  // a NO RESULT re-ask was queued
  bool refreshed = false;  // resent once with a new id after the camera board said NEED_FRESH
  bool range = false;    // range logging was on when the press was made (its row is written)
  bool duty = false;     // never sent: the duty governor refused it
  uint16_t focus = 500;  // Shot: the focus wait it was sent with (resends use the same)
  uint16_t hold = 0;     // Shoot: max hold ms (camera board lets go by itself), 0 = held
  float rssi_up = NAN, snr_up = NAN, rssi_dn = NAN, snr_dn = NAN;
};

struct PingState {
  bool active = false;
  uint16_t seq = 0;
  uint32_t id = 0;
  uint32_t tx_at = 0;
};

struct Deferred {
  bool active = false;
  uint32_t at = 0;
  uint16_t track = 0;
  MsgType type = MsgType::Shot;
  uint16_t focus = 500;
  uint16_t hold = 0;  // Shoot: max hold ms (0 = held until Release)
};

struct Handheld {
  Settings* s = nullptr;
  bool relay = false, paired = false, radio_ok = false;
  PairRecord pair;
  ReplayGuard guard;
  LinkManager link{7};
  CamExec exec;
  ClickButton prg;
  TwoStage two;
  bool held = false;  // a Focus/Shoot is out without its Release
  bool held_full = false;  // S6: that hold is a two-stage full press (its FullUp may be dropped)
  bool swallow_prg = false, swallow_long = false;
  bool wake_click = false;     // PRG woke us and was still down: a Shot unless it becomes Long
  bool prg_seen_down = false;
  uint32_t prg_up_since = 0;
  bool suppress_half = false;  // the HALF contact of a wake stab already fired its Shot
  bool suppress_full = false;  // ... and so did its FULL contact (until everything is up)
  bool wake_full = false;      // FULL woke us and was still down: TwoStage must report it
  uint32_t wake_full_up_since = 0;
  uint8_t stage_level = 0;     // two-stage level last seen by idle_check
  uint32_t stage_level_since = 0;
  bool stuck = false;          // two-stage contact held unchanged too long: ignored
  uint32_t release_ms = 0;     // last two-stage Release (a held outcome follows it)
  // Button events caught inside a blocking radio_send (see sample_buttons).
  BtnEvent prg_ev[EVQ];
  StageEvent st_ev[EVQ];
  volatile uint8_t prg_n = 0, st_n = 0;
  bool release_warned = false;

  TxCmd q[QCAP];
  uint8_t qn = 0;
  PingState ping;
  uint32_t last_ping_try = 0, last_fixed_try = 0, last_tx_ok = 0;
  bool rescue = false;
  uint8_t rescue_i = 0;
  bool probe_now = false;   // rescue just started: send the first probe at once
  bool rescue_early = false;  // K3: the next SF12 probe after the first may go early
  bool ping_gate = false;   // power-on: no automatic pings until the user does something
  bool hunt_slowed = false;
  bool sf_unconfirmed = false;  // camera board may have dropped to SF12 unseen
  bool ctr_skipped = false;     // counter already skipped in this silence episode
  uint8_t pending_sf = 0;       // SetSf sent, Ack never heard: the camera board may be there
  uint8_t sf_wanted = 0;        // adaptive SF change held back while a press is running
  uint32_t last_rx_ok = 0;      // last authenticated frame from the camera board
  uint32_t tx_backoff_until = 0;
  bool link_asked = false;      // a ping or command went unanswered past its Ack slot this boot
  uint32_t reask_guard_until = 0;  // a dropped in-flight re-ask's answer may be on air until then
  bool duty_quiet_sent = false;    // our own duty holds pings: told the camera board (Sleep)
  bool cam_duty_doubt = false;     // X13: a command went unanswered: duty does not explain it
  bool radio_reinit = false;       // K5: the radio was re-initialised in this run of TX faults
  uint32_t radio_reinit_at = 0;    // K5: when, so a repeat waits RADIO_REINIT_GAP_MS
  uint32_t last_rel_id = 0;        // id of the last Release on air (its Result is matched)

  Track t;
  uint16_t next_track = 1, shot_count = 0;
  Deferred defer;

  // peer status from the latest Ack/Result
  uint32_t last_ack = 0;
  CamLink cam = CamLink::None;
  int8_t cam_ble = 0;
  uint8_t cam_batt = 255;
  uint16_t cam_duty = 0;
  bool cam_fix = false;
  uint32_t cam_fix_at = 0;
  double cam_lat = 0, cam_lon = 0;
  float rssi_up = NAN, snr_up = NAN, rssi_dn = NAN, snr_dn = NAN;
  uint16_t rtt = 0;
  int16_t hunt_rssi = 0, hunt_best = -200;

  uint8_t page_i = 0;
  bool range_toggle = false;
  uint16_t range_n = 0, range_ok = 0;
  RangeRow logq[LOGQ];
  bool logq_ok[LOGQ];
  uint8_t logq_n = 0;

  GpsFix fix{};
  uint32_t last_gps = 0;
  uint8_t batt = 255;
  uint32_t last_batt = 0;

  uint32_t last_activity = 0, last_ui = 0;
  const char* toast = nullptr;
  uint32_t toast_until = 0;

  bool shutting = false;
  bool shut_idle = false;      // C2: this shutdown is the idle SLEEP one: a press cancels it
  uint32_t shut_deadline = 0;
  const char* shut_msg = "";

  // One button (press style A, B or C; docs/specs/2026-11-08-one-button-ux.md). Classic and
  // the two-stage switch keep the bench v0.1 paths above, untouched.
  bool ob = false;
  ModeEngine eng;
  PressClassifier pc;          // PRG
  PressClassifier auxpc;       // R2/R2b second button on GPIO6
  bool aux_on = false, aux_swallow = false;
  PressEvent pev[PEVQ], aev[PEVQ];
  volatile uint8_t pev_n = 0, aev_n = 0;
  LedCue led;
  bool led_was = false;
  uint16_t eng_tag = 0, eng_track = 0;  // the engine's last Shot/Shoot and the track it opened
  bool eng_reported = true;
  uint32_t last_press = 0;
  uint8_t dim = 0;             // 0 on, 1 dimmed, 2 dark
  bool rel_alert = false;      // RELEASE NOT CONFIRMED, until a press
  bool rel_pending_was = false;
  bool fuji_was_ready = false;
  ObViewWatch views;           // the engine's view last seen (HUNT best, view-page bar)
};

Handheld g;

// ---------- small helpers ----------

bool after(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

void show_toast(const char* msg, uint32_t now) {
  g.toast = msg;
  g.toast_until = now + TOAST_MS;
}

const Page* pages(uint8_t& n) {
  if (g.relay) {
    n = sizeof(RELAY_PAGES) / sizeof(RELAY_PAGES[0]);
    return RELAY_PAGES;
  }
  n = sizeof(DIRECT_PAGES) / sizeof(DIRECT_PAGES[0]);
  return DIRECT_PAGES;
}

Page page() {
  uint8_t n;
  const Page* p = pages(n);
  return p[(g.ob ? g.eng.view() : g.page_i) % n];
}

bool range_on() {
  if (g.s->rlog_always) return true;  // 'rlog on': every shot is logged, whatever the page
  return g.s->two_stage ? g.range_toggle : page() == Page::Range;
}

uint32_t ack_timeout(uint8_t frame_len) {
  return radio_airtime_ms(frame_len) + radio_airtime_ms(ACK_FRAME_LEN) + 150;
}

// Sized from the round trip (our ping + the camera's ack), shared with the camera's
// silence rule (linkmgr.h). Hunt pings faster, but never past 70 % of either board's budget.
uint32_t normal_ping_interval() {
  const uint32_t iv = ping_interval_ms(radio_airtime_ms(PING_FRAME_LEN), radio_airtime_ms(ACK_FRAME_LEN),
                                       timing::PING_MIN_INTERVAL_MS);
  // On a slow SF with a strong link (just after a rescue or reboot) check again soon, so the
  // step back down to a fast SF takes seconds, not a minute per check. More pings never
  // trigger the camera board's silence rule.
  // X12: only when adaptive. On a fixed SF no step down ever ends this, and 8 s pings at
  // SF11/SF12 run both boards into their duty limit.
  if (g.s->adaptive_sf && radio_sf() >= 10 && g.link.lastMarginDb() > 20.f && iv > 8000)
    return 8000;
  return iv;
}

bool hunt_duty_ok() {
  return duty_permille() < HUNT_DUTY_STOP_PERMILLE && g.cam_duty < HUNT_DUTY_STOP_PERMILLE;
}

uint32_t ping_interval() {
  if (page() == Page::Hunt && hunt_duty_ok())
    return hunt_interval_ms(radio_airtime_ms(PING_FRAME_LEN), radio_airtime_ms(ACK_FRAME_LEN),
                            timing::HUNT_PING_INTERVAL_MS);
  return normal_ping_interval();
}

uint32_t airtime_at(uint8_t sf, uint8_t len) {
  return lora_airtime_ms(sf, radiocfg::BW_KHZ, radiocfg::CR_DENOM, radiocfg::PREAMBLE, len);
}

// The camera board refuses non-priority Acks (pings) above 80 % of its hour. While it last
// said it is near that, an unanswered ping is the camera saving airtime, not a lost link.
// X13: this is for pings only and a failed command does not end it. Above its 80 % line the
// camera board is silent to pings on purpose, so un-excusing them after one lost command
// forced an SF12 rescue on a working link. A real loss is found by commands (cmd_failed).
bool cam_duty_held(uint32_t now) {
  return g.last_ack && g.cam_duty >= CAM_DUTY_HELD_PERMILLE &&
         now - g.last_ack < CAM_DUTY_HELD_MAX_MS;
}

// X13: commands are Acked with priority up to the camera board's full budget, so only a
// report this close to the limit can explain an unanswered command. Below it, it is a loss.
// Not once a command has gone unanswered since that report (cam_duty_doubt).
bool cam_duty_spent(uint32_t now) {
  return cam_duty_held(now) && !g.cam_duty_doubt && g.cam_duty >= CAM_DUTY_SPENT_PERMILLE;
}

uint32_t result_wait_ms() {
  uint32_t w = RESULT_WAIT_MS + g.s->focus_ms;
  if (g.relay && g.radio_ok) w += 2 * radio_airtime_ms(ACK_FRAME_LEN);
  return w;
}

// What the camera board waits before it decides we are gone and drops to SF12.
uint32_t camera_silence_ms() {
  return rescue_silence_ms(normal_ping_interval(), radio_airtime_ms(PING_FRAME_LEN),
                           timing::CAM_RESCUE_SILENCE_MS);
}

bool tx_holdoff(uint32_t now) {
  // L1: no hold-off on a deep-sleep wake (hw_tx_holdoff_ms is 0 then): the wake press goes now.
  return now - hw_boot_ms() < hw_tx_holdoff_ms();
}

void persist_sf(uint8_t sf) {
  if (g.s->last_sf == sf) return;
  g.s->last_sf = sf;
  store_save(*g.s);
}

// ---------- shot tracking + range log ----------

bool final_stage(ShotStage s) { return s == ShotStage::Failed || s == ShotStage::CamEvent; }

// "No Ack" only means we heard nothing back; a later Ack/Result from the camera board is
// proof that it got the command, so it may still overturn the failure.
bool provisional_fail(const Track& t) {
  return t.stage == ShotStage::Failed && t.err == apperr::NO_ACK;
}

void set_stage(ShotStage st, uint8_t err, uint32_t now) {
  Track& t = g.t;
  if (provisional_fail(t) && (st == ShotStage::Delivered || st == ShotStage::Accepted ||
                              st == ShotStage::CamEvent)) {
    t.late = true;
    t.stage = st;
    t.err = 0;
    t.stage_ms = now;
    show_toast("LATE: CAM GOT IT", now);
    return;
  }
  if (provisional_fail(t) && st == ShotStage::Failed && err != apperr::NO_ACK) {
    t.late = true;  // the camera board did get it; now we know why it failed
    t.err = err;
    t.stage_ms = now;
    return;
  }
  // Reported "NO RESULT" (unknown), then the outcome came after all: show it, marked late.
  if (t.stage == ShotStage::Delivered && t.err == apperr::NO_RESULT &&
      (st == ShotStage::Accepted || st == ShotStage::CamEvent || st == ShotStage::Failed)) {
    t.late = true;
    t.stage = st;
    t.err = err;
    t.stage_ms = now;
    return;
  }
  // A shutter write that ended Unsure (sent, never confirmed), then the camera notified: it
  // fired after all. Shown as CAM EVT, marked late; never as ACCEPTED (the write was not).
  if (t.stage == ShotStage::Failed && t.err == (uint8_t)FujiErr::Unsure &&
      st == ShotStage::CamEvent) {
    t.late = true;
    t.stage = st;
    t.err = 0;
    t.stage_ms = now;
    return;
  }
  if (final_stage(t.stage)) return;
  if (st != ShotStage::Failed && (uint8_t)st <= (uint8_t)t.stage) return;
  t.stage = st;
  t.err = err;
  t.stage_ms = now;
  // "#N" counts presses that went to the camera (on air / BLE write started), not ones that
  // were never sent (duty, hold-off, queue, cancelled, no camera).
  if (st == ShotStage::Sent && t.type != MsgType::Focus && !t.counted) {
    t.counted = true;
    g.shot_count++;
  }
}

bool unknown_outcome(const Track& t) {
  return t.err == apperr::NO_ACK || t.err == apperr::NO_RESULT || t.err == apperr::STALE ||
         t.err == (uint8_t)FujiErr::Unsure;
}

// Range log: rows are built when a track closes but written later, from loop() when nothing
// is queued or held (flush_log), so a LittleFS erase never lands in a press's path.
void log_row(const Track& t) {
  if (!t.range) return;  // as at press time: a page change before the track closes is not a stop
  if (g.logq_n >= LOGQ) {
    show_toast("LOG BUSY: ROW LOST", millis());
    return;
  }
  RangeRow r{};
  r.t_s = millis() / 1000;
  r.mode = g.relay ? 1 : 0;
  r.sf = g.relay ? t.sf : 0;
  r.tx_dbm = g.relay ? t.tx_dbm : 0;
  r.hh_lat = g.fix.valid ? g.fix.lat : NAN;
  r.hh_lon = g.fix.valid ? g.fix.lon : NAN;
  const bool cam_fix = g.relay && g.cam_fix;
  r.cam_lat = cam_fix ? g.cam_lat : NAN;
  r.cam_lon = cam_fix ? g.cam_lon : NAN;
  r.dist_m = (g.fix.valid && cam_fix) ? geo_distance_m(g.fix.lat, g.fix.lon, g.cam_lat, g.cam_lon)
                                      : NAN;
  r.rssi_up = t.rssi_up;
  r.snr_up = t.snr_up;
  r.rssi_dn = t.rssi_dn;
  r.snr_dn = t.snr_dn;
  r.ble_rssi = t.ble;
  r.rtt_ms = t.rtt;
  r.retries = t.tries > 0 ? t.tries - 1 : 0;
  // RANGE_STAGE_UNKNOWN: may or may not have fired (no reply, no result, unconfirmed write).
  const bool unknown = t.stage != ShotStage::Idle && unknown_outcome(t);
  r.stage = unknown ? RANGE_STAGE_UNKNOWN : (uint8_t)t.stage;
  g.logq[g.logq_n] = r;
  g.logq_ok[g.logq_n] = !unknown && (t.stage == ShotStage::Delivered ||
                                     t.stage == ShotStage::Accepted ||
                                     t.stage == ShotStage::CamEvent);
  g.logq_n++;
}

// Documented exception to the 50 ms loop rule: a LittleFS append (and, every 256 KB, a
// rotation) can take longer. It only runs when no command is queued, nothing is held and no
// shot is running.
void write_log_row(uint32_t now) {
  if (!g.logq_n) return;
  char line[200];
  const size_t n = range_csv_row(g.logq[0], line, sizeof line);
  if (n && rlog_append(line)) {
    g.range_n++;
    if (g.logq_ok[0]) g.range_ok++;
  } else {
    show_toast("LOG FAILED", now);
  }
  for (uint8_t i = 1; i < g.logq_n; i++) {
    g.logq[i - 1] = g.logq[i];
    g.logq_ok[i - 1] = g.logq_ok[i];
  }
  g.logq_n--;
}

void flush_log(uint32_t now) {
  if (!g.logq_n || g.qn || g.held || g.exec.busy() || g.defer.active || g.shutting) return;
  write_log_row(now);
}

void close_track() {
  if (!g.t.open) return;
  g.t.open = false;
  if (g.t.type != MsgType::Focus) log_row(g.t);
}

void ob_report(bool force, uint32_t now);

uint16_t open_track(MsgType type, uint32_t now) {
  if (g.ob) ob_report(true, now);  // the engine's last shot gets its outcome before it goes
  if (g.t.open) {
    if (g.t.type == MsgType::Focus && type == MsgType::Shoot) g.t.open = false;  // same press
    else close_track();
  }
  Track t;
  t.id = g.next_track++;
  if (g.next_track == 0) g.next_track = 1;
  t.open = true;
  t.type = type;
  t.press_ms = t.stage_ms = now;
  t.sf = g.radio_ok ? radio_sf() : 0;
  t.tx_dbm = g.radio_ok ? radio_tx_dbm() : 0;
  t.range = range_on();
  g.t = t;
  // A NO RESULT re-ask of the previous press must not delay this one (stop-and-wait). One
  // already on air may be answered right now (half duplex): keep off the air until its Ack
  // slot has passed (relay_tx), and enqueue() counts that wait in the new press's max_wait.
  for (uint8_t i = g.qn; i-- > 0;)
    if (g.q[i].reask) {
      if (g.q[i].tries > 0) {
        const uint32_t end =
            g.q[i].last_tx + ack_timeout((uint8_t)(PROTO_HDR + g.q[i].len + PROTO_TAG));
        if ((int32_t)(end - g.reask_guard_until) > 0) g.reask_guard_until = end;
      }
      for (uint8_t k = i + 1; k < g.qn; k++) g.q[k - 1] = g.q[k];
      g.qn--;
    }
  return t.id;
}

bool reask_queued(uint16_t track) {
  for (uint8_t i = 0; i < g.qn; i++)
    if (g.q[i].reask && g.q[i].track == track) return true;
  return false;
}

bool enqueue_reask(const Track& t, uint32_t now);

void track_step(uint32_t now) {
  Track& t = g.t;
  if (!t.open) return;
  // Two-stage Focus/Shoot: the camera board sends the outcome after the Release (it stays
  // quiet while the button is down), so never time the track out while still held.
  const bool hold_track = t.type == MsgType::Focus || t.type == MsgType::Shoot;
  // A provisional NO REPLY too: the real outcome is sent after the Release, still to come.
  if (hold_track && g.held && (!final_stage(t.stage) || provisional_fail(t))) return;
  uint32_t ref = t.stage_ms;
  if (hold_track && g.release_ms && (int32_t)(g.release_ms - ref) > 0) ref = g.release_ms;
  // C1: direct outcomes are stamped after a blocking write, later than this loop's now: that
  // is 0 ms ago, not 49 days (the unsigned difference would close the track at once).
  const uint32_t since = (int32_t)(now - ref) > 0 ? now - ref : 0;
  const uint32_t grace = g.relay ? 2 * radio_airtime_ms(ACK_FRAME_LEN) + 500 : 500;
  const uint32_t result_wait = result_wait_ms();
  bool done = final_stage(t.stage);
  if (provisional_fail(t) && g.relay) done = since > result_wait;  // a late Result may come
  // Unsure shutter write: a camera notification may still show it fired (set_stage).
  if (t.stage == ShotStage::Failed && t.err == (uint8_t)FujiErr::Unsure)
    done = since > timing::CAM_EVENT_WINDOW_MS + grace;
  if (t.stage == ShotStage::Accepted && since > timing::CAM_EVENT_WINDOW_MS + grace) done = true;
  if (t.stage == ShotStage::Delivered && since > result_wait) {
    // At the camera board, outcome never heard: say so rather than leave "AT CAM" (or the
    // "FOCUS" arrow) up, but keep listening one more wait: a late Result still overturns it
    // (set_stage).
    if (t.err != apperr::NO_RESULT) {
      t.err = apperr::NO_RESULT;
      t.stage_ms = now;
      show_toast(t.type == MsgType::Focus ? "AF: NO RESULT" : "NO RESULT", now);
      // Ask again with the same command id: the camera board dedupes it (never runs it
      // twice) and answers from its cached Result.
      // X21: not while shutting down. A re-ask heard after our Sleep would wake the camera
      // board's silence watch again, and it would drop to SF12 while we sleep.
      if (g.relay && t.type != MsgType::Focus && !t.reasked && !g.shutting)
        t.reasked = enqueue_reask(t, now);
    } else {
      done = true;
    }
  }
  if (done && reask_queued(t.id)) done = false;  // its answer may still overturn NO RESULT
  if (done) close_track();
}

// A relay press is waiting for the camera board's outcome: its Result(s) may go on air at any
// moment, and anything we transmit then is lost and loses them (half duplex).
bool awaiting_outcome(uint32_t now) {
  const Track& t = g.t;
  if (!g.relay || !t.open || final_stage(t.stage) || t.err == apperr::NO_RESULT) return false;
  if (t.stage != ShotStage::Sent && t.stage != ShotStage::Delivered &&
      t.stage != ShotStage::Accepted)
    return false;
  // Two-stage: the camera board holds the outcome back until the Release, so time it from
  // there (as track_step does), and it is always due while the switch is held.
  const bool hold_track = t.type == MsgType::Focus || t.type == MsgType::Shoot;
  if (hold_track && g.held) return true;
  uint32_t ref = t.stage_ms;
  if (hold_track && g.release_ms && (int32_t)(g.release_ms - ref) > 0) ref = g.release_ms;
  // C1: a stamp later than now counts as 0 ms ago (as track_step).
  return (int32_t)(now - ref) <= 0 || now - ref <= result_wait_ms();
}

// The two-stage switch is held (and not stuck).
bool switch_active() { return g.s->two_stage && g.two.level() != 0 && !g.stuck; }

// A press is in progress: no pings and no adaptive SF change now. Pings would hold a Shoot or
// Release behind their Ack slot and collide with Results; an SF change mid-shot can split the
// two ends while the camera board is busy in a BLE write.
bool link_quiet(uint32_t now) {
  if (g.held || switch_active()) return true;
  if (g.release_ms && now - g.release_ms < 2 * radio_airtime_ms(ACK_FRAME_LEN) + 500) return true;
  return awaiting_outcome(now);
}

// ---------- relay: frames ----------

// Outcome of one transmit attempt.
//  Sent: on air.  Lost: the transmit started (booked) but TX_DONE never came; treat it as a
//  try (it may have gone out).  Duty: the governor refused it.  NotNow: hold-off, radio down,
//  back-off after a failed transmit; nothing left the board and no counter was used.
enum class Tx : uint8_t { Sent, Lost, Duty, NotNow };

bool on_air(Tx r) { return r == Tx::Sent || r == Tx::Lost; }

// Sends one authenticated frame. Assigns seq (and the full id) from the counter on first use
// so seqs never repeat across deep-sleep reboots (the camera dedupes on the full id).
Tx send_frame(MsgType type, uint16_t& seq, bool& has_seq, uint32_t& id, const uint8_t* pl,
              uint8_t len, bool prio, uint32_t now) {
  if (!g.radio_ok || tx_holdoff(now)) return Tx::NotNow;
  // Power-on antenna gate: nothing at all goes out until the user has touched a button (every
  // user-input path opens the gate first), so no automatic path can get round it.
  if (g.ping_gate) return Tx::NotNow;
  if ((int32_t)(now - g.tx_backoff_until) < 0) return Tx::NotNow;
  const uint8_t flen = PROTO_HDR + len + PROTO_TAG;
  const uint32_t air = radio_airtime_ms(flen);
  if (!duty_can_send(air, prio)) return Tx::Duty;
  Frame f{};
  f.type = type;
  f.net = g.pair.net;
  f.ctr = store_next_ctr();
  if (f.ctr == 0) {  // counter block not persisted: never reuse a counter after a reboot
    show_toast("STORAGE FAIL", now);
    g.tx_backoff_until = millis() + TX_FAIL_BACKOFF_MS;
    return Tx::NotNow;
  }
  if (!has_seq) {
    seq = (uint16_t)f.ctr;
    id = f.ctr;
    has_seq = true;
  }
  f.seq = seq;
  f.len = len;
  if (len) memcpy(f.payload, pl, len);
  uint8_t buf[PROTO_MAX_FRAME];
  const size_t n = frame_encode(f, g.pair.key, buf, sizeof buf);
  if (!n) return Tx::NotNow;
  const RadioTx r = radio_send(buf, (uint8_t)n);
  if (!radio_tx_started(r)) {
    g.tx_backoff_until = millis() + TX_FAIL_BACKOFF_MS;
    return Tx::NotNow;
  }
  duty_record(air);  // every transmit that started is booked, TX_DONE or not
  if (r == RadioTx::TimedOut) {
    g.tx_backoff_until = millis() + TX_FAIL_BACKOFF_MS;
    return Tx::Lost;
  }
  g.last_tx_ok = millis();
  return Tx::Sent;
}

bool queued(MsgType type) {
  for (uint8_t i = 0; i < g.qn; i++)
    if (g.q[i].type == type) return true;
  return false;
}

void pop_head() {
  for (uint8_t i = 1; i < g.qn; i++) g.q[i - 1] = g.q[i];
  if (g.qn) g.qn--;
}

void remove_at(uint8_t k) {
  for (uint8_t i = k + 1; i < g.qn; i++) g.q[i - 1] = g.q[i];
  if (g.qn) g.qn--;
}

void remove_type(MsgType type) {
  uint8_t w = 0;
  for (uint8_t i = 0; i < g.qn; i++)
    if (g.q[i].type != type) g.q[w++] = g.q[i];
  g.qn = w;
}

bool must_deliver(MsgType t) { return t == MsgType::Release || t == MsgType::Sleep; }

void link_feed(bool acked, uint32_t rtt, uint32_t now);

void cmd_failed(const TxCmd& c, uint8_t err, uint32_t now) {
  // The camera board said it is near its duty limit: its Acks are refused, not lost. Not a
  // link failure (no miss, no counter skip, no rescue), and not an unconfirmed SF.
  // X13: only when it is all but out of duty; it Acks commands with priority until then.
  const bool cam_duty = cam_duty_spent(now);
  // X13: a real loss: the old duty report no longer excuses commands. Pings stay excused (the
  // camera board may only be saving airtime), so no ping miss will start a rescue: the next
  // command tries SF12 as well, in case the camera board dropped there unseen.
  if (err == apperr::NO_ACK && !cam_duty) {
    if (cam_duty_held(now)) g.sf_unconfirmed = true;
    g.cam_duty_doubt = true;
  }
  if (c.reask) {  // NO RESULT follow-up: the press already shows its (unknown) outcome
    if (err == apperr::NO_ACK && !cam_duty) link_feed(false, 0, now);
    return;
  }
  if (c.track && c.track == g.t.id) {
    g.t.duty = c.duty_refused;
    set_stage(ShotStage::Failed, err, now);
    const char* why = err == apperr::NO_ACK && cam_duty ? "NO REPLY: CAM DUTY"
                      : err == apperr::NO_ACK      ? "NO REPLY"  // may have fired: UNKNOWN
                      : err == apperr::QUEUED    ? "NOT SENT: LINK BUSY"
                      : err == apperr::CANCELLED ? "CANCELLED"
                      : c.duty_refused           ? "NOT SENT: DUTY"
                      : !g.radio_ok              ? "RADIO FAILED"
                                                 : "NOT SENT";
    show_toast(why, now);
  }
  if (c.type == MsgType::Release) {
    show_toast(cam_duty ? "RELEASE: CAM DUTY" : "RELEASE LOST", now);
    if (g.ob) g.rel_alert = true;  // the camera may still hold AF or the shutter
  }
  if (c.type == MsgType::Sleep) {
    // The camera board may have dropped to SF12 without hearing our Sleep: next wake, try
    // both. Not if it is only out of duty (it heard us and stays put). Never a link miss:
    // we are going to sleep, a rescue now would only burn airtime.
    if (!cam_duty) {
      rtc_sf_unconfirmed = true;
      // K2: a duty-quiet Sleep (we stay awake) went unanswered: the next command must try
      // SF12 as well, now, not only after the next wake.
      if (!g.shutting) g.sf_unconfirmed = true;
    }
    return;
  }
  // A SetSf the camera board may have acted on (its Ack lost): it may be on the new SF now.
  if (c.type == MsgType::SetSf && err == apperr::NO_ACK && c.len >= 1) {
    g.pending_sf = c.pl[0];
    rtc_pending_sf = g.pending_sf;
  }
  if (err == apperr::NO_ACK && !cam_duty) link_feed(false, 0, now);
}

// Release (and Sleep) must never be refused for space: other commands leave the last slot
// free, and if even that is taken the newest unsent press is dropped (it can no longer
// matter once the switch is released). Adjacent unsent Releases merge.
bool enqueue(MsgType type, const uint8_t* pl, uint8_t len, bool prio, uint16_t track,
             uint32_t now) {
  const bool must = must_deliver(type);
  // A press must not wait behind an adaptive SF change that has not gone out yet.
  if (prio && !must) {
    for (uint8_t i = g.qn; i-- > 0;)
      if (g.q[i].type == MsgType::SetSf && g.q[i].tries == 0) remove_at(i);
  }
  if (must && g.qn && g.q[g.qn - 1].type == type && g.q[g.qn - 1].tries == 0) return true;
  const uint8_t cap = must ? QCAP : QCAP - 1;
  if (g.qn >= cap && must) {
    for (uint8_t i = g.qn; i-- > 1;) {
      const MsgType t = g.q[i].type;
      if (g.q[i].tries != 0 ||
          (t != MsgType::Focus && t != MsgType::Shoot && t != MsgType::Shot))
        continue;
      const TxCmd dead = g.q[i];
      remove_at(i);
      cmd_failed(dead, apperr::CANCELLED, now);
      break;
    }
  }
  if (g.qn >= cap) {
    show_toast("QUEUE FULL", now);
    return false;
  }
  TxCmd c{};
  c.type = type;
  if (len) memcpy(c.pl, pl, len);
  c.len = len;
  c.prio = prio;
  c.queued_at = now;
  c.track = track;
  // Never fire late: allow for what is ahead (one retry each), then give up.
  uint32_t wait = TX_DEADLINE_MS;
  if (g.radio_ok) {
    for (uint8_t i = 0; i < g.qn; i++)
      wait += 2 * ack_timeout((uint8_t)(PROTO_HDR + g.q[i].len + PROTO_TAG));
    // A ping's Ack slot holds the queue (half duplex): at SF12 it is longer than the
    // first-try deadline, so a press made during it would otherwise expire unsent.
    if (g.ping.active) {
      const uint32_t slot_end = g.ping.tx_at + ack_timeout(PING_FRAME_LEN);
      if ((int32_t)(slot_end - now) > 0) wait += slot_end - now;
    }
    if ((int32_t)(g.reask_guard_until - now) > 0) wait += g.reask_guard_until - now;
  }
  c.max_wait = min<uint32_t>(wait, MAX_QUEUE_WAIT_MS);
  g.q[g.qn++] = c;
  return true;
}

// The payload of a press command, echo left at 0 (filled at each send, service_head).
uint8_t press_payload(MsgType type, uint16_t focus, uint16_t hold, uint8_t pl[6]) {
  memset(pl, 0, 6);
  if (type == MsgType::Shot) {
    pl[0] = (uint8_t)(focus & 0xFF);  // u16 little-endian, then the u32 echo
    pl[1] = (uint8_t)(focus >> 8);
    return 6;
  }
  if (type == MsgType::Shoot && hold) {
    shoot_put_hold(pl, hold);  // after the u32 echo
    return SHOOT_PAYLOAD_LEN_TIMED;
  }
  return 4;  // Focus, held Shoot: u32 echo
}

bool enqueue_reask(const Track& t, uint32_t now) {
  if (!t.has_seq || !t.cmd_id || !g.radio_ok) return false;
  uint8_t pl[6];
  const uint8_t len = press_payload(t.type, t.focus, t.hold, pl);
  if (!enqueue(t.type, pl, len, true, t.id, now)) return false;
  TxCmd& c = g.q[g.qn - 1];
  c.reask = true;
  c.has_seq = true;  // the same command id: the camera board can never run it again
  c.seq = t.seq;
  c.id = t.cmd_id;
  return true;
}

// SF for the next try of the head command. Rescue: SF12 (where the camera board is parked),
// odd tries on a pending SF if one is known. Otherwise odd tries go to the SF the camera
// board may be on unseen: the new SF of a SetSf (its own retries, or one whose Ack we never
// heard), or SF12 when the peer SF is unconfirmed (we reset, or our Sleep went unanswered).
// Whichever answers is adopted (on_peer_status).
uint8_t try_sf(const TxCmd& c) {
  const bool odd = c.tries & 1;
  // Rescue: the camera board may still be on the link SF (it heard our pings within its
  // silence time and has not dropped to SF12 yet): odd tries go there, or to a pending SF.
  if (g.rescue) return odd ? (g.pending_sf ? g.pending_sf : g.link.sf()) : radiocfg::SF_MAX;
  if (odd) {
    if (c.type == MsgType::SetSf && c.len >= 1) return c.pl[0];
    // X22: both doubts at once (a wake with an old pending SF): SF12 gets the first odd try
    // and the pending SF the next, or SF12 would never be tried.
    if (g.pending_sf && g.sf_unconfirmed)
      return (c.tries & 2) ? g.pending_sf : radiocfg::SF_MAX;
    if (g.pending_sf) return g.pending_sf;
    if (g.sf_unconfirmed) return radiocfg::SF_MAX;
  }
  return g.link.sf();
}

void service_head(uint32_t now) {
  TxCmd& c = g.q[0];
  if (!c.headed) {
    c.headed = true;
    c.head_since = now;
  }
  const uint8_t flen = PROTO_HDR + c.len + PROTO_TAG;
  if (c.tries > 0 && now - c.last_tx < ack_timeout(flen)) return;
  if (c.tries > 0) g.link_asked = true;  // a reply was due and did not come
  // Release is idempotent and the camera may be holding the shutter until it arrives:
  // keep repeating it (same seq) instead of giving up after MAX_TRIES / TX_DEADLINE.
  const bool must = c.type == MsgType::Release;
  // L1: the same hold-off as tx_holdoff(), so the first-try deadline base agrees with it.
  const uint32_t holdoff_end = hw_boot_ms() + hw_tx_holdoff_ms();
  if (must) {
    const uint32_t base = after(c.queued_at, holdoff_end) ? c.queued_at : holdoff_end;
    if (after(now, base + RELEASE_GIVE_UP_MS)) {
      const TxCmd dead = c;
      pop_head();
      cmd_failed(dead, dead.tries ? apperr::NO_ACK : apperr::NO_TX, now);
      return;
    }
    // Every MAX_TRIES unanswered tries count as a miss: skips our counter past a rebooted
    // camera board's reservation (else its guard refuses every retry) and can start rescue.
    if (c.tries >= MAX_TRIES && c.tries % MAX_TRIES == 0 && c.fed_at != c.tries) {
      c.fed_at = c.tries;
      if (g.ob) g.rel_alert = true;  // RELEASE NOT CONFIRMED: retries carry on
      // X13: a Release is Acked with priority, so only an all but spent camera excuses it.
      if (!cam_duty_spent(now)) {  // out of duty is not a lost link
        if (cam_duty_held(now)) g.sf_unconfirmed = true;  // X13: as cmd_failed: try SF12 too
        g.cam_duty_doubt = true;
        link_feed(false, 0, now);
      }
    }
  } else if (c.tries >= MAX_TRIES) {
    const TxCmd dead = c;
    pop_head();
    cmd_failed(dead, apperr::NO_ACK, now);
    return;
  } else if (c.tries == 0) {
    const TxExpiry x = tx_first_try_expiry(now, c.queued_at, c.head_since, holdoff_end,
                                           TX_DEADLINE_MS, c.max_wait);
    if (x != TxExpiry::None) {
      const TxCmd dead = c;
      pop_head();
      cmd_failed(dead, x == TxExpiry::Queued ? apperr::QUEUED : apperr::NO_TX, now);
      return;
    }
  } else if (after(now, c.last_tx + ack_timeout(flen) + TX_DEADLINE_MS)) {
    const TxCmd dead = c;  // a retry kept off the air too long: never fire late
    pop_head();
    cmd_failed(dead, apperr::NO_ACK, now);
    return;
  }
  const uint8_t sf = try_sf(c);
  if (radio_sf() != sf) {
    radio_set_sf(sf);
    g.ping.active = false;  // a probe's answer can no longer be heard (not a miss)
  }
  // Freshness binding: a shutter command carries the counter of the newest camera-board frame
  // we accepted. The camera board runs it only if it sent that frame recently, so a recording
  // of this frame replayed later cannot fire the camera.
  if (c.type == MsgType::Shot || c.type == MsgType::Focus || c.type == MsgType::Shoot) {
    const uint8_t off = c.type == MsgType::Shot ? 2 : 0;
    const uint32_t echo = g.guard.last();
    if (c.len >= off + 4) {
      c.pl[off] = (uint8_t)echo;
      c.pl[off + 1] = (uint8_t)(echo >> 8);
      c.pl[off + 2] = (uint8_t)(echo >> 16);
      c.pl[off + 3] = (uint8_t)(echo >> 24);
    }
  }
  const Tx r = send_frame(c.type, c.seq, c.has_seq, c.id, c.pl, c.len, c.prio, now);
  if (r == Tx::Duty) c.duty_refused = true;
  if (!on_air(r)) return;
  // X21: the camera board hears this and watches for silence again, so the duty-quiet Sleep
  // must be sent again after it (service_ping), or it drops to SF12 unseen.
  // Not on SF12: the camera board cannot drop anywhere from there, and the Sleep with its Ack
  // would only spend both reserves and hold the next press behind it.
  if (c.type != MsgType::Sleep && !g.shutting && g.link.sf() != radiocfg::SF_MAX)
    g.duty_quiet_sent = false;
  c.duty_refused = false;
  if (c.tries < 255) c.tries++;
  c.last_tx = millis();
  if (c.type == MsgType::Release) g.last_rel_id = c.id;
  if (c.track && c.track == g.t.id && !c.reask) {
    g.t.has_seq = true;
    g.t.seq = c.seq;
    g.t.cmd_id = c.id;
    g.t.tries = c.tries;
    if (c.tries == 1) {
      g.t.sf = radio_sf();
      g.t.tx_dbm = radio_tx_dbm();
      set_stage(ShotStage::Sent, 0, now);
    }
  }
}

void request_sf(uint8_t sf, uint32_t now) {
  if (sf < radiocfg::SF_MIN || sf > radiocfg::SF_MAX || queued(MsgType::SetSf)) return;
  if (link_quiet(now)) {  // never mid-press: issued from relay_tx once the press is over
    g.sf_wanted = sf;
    return;
  }
  g.sf_wanted = 0;
  enqueue(MsgType::SetSf, &sf, 1, false, 0, now);
}

void start_rescue(uint32_t now) {
  remove_type(MsgType::SetSf);
  g.sf_wanted = 0;
  if (radio_sf() != radiocfg::SF_MAX) radio_set_sf(radiocfg::SF_MAX);
  if (!g.rescue) {
    show_toast("RESCUE SF12", now);
    g.rescue_i = rtc_rescue_i;  // a sweep cut short by sleep continues where it stopped
    g.probe_now = true;         // first probe at once, not one SF12 interval from now
    g.rescue_early = true;      // K3: and SF12 again once the camera board must have dropped
    // K1: the fixed SF itself lost the link: back off before going back to it, longer each time.
    if (!g.s->adaptive_sf && g.link.sf() == g.s->fixed_sf) {
      rtc_fixed_backoff = rtc_fixed_backoff
                              ? min<uint32_t>(rtc_fixed_backoff * 2, FIXED_SF_BACKOFF_MAX_MS)
                              : FIXED_SF_BACKOFF_MS;
      g.last_fixed_try = now;  // K1: the new back-off is counted from here (and LINK FOUND)
    }
  }
  g.rescue = true;
}

// Rescue searches rather than parking on SF12: the camera board may be on another SF (it
// rebooted onto its saved SF, or a SetSf Ack was lost). Every other probe is SF12; the rest
// sweep SF7..11 starting from the last agreed SF (rescue_probe_sf).
uint8_t rescue_peek_sf() { return rescue_probe_sf(g.rescue_i, g.link.sf(), g.pending_sf); }

void rescue_advance() {
  g.rescue_i++;
  rtc_rescue_i = g.rescue_i;
}

void link_feed(bool acked, uint32_t rtt, uint32_t now) {
  // A silent camera board may have rebooted and reserved counters above ours. One skip per
  // silence episode clears that reservation; more would only burn NVS writes and counters.
  if (!acked && !g.ctr_skipped) {
    store_skip_ctr(PEER_CTR_SKIP);
    g.ctr_skipped = true;
  }
  LinkSample s{acked, isnan(g.snr_up) ? -30.f : g.snr_up, isnan(g.snr_dn) ? -30.f : g.snr_dn,
               rtt};
  const LinkManager::Action a = g.link.onPing(s);
  if (a == LinkManager::Action::Rescue) start_rescue(now);
  else if (a == LinkManager::Action::RequestSf && !g.rescue) request_sf(g.link.requestedSf(), now);
}

void maybe_fixed_sf(uint32_t now) {
  // K1: a changed SF setting starts with no back-off.
  if (g.s->adaptive_sf || g.s->fixed_sf != rtc_fixed_for) {
    rtc_fixed_backoff = 0;
    rtc_fixed_for = g.s->fixed_sf;
  }
  if (g.s->adaptive_sf || g.rescue || queued(MsgType::SetSf) || link_quiet(now)) return;
  if (g.link.sf() == g.s->fixed_sf || g.last_ack == 0) return;
  // K1: after the fixed SF lost the link, wait out the back-off instead of flapping
  // between it and SF12 (timed from LINK FOUND, on_peer_status).
  const uint32_t wait = max<uint32_t>(FIXED_SF_RETRY_MS, rtc_fixed_backoff);
  if (now - g.last_fixed_try < wait) return;
  g.last_fixed_try = now;
  request_sf(g.s->fixed_sf, now);
}

void ping_timed_out(uint32_t now) {
  g.ping.active = false;
  g.link_asked = true;
  g.hunt_rssi = 0;  // nothing heard: show "---", not the last good value
  if (cam_duty_held(now)) {
    // The camera board is saving its last 20 % for commands (they still get priority Acks):
    // not a lost link, so no miss, no rescue, which at SF12 would cost it far more airtime.
    show_toast("CAM DUTY: PINGS HELD", now);
    return;
  }
  link_feed(false, 0, now);
}

void service_ping(uint32_t now) {
  if (g.ping_gate || tx_holdoff(now)) return;
  // No pings while a press is held or its outcome is due (link_quiet), nor while the camera
  // board says it is out of duty (it would not answer). Exception: a keepalive, so the
  // camera board still hears us within its silence time (hold watchdog, SF12 rescue).
  const bool held = g.held || switch_active();
  if (held && !g.rescue) {
    // Keepalive while a half/full press is held, even while its outcome is due (the camera
    // board answers neither until the Release). Every silence/4, so the camera board's hold
    // watchdog (one silence time) only fires after 3 lost in a row. Fire and forget: no Ack
    // comes while its button is down, and an Ack slot would hold the Release behind it.
    // Priority: bounded by the hold, and without it the camera lets go of AF / the shutter.
    if (!g.last_tx_ok || now - g.last_tx_ok < camera_silence_ms() / 4) return;
    if (now - g.last_ping_try < timing::PING_MIN_INTERVAL_MS) return;  // refused: no spin
    PingPayload kp{};
    kp.hh_batt = g.batt;
    // bit1 "I am holding": a camera board holding a press for us lets go at once when a ping
    // says we are not (we were reset mid-hold), instead of waiting out its silence watchdog.
    kp.flags = (gps_is_on() ? 1 : 0) | (g.held ? 2 : 0);
    uint8_t kpl[PING_PAYLOAD_LEN];
    ping_pack(kp, kpl);
    uint16_t kseq = 0;
    uint32_t kid = 0;
    bool khas = false;
    g.last_ping_try = now;
    send_frame(MsgType::Ping, kseq, khas, kid, kpl, sizeof kpl, true, now);
    return;
  }
  if (awaiting_outcome(now)) return;  // bounded by result_wait after the press / Release
  if (held || link_quiet(now) || cam_duty_held(now)) {
    if (!g.last_tx_ok || now - g.last_tx_ok <= camera_silence_ms() / 2) return;
    if (now - g.last_ping_try < timing::PING_MIN_INTERVAL_MS) return;  // refused: no spin
  }
  // Our own duty is past the 80 % ping line: pings are refused for a while. Tell the camera
  // board once (a priority Sleep: it then waits instead of rescuing to SF12), and do not
  // read our own silence as a lost link (relay_tx). Cleared when a ping goes out again.
  if (!g.rescue && !duty_allows(radio_airtime_ms(PING_FRAME_LEN), false)) {
    if (!g.duty_quiet_sent && g.last_tx_ok && !queued(MsgType::Sleep)) {
      g.duty_quiet_sent = enqueue(MsgType::Sleep, nullptr, 0, true, 0, now);
      if (g.duty_quiet_sent) show_toast("DUTY: PINGS PAUSED", now);
    }
    g.last_ping_try = now;
    return;
  }
  uint32_t interval = ping_interval();
  uint8_t probe_sf = 0;
  if (g.rescue) {
    // Each probe waits by its own SF's round trip (and pays its own duty), so SF7..11
    // probes are not held behind SF12's long interval.
    probe_sf = rescue_peek_sf();
    // K4: a refused first probe stays first, so it needs a small wait or it would be tried
    // on every loop. At rescue start the last ping is nearly always older than this already.
    interval = g.probe_now ? TX_FAIL_BACKOFF_MS
                           : ping_interval_ms(airtime_at(probe_sf, PING_FRAME_LEN),
                                              airtime_at(probe_sf, ACK_FRAME_LEN),
                                              timing::PING_MIN_INTERVAL_MS);
    // K3: the camera board may have heard a ping we got no Ack for, and so dropped to SF12
    // only after our first SF12 probe. Probe SF12 again as soon as its silence time on the
    // old SF must be over (counted from our last probe), not a full SF12 interval later.
    if (!g.probe_now && g.rescue_early && probe_sf == radiocfg::SF_MAX) {
      const uint8_t old_sf = max<uint8_t>(g.link.sf(), g.pending_sf);
      const uint32_t old_ping = airtime_at(old_sf, PING_FRAME_LEN);
      const uint32_t drop =
          rescue_silence_ms(ping_interval_ms(old_ping, airtime_at(old_sf, ACK_FRAME_LEN),
                                             timing::PING_MIN_INTERVAL_MS),
                            old_ping, timing::CAM_RESCUE_SILENCE_MS) +
          old_ping + 500;
      if (drop < interval) interval = drop;
    }
  }
  if (now - g.last_ping_try < interval) return;
  if (page() == Page::Hunt && !hunt_duty_ok() && !g.hunt_slowed) {
    g.hunt_slowed = true;
    show_toast("HUNT SLOWED: DUTY", now);
  }
  g.last_ping_try = now;
  // K4: the sweep moves on only once the probe is on air (below), so a refused probe (duty,
  // back-off, radio) keeps its slot and is tried again.
  if (g.rescue && probe_sf != radio_sf()) radio_set_sf(probe_sf);
  PingPayload pp{};
  pp.hh_batt = g.batt;
  pp.last_rssi = isnan(g.rssi_dn) ? 0 : app_clamp8(g.rssi_dn);
  pp.last_snr_q4 = isnan(g.snr_dn) ? 0 : app_clamp8(g.snr_dn * 4.f);
  pp.flags = (gps_is_on() ? 1 : 0) | (g.held ? 2 : 0);
  uint8_t pl[PING_PAYLOAD_LEN];
  ping_pack(pp, pl);
  uint16_t seq = 0;
  uint32_t id = 0;
  bool has = false;
  if (on_air(send_frame(MsgType::Ping, seq, has, id, pl, sizeof pl, false, now))) {
    if (g.rescue) {
      // K3: one early SF12 probe per rescue: used up by the first SF12 probe after the start.
      if (!g.probe_now && probe_sf == radiocfg::SF_MAX) g.rescue_early = false;
      g.probe_now = false;
      rescue_advance();
    }
    g.duty_quiet_sent = false;
    g.ping.active = true;
    g.ping.seq = seq;
    g.ping.id = id;
    g.ping.tx_at = millis();
  }
}

void relay_tx(uint32_t now) {
  if (!g.radio_ok) {
    // Nothing can ever go out: fail every queued press so it is shown and we can sleep.
    while (g.qn) {
      const TxCmd dead = g.q[0];
      pop_head();
      cmd_failed(dead, apperr::NO_TX, now);
    }
    return;
  }
  // Kept off the air (duty governor) long enough that the camera board will treat us as
  // gone and drop to SF12: follow it there instead of shouting on the old SF.
  // Not when it is our own duty that keeps pings off the air: that silence is intended (the
  // camera board was told, see service_ping), and SF12 would cost ~20x the airtime.
  if (!g.rescue && !g.shutting && g.last_tx_ok && now - g.last_tx_ok > camera_silence_ms() &&
      !g.duty_quiet_sent && duty_allows(radio_airtime_ms(PING_FRAME_LEN), false))
    start_rescue(now);
  // A dropped re-ask may be answered right now (half duplex): wait out its Ack slot.
  if ((int32_t)(g.reask_guard_until - now) > 0) return;
  if (g.ping.active) {
    // Half duplex: while the camera board may be sending its Ack, anything we send is lost
    // (and so is its Ack). Commands wait for that Ack or for its slot to pass.
    if ((int32_t)(now - g.ping.tx_at) <= (int32_t)ack_timeout(PING_FRAME_LEN)) return;
    ping_timed_out(now);
  }
  // K5: transmits keep starting but never finish (TX_DONE missing): re-initialise the radio.
  // Without this only a deep sleep and wake brings it back (never, on a computer's USB).
  // With nothing queued: only when nothing is held or awaiting its outcome. With a command
  // queued (a Release must not wait 30 s for this): between two tries of the head, never
  // inside its Ack slot. Repeated at most every RADIO_REINIT_GAP_MS while the fault lasts,
  // since one re-init may not cure it (a brown-out that is not over yet).
  if (radio_tx_fault_streak() < 2) {
    g.radio_reinit = false;
  } else if (!g.radio_reinit || now - g.radio_reinit_at >= RADIO_REINIT_GAP_MS) {
    const bool head_due =
        g.qn && (g.q[0].tries == 0 ||
                 now - g.q[0].last_tx >= ack_timeout((uint8_t)(PROTO_HDR + g.q[0].len + PROTO_TAG)));
    if (head_due || (!g.qn && !g.shutting && !link_quiet(now))) {
      g.radio_reinit = true;
      g.radio_reinit_at = now;
      const uint8_t sf = radio_sf();  // rescue may have it on a probe SF: keep it
      radio_sleep();
      g.radio_ok = radio_begin(sf, g.s->ant_gain_dbi);
      if (!g.radio_ok) show_toast("RADIO FAILED", now);  // presses now fail at once (command)
      return;
    }
  }
  if (g.qn) {
    // A new command's id is assigned at its first send. If the camera board has been silent
    // long enough that it may have rebooted (and reserved counters above ours), skip past
    // that reservation first, or every try of this command would be refused.
    if (!g.q[0].has_seq && !g.ctr_skipped && g.last_rx_ok &&
        now - g.last_rx_ok > camera_silence_ms()) {
      store_skip_ctr(PEER_CTR_SKIP);
      g.ctr_skipped = true;
    }
    service_head(now);  // stop-and-wait: keeps commands in order, no pings meanwhile
    return;
  }
  if (g.shutting) return;
  // Back on the agreed SF after an SF12 try of an unconfirmed-SF command.
  if (!g.rescue && radio_sf() != g.link.sf()) radio_set_sf(g.link.sf());
  if (!link_quiet(now)) {
    // An adaptive SF change held back during a press goes out now.
    if (g.sf_wanted && !g.rescue && g.s->adaptive_sf) request_sf(g.sf_wanted, now);
    g.sf_wanted = 0;
    maybe_fixed_sf(now);
  }
  if (!g.qn) service_ping(now);
}

// Status fields are trusted only from an Ack/Result that answers a frame this boot sent and
// is still waiting on, matched on the full command id (seq + id_hi): a replayed old Ack, or
// one for an older command sharing the 16-bit seq, can never paint success on the screen.
bool ping_answered(const AckPayload& a) {
  return g.ping.active && a.ack_type == MsgType::Ping &&
         ack_matches_id(a.ack_seq, a.ack_id_hi, g.ping.id);
}

bool head_answered(const AckPayload& a) {
  return g.qn && g.q[0].has_seq && g.q[0].type == a.ack_type &&
         ack_matches_id(a.ack_seq, a.ack_id_hi, g.q[0].id);
}

bool track_answered(const AckPayload& a) {
  return g.t.open && g.t.has_seq && g.t.type == a.ack_type &&
         ack_matches_id(a.ack_seq, a.ack_id_hi, g.t.cmd_id);
}

// The outcome of a Release write at the camera board: a two-stage Release we sent, or a
// one-press's own Release step (reported under the Shot's id, typed Release).
bool release_answered(const AckPayload& a) {
  if (a.ack_type != MsgType::Release) return false;
  if (g.last_rel_id && ack_matches_id(a.ack_seq, a.ack_id_hi, g.last_rel_id)) return true;
  return g.t.has_seq && g.t.cmd_id && ack_matches_id(a.ack_seq, a.ack_id_hi, g.t.cmd_id);
}

bool answers_ours(const AckPayload& a, MsgType ftype) {
  if (ftype == MsgType::Ack && ping_answered(a)) return true;
  if (ftype == MsgType::Result && release_answered(a)) return true;
  return head_answered(a) || track_answered(a);
}

void on_peer_status(const AckPayload& a, const RxPacket& p, uint32_t now) {
  g.last_ack = now;
  g.cam = a.cam_link;
  g.cam_ble = a.ble_rssi;
  g.cam_batt = a.cam_batt;
  g.cam_duty = a.duty_permille;
  g.cam_duty_doubt = false;  // X13: a fresh report, trusted again
  g.rssi_up = a.rx_rssi;
  g.snr_up = a.rx_snr_q4 / 4.f;
  g.rssi_dn = p.rssi;
  g.snr_dn = p.snr;
  if (a.gps_flags & 1) {
    g.cam_fix = true;
    g.cam_fix_at = now;
    g.cam_lat = a.lat_e7 / 1e7;
    g.cam_lon = a.lon_e7 / 1e7;
  }
  g.hunt_rssi = (int16_t)lroundf(p.rssi);
  if (g.hunt_rssi > g.hunt_best) g.hunt_best = g.hunt_rssi;
  g.ping_gate = false;
  g.sf_unconfirmed = false;  // we know where the camera board is listening now
  rtc_sf_unconfirmed = false;
  g.pending_sf = 0;
  rtc_pending_sf = 0;
  if (g.rescue && a.sf == radio_sf()) {  // found the camera board: adopt the SF it is on
    g.rescue = false;
    rtc_rescue_i = 0;
    if (rtc_fixed_backoff) g.last_fixed_try = now;  // K1: the back-off runs from here
    g.link.confirmSf(a.sf);
    persist_sf(a.sf);
    show_toast("LINK FOUND", now);
  } else if (!g.rescue && a.sf == radio_sf() && a.sf != g.link.sf()) {
    // Answered on the SF12 try of an unconfirmed-SF command: the camera board is there.
    g.link.confirmSf(a.sf);
    persist_sf(a.sf);
    show_toast("LINK FOUND", now);
  }
}

void fill_track_link(Track& t) {
  t.rssi_up = g.rssi_up;
  t.snr_up = g.snr_up;
  t.rssi_dn = g.rssi_dn;
  t.snr_dn = g.snr_dn;
  t.ble = g.cam_ble;
}

void on_cmd_acked(const TxCmd& c, uint32_t now) {
  const uint32_t rtt = now - c.last_tx;
  g.rtt = (uint16_t)min<uint32_t>(rtt, 65535);
  if (c.type == MsgType::SetSf) {
    const uint8_t sf = c.pl[0];
    radio_set_sf(sf);
    g.link.confirmSf(sf);
    persist_sf(sf);
    g.pending_sf = 0;
    rtc_pending_sf = 0;
  }
  if (c.track && c.track == g.t.id && !c.reask) {
    g.t.rtt = (uint16_t)min<uint32_t>(now - g.t.press_ms, 65535);
    fill_track_link(g.t);
    set_stage(ShotStage::Delivered, 0, now);
  }
  link_feed(true, rtt, now);
}

void on_ack(const AckPayload& a, uint32_t now) {
  if (ping_answered(a)) {
    g.ping.active = false;
    g.rtt = (uint16_t)min<uint32_t>(now - g.ping.tx_at, 65535);
    if (g.link.sf() == g.s->fixed_sf) rtc_fixed_backoff = 0;  // K1: the fixed SF works again
    link_feed(true, now - g.ping.tx_at, now);
    return;
  }
  if (head_answered(a)) {
    const TxCmd c = g.q[0];
    pop_head();
    on_cmd_acked(c, now);
    return;
  }
  // Late Ack for a press already reported NO ACK: the camera board did get it.
  if (track_answered(a)) {
    fill_track_link(g.t);
    set_stage(ShotStage::Delivered, 0, now);
  }
}

void release_outcome_toast(ShotStage st, uint8_t err, uint32_t now) {
  show_toast(st == ShotStage::Accepted ? "RELEASE OK"
             : err == apperr::GAVE_UP  ? "RELEASE GAVE UP: CHECK CAM"
                                       : "RELEASE FAILED: RETRYING",
             now);
}

// The camera board refused to run the press (not bound to a recent frame of its own: after a
// sleep our last one is old). Nothing ran, so it is safe to send again, once, as a new
// command (new id) that now echoes the Result we just accepted.
bool refresh_press(uint32_t now) {
  Track& t = g.t;
  if (t.refreshed || !g.radio_ok) return false;
  // X11: a full press already let go (a short stab, or AF LOCK's 200 ms shot) is still the one
  // shot the user asked for, and nothing ran. It is resent as a timed press, which the camera
  // board lets go of by itself, with a Release behind it as backup. Never while shutting down.
  // Only a press meant to be short: a timed one, or the two-stage switch. A one-button held
  // Shoot (BULB, BURST) that the user has already stopped must not come back as a 200 ms frame.
  const bool let_go = t.type == MsgType::Shoot && !g.held && !g.shutting &&
                      (t.hold != 0 || g.s->two_stage);
  // A half/full press only while still held, and never behind its own Release (it would
  // press the camera after the user let go).
  if ((t.type == MsgType::Focus || t.type == MsgType::Shoot) && !let_go &&
      (!g.held || queued(MsgType::Release)))
    return false;
  // X11: room for both, or queueing the Release could push the resent press out again.
  if (let_go && g.qn + 2 > QCAP) return false;
  if (let_go && !t.hold) t.hold = timing::SHUTTER_HOLD_MS;  // X11: was held until the Release
  uint8_t pl[6];
  const uint8_t len = press_payload(t.type, t.focus, t.hold, pl);
  if (!enqueue(t.type, pl, len, true, t.id, now)) return false;
  if (let_go && g.qn >= 2 && g.q[g.qn - 2].type == MsgType::Release && g.q[g.qn - 2].tries == 0) {
    // X11: the press's own Release has not gone out yet: the resent Shoot goes ahead of it, so
    // the camera board shoots on the AF hold it still keeps, and that Release then follows.
    const TxCmd rel = g.q[g.qn - 2];
    g.q[g.qn - 2] = g.q[g.qn - 1];
    g.q[g.qn - 1] = rel;
  } else if (let_go) {
    // X11: the backup Release, straight behind it (a Release can never fire anything).
    enqueue(MsgType::Release, nullptr, 0, true, 0, now);
  }
  t.refreshed = true;
  t.has_seq = false;  // the new id is taken at its first send (service_head)
  t.cmd_id = 0;
  t.stage = ShotStage::Sent;
  t.err = 0;
  t.stage_ms = now;
  return true;
}

void on_result(const AckPayload& a, uint32_t now) {
  // A Result also proves delivery if the Ack was lost.
  if (head_answered(a)) {
    const TxCmd c = g.q[0];
    pop_head();
    on_cmd_acked(c, now);
  }
  if (a.ack_type == MsgType::Release) {
    if (release_answered(a) && (a.stage == ShotStage::Failed || a.stage == ShotStage::Accepted))
      release_outcome_toast(a.stage, a.err, now);
    return;
  }
  if (!track_answered(a)) return;
  if (a.stage == ShotStage::Failed && a.err == apperr::NEED_FRESH && refresh_press(now)) return;
  if (a.stage != ShotStage::Accepted && a.stage != ShotStage::Failed &&
      a.stage != ShotStage::CamEvent)
    return;
  if (provisional_fail(g.t) && a.stage != ShotStage::Failed) fill_track_link(g.t);
  set_stage(a.stage, a.err, now);
  g.t.ble = a.ble_rssi;
  if (a.stage == ShotStage::Failed && a.err == apperr::STALE)
    show_toast("CAM BOARD RESTARTED", now);
  if (a.stage == ShotStage::Failed && a.err == apperr::STORE)
    show_toast("CAM BOARD STORAGE FAIL", now);
  if (a.stage == ShotStage::Failed && a.err == apperr::HOLD_LOST)
    show_toast("HOLD RELEASED: NO LINK", now);
}

void relay_rx(uint32_t now) {
  if (!g.radio_ok) return;
  RxPacket p;
  for (uint8_t i = 0; i < 4 && radio_poll(p); i++) {
    Frame f;
    if (!frame_decode(p.data, p.len, g.pair.key, f) || f.net != g.pair.net) continue;
    if (f.type != MsgType::Ack && f.type != MsgType::Result) continue;
    if (!g.guard.accept(f.ctr)) continue;
    store_save_peer_ctr(f.ctr);
    g.ctr_skipped = false;  // the camera board answers again: the silence episode is over
    g.last_rx_ok = now;
    AckPayload a;
    if (!ack_unpack(f.payload, f.len, a)) continue;
    if (a.utc) utc_set(a.utc);   // the camera unit's GPS time
    if (!answers_ours(a, f.type)) continue;
    on_peer_status(a, p, now);
    if (f.type == MsgType::Ack) on_ack(a, now);
    else on_result(a, now);
  }
}

// ---------- direct: BLE ----------

void direct_start_shot(uint16_t id, uint32_t now, uint16_t focus) {
  if (g.exec.startShot(now, focus, id)) {
    set_stage(ShotStage::Sent, 0, now);
  } else {
    set_stage(ShotStage::Failed, apperr::BUSY, now);
    show_toast("BUSY", now);
  }
}

void direct_step(uint32_t now) {
  fuji.loop(now);
  // Release on connect: the first write on a new camera link is a Release. It can never fire,
  // and it clears a press the camera may still hold from a link that dropped mid-hold.
  const bool ready = fuji.ready();
  if (ready && !g.fuji_was_ready && !g.held && !g.exec.busy()) {
    g.exec.releaseOnConnect(now);  // S9: retried if this first write fails
    g.exec.poll(now);
  }
  g.fuji_was_ready = ready;
  if (g.defer.active) {
    if (fuji.ready()) {
      g.defer.active = false;
      if (g.t.id == g.defer.track) {
        if (g.defer.type == MsgType::Shot) {
          direct_start_shot(g.defer.track, now, g.defer.focus);
        } else {  // two-stage Shoot, switch still held: press the shutter now
          if (g.defer.hold) g.exec.fullTimed(now, g.defer.track, g.defer.hold);
          else g.exec.full(now, g.defer.track);
          set_stage(ShotStage::Sent, 0, now);
        }
      }
    } else if (now - g.defer.at > DEFER_SHOT_MS) {
      g.defer.active = false;
      if (g.t.id == g.defer.track) set_stage(ShotStage::Failed, (uint8_t)FujiErr::NotReady, now);
    }
  }
  g.exec.poll(now);
  if (g.exec.releasePending() != g.release_warned) {
    g.release_warned = g.exec.releasePending();
    if (g.release_warned) show_toast("RELEASE FAILED: RETRYING", now);
  }
  CamOutcome o;
  while (g.exec.next(o)) {
    if (o.release) {
      // RELEASE FAILED: RETRYING is shown above; say how it ended too.
      if (o.stage == ShotStage::Accepted || o.err == apperr::GAVE_UP)
        release_outcome_toast(o.stage, o.err, millis());
      continue;
    }
    if (o.tag != g.t.id) continue;
    const uint32_t t = millis();
    if (o.stage != ShotStage::CamEvent) g.t.rtt = (uint16_t)min<uint32_t>(t - g.t.press_ms, 65535);
    g.t.ble = fuji.rssi();
    set_stage(o.stage, o.err, t);
  }
}

CamLink fuji_camlink() {
  if (!fuji.hasSaved() && fuji.state() != FujiState::PairScan) return CamLink::NotPaired;
  switch (fuji.state()) {
    case FujiState::Ready: return CamLink::Ready;
    case FujiState::Connecting:
    case FujiState::Securing:
    case FujiState::Handshake: return CamLink::Connecting;
    case FujiState::Scanning:
    case FujiState::PairScan:
    case FujiState::Failed: return CamLink::Scanning;
    default: return CamLink::None;
  }
}

uint8_t ble_bars(int8_t rssi) {
  if (rssi == 0) return 0;
  const int b = (int)lroundf((rssi + 95) * 4 / 45.f);
  return (uint8_t)constrain(b, 0, 4);
}

// ---------- commands ----------

// A one-press is still on its way (not yet accepted by the camera, nor failed / unknown):
// a second press now would only fail BUSY and hide the first one's real outcome.
bool one_press_running(uint32_t now) {
  if (!g.relay && (g.exec.busy() || g.defer.active)) return true;
  if (!g.t.open || g.t.type != MsgType::Shot) return false;
  if (final_stage(g.t.stage) || g.t.err == apperr::NO_RESULT) return false;
  if ((uint8_t)g.t.stage >= (uint8_t)ShotStage::Accepted) return false;
  // Relay, delivered: bounded by the camera board's own busy time, not by its Result (which
  // can be lost). Past that the board is free again; if not, it answers BUSY itself.
  if (g.relay && g.t.stage == ShotStage::Delivered &&
      now - g.t.stage_ms > (uint32_t)g.s->focus_ms + timing::SHUTTER_HOLD_MS + CAM_CMD_WAIT_MS +
                               2 * radio_airtime_ms(ACK_FRAME_LEN))
    return false;
  return true;
}

// hold_ms (Shoot only): the press lets go by itself after this long, timed where the camera
// write happens (camera board in relay, CamExec in direct). The Release that follows is a
// backup. 0 = held until the Release (held BURST/BULB, the two-stage switch).
void command(MsgType type, uint32_t now, uint16_t focus_ms = FOCUS_DEFAULT, uint16_t hold_ms = 0) {
  const uint16_t focus = focus_ms == FOCUS_DEFAULT ? g.s->focus_ms : focus_ms;
  g.last_activity = now;
  if (type == MsgType::Shot && one_press_running(now)) {
    show_toast("BUSY: LAST SHOT RUNNING", now);
    return;
  }
  if (g.relay) {
    if (!g.paired) {
      show_toast("NOT PAIRED", now);
      return;
    }
    if (!g.radio_ok) {  // nothing can go out: fail the press now, never queue it forever
      if (type != MsgType::Release) {
        open_track(type, now);
        set_stage(ShotStage::Failed, apperr::NO_TX, now);
      }
      show_toast("RADIO FAILED", now);
      return;
    }
    uint8_t pl[6] = {0, 0, 0, 0, 0, 0};
    uint8_t len = 0;
    uint16_t track = 0;
    if (type == MsgType::Shot || type == MsgType::Focus || type == MsgType::Shoot)
      len = press_payload(type, focus, hold_ms, pl);  // + u32 freshness echo, per send
    if (type != MsgType::Release) {
      track = open_track(type, now);
      g.t.focus = focus;
      g.t.hold = type == MsgType::Shoot ? hold_ms : 0;
    }
    if (!enqueue(type, pl, len, true, track, now) && track) {
      set_stage(ShotStage::Failed, apperr::QUEUED, now);
    } else if (track && tx_holdoff(now)) {
      show_toast("RADIO WARMUP", now);
    }
    return;
  }

  switch (type) {
    case MsgType::Shot:
    case MsgType::Shoot: {
      if (!fuji.ready()) {
        fuji.wanted();  // P1: the press waits here, so no fuji command would end eased scanning
        g.defer = Deferred{true, now, open_track(type, now), type, focus, hold_ms};
        g.t.focus = focus;
        show_toast("CONNECTING", now);
        return;
      }
      const uint16_t id = open_track(type, now);
      g.t.focus = focus;
      if (type == MsgType::Shot) {
        direct_start_shot(id, now, focus);
      } else if (hold_ms) {
        g.exec.fullTimed(now, id, hold_ms);  // lets go by itself even if the loop blocks
        set_stage(ShotStage::Sent, 0, now);
      } else {
        g.exec.full(now, id);
        set_stage(ShotStage::Sent, 0, now);
      }
      break;
    }
    case MsgType::Focus: {
      if (!fuji.ready()) {
        fuji.wanted();  // P1: as above
        show_toast("CONNECTING", now);
        return;
      }
      const uint16_t id = open_track(type, now);
      g.exec.half(now, id);
      set_stage(ShotStage::Sent, 0, now);
      break;
    }
    case MsgType::Release:
      if (g.defer.active) {  // let go before the camera was ready: that is "no shot"
        g.defer.active = false;
        if (g.t.id == g.defer.track) set_stage(ShotStage::Failed, apperr::CANCELLED, now);
        show_toast("NOT SENT: NO CAM", now);
        break;  // nothing was pressed on the camera
      }
      g.exec.release(now);  // CamExec repeats it until the camera acknowledges
      break;
    default: break;
  }
  g.exec.poll(now);  // run due steps now, before a Release can cancel them
}

// ---------- power ----------

void ob_force_stop(ObStop why, uint32_t now);

void begin_shutdown(uint32_t now, const char* msg) {
  if (g.shutting) return;
  g.shutting = true;
  g.shut_msg = msg;
  // One button: stop any run and let go of anything held (its Release goes out first).
  if (g.ob) ob_force_stop(ObStop::Sleep, now);
  g.ping.active = false;
  g.defer.active = false;
  g.shut_deadline = now + 1000;
  if (g.held) {  // never leave the camera holding focus/shutter
    g.held = false;
    command(MsgType::Release, now);
  }
  // S7: direct, the camera has not acknowledged a Release: it may still hold AF or the shutter.
  // Give the retries real time (from now, the write above may have blocked) before sleeping.
  if (!g.relay && g.exec.releasePending()) g.shut_deadline = millis() + DIRECT_RELEASE_WAIT_MS;
  // Antenna gate still closed (power-on, never touched): nothing was sent this boot, so the
  // camera board has no new silence to misread; send nothing (rtc_sf_unconfirmed unchanged).
  if (g.relay && g.paired && g.radio_ok && !g.ping_gate) {
    remove_type(MsgType::SetSf);
    g.sf_wanted = 0;
    // enqueue() keeps a slot for Sleep (and may drop an unsent press to make room).
    // X13: as cmd_failed: only an all but spent camera board explains a Sleep without Ack.
    if (!enqueue(MsgType::Sleep, nullptr, 0, true, 0, now) && !cam_duty_spent(now))
      rtc_sf_unconfirmed = true;
    // Sized from the SFs the queued frames will really use (try_sf: SF12 in rescue or with
    // an unconfirmed peer SF), with each frame's own airtime and its Ack's, for 2 tries.
    uint32_t need = 2000;
    for (uint8_t i = 0; i < g.qn; i++) {
      const TxCmd& c = g.q[i];
      const uint8_t flen = (uint8_t)(PROTO_HDR + c.len + PROTO_TAG);
      for (uint8_t k = 0; k < 2; k++) {
        TxCmd probe = c;
        probe.tries = (uint8_t)(c.tries + k);
        const uint8_t sf = try_sf(probe);
        need += airtime_at(sf, flen) + airtime_at(sf, ACK_FRAME_LEN) + 150;
      }
    }
    g.shut_deadline = now + need;
  }
}

bool two_stage_up() {
  return !g.s->two_stage ||
         (digitalRead(pins::BTN_HALF) == HIGH && digitalRead(pins::BTN_FULL) == HIGH);
}

[[noreturn]] void deep_sleep(bool prg_stuck = false) {
  close_track();
  rtc_page_i = g.ob ? g.eng.view() : g.page_i;
  rtc_ob_valid = g.ob;
  rtc_ob_mode = (uint8_t)g.eng.mode();
  rtc_ob_rec = g.ob && g.eng.recMaybe();
  bootgest_settle();
  hw_led(false);
  rtc_range_toggle = g.range_toggle;
  rtc_range_n = g.range_n;
  rtc_range_ok = g.range_ok;
  while (g.logq_n) write_log_row(millis());  // deferred range-log rows; nothing else runs now
  ui_power(false);
  gps_power(false);
  radio_sleep();  // also when never started: SX1262 otherwise idles in standby or RX
  duty_checkpoint(true);
  // Exact replay-guard position, so a wake never re-accepts camera frames already seen.
  if (g.relay && g.paired) store_put_peer_ctr(g.guard.last());
  // K1: the awake time waited so far counts on the next wake (unsigned difference, capped).
  rtc_fixed_waited =
      rtc_fixed_backoff ? min<uint32_t>(millis() - g.last_fixed_try, rtc_fixed_backoff) : 0;
  if (g.relay) {  // adaptive-SF streak continues on the next wake
    rtc_link = g.link.state();
    rtc_link_valid = true;
  }
  // Let the camera see the disconnect before we sleep (documented <= 200 ms block).
  if (!g.relay) fuji.shutdown(200);
  hw_deep_sleep(app_wake_extra(*g.s), prg_stuck);
}

// C2: a press made while an idle SLEEP shutdown runs (the loop keeps sampling the buttons, it
// does not act on them). Events queue as usual and are handled once the sleep is cancelled.
bool press_while_shutting() {
  if (g.ob) return g.pev_n || g.aev_n;  // every press starts with its Down event
  if (g.prg_n || g.prg.isDown()) return true;
  for (uint8_t i = 0; i < g.st_n; i++)  // a contact opening is not a press (stuck switch)
    if (g.st_ev[i] == StageEvent::HalfDown || g.st_ev[i] == StageEvent::FullDown) return true;
  return false;
}

// C2: the press wins over an idle sleep: stay awake, and inputs() handles it on the next loop
// like any other press. Never used for OFF, SETUP or BUTTON STUCK.
void cancel_idle_shutdown(uint32_t now) {
  g.shutting = false;
  g.shut_idle = false;
  g.last_activity = now;
  // The Sleep frame must not hold the press behind it. One already on air may be answered
  // right now (half duplex): stay off the air until its Ack slot has passed (as open_track).
  for (uint8_t i = g.qn; i-- > 0;) {
    if (g.q[i].type != MsgType::Sleep) continue;
    if (g.q[i].tries > 0) {
      const uint32_t end =
          g.q[i].last_tx + ack_timeout((uint8_t)(PROTO_HDR + g.q[i].len + PROTO_TAG));
      if ((int32_t)(end - g.reask_guard_until) > 0) g.reask_guard_until = end;
    }
    remove_at(i);
  }
  g.duty_quiet_sent = false;  // a duty-quiet Sleep merged into that one is sent again if due
  // A Sleep that was given up unanswered: we stay awake, so try SF12 as well now (as K2).
  if (rtc_sf_unconfirmed) g.sf_unconfirmed = true;
}

void shutdown_step(uint32_t now) {
  if (g.shut_idle && press_while_shutting()) {
    cancel_idle_shutdown(now);
    return;
  }
  const bool sent = (g.qn == 0 && !g.exec.releasePending()) || after(now, g.shut_deadline);
  // X13: as cmd_failed: only an all but spent camera board explains a Sleep without Ack.
  if (sent && g.qn && !cam_duty_spent(now)) {  // gave up on the queue (e.g. Sleep unanswered)
    for (uint8_t i = 0; i < g.qn; i++)
      if (g.q[i].type == MsgType::Sleep) rtc_sf_unconfirmed = true;
  }
  if (!sent) return;
  // PRG must be up (it is the wake pin: held, it would wake us at once). A two-stage contact
  // is waited for only a while: one stuck LOW (crushed cable, jammed switch) must not keep
  // the board awake until the battery is flat; hw_deep_sleep leaves a LOW pin out of the
  // wake mask, and PRG still wakes.
  // PRG held or stuck LOW (pressed in a bag) past the same grace: sleep anyway, with PRG out
  // of the wake sources and a timer that re-checks it (hw_stuck_wake_check).
  if (digitalRead(pins::BTN_PRG) == LOW) {
    if (after(now, g.shut_deadline + STUCK_SLEEP_GRACE_MS)) deep_sleep(true);
    return;
  }
  if (two_stage_up() || after(now, g.shut_deadline + STUCK_SLEEP_GRACE_MS)) deep_sleep();
}

void idle_check(uint32_t now) {
  // A two-stage level held unchanged for minutes is a stuck contact, not a user: release
  // what it holds once, say so, and stop counting it as activity (else we never sleep).
  const uint8_t lv = g.s->two_stage ? g.two.level() : 0;
  if (lv != g.stage_level) {
    g.stage_level = lv;
    g.stage_level_since = now;
    g.stuck = false;
  } else if (lv != 0 && !g.stuck && now - g.stage_level_since >= STUCK_SWITCH_MS) {
    g.stuck = true;
    show_toast("SWITCH STUCK", now);
    if (g.held) {
      g.held = false;
      g.release_ms = now;
      command(MsgType::Release, now);
    }
  }
  if (g.qn || g.exec.busy() || g.exec.releasePending() || g.defer.active || g.held)
    g.last_activity = now;
  if (switch_active()) g.last_activity = now;
  // One button: never sleep while anything runs, is held, locked, or a card is open.
  if (g.ob && g.eng.busy()) g.last_activity = now;
  // On a computer's USB (host sending frames, not a plain charger): stay awake, so a bench
  // setup or a host tool never loses the board to deep sleep. On battery or a charger: 60 s idle.
  if (HWCDC::isPlugged()) g.last_activity = now;
  if (now - g.last_activity >= timing::HH_IDLE_SLEEP_MS) {
    begin_shutdown(now, "SLEEP");
    g.shut_idle = true;  // C2: only this shutdown can be cancelled by a press
  }
}

// ---------- input ----------

void next_page(uint32_t now) {
  uint8_t n;
  pages(n);
  g.page_i = (uint8_t)((g.page_i + 1) % n);
  if (page() == Page::Hunt) g.hunt_best = -200;
  g.hunt_slowed = false;
  (void)now;
}

void page_action(uint32_t now) {
  switch (page()) {
    case Page::Range:
      g.range_toggle = !g.range_toggle;
      show_toast(g.range_toggle ? "RANGE LOG ON" : "RANGE LOG OFF", now);
      break;
    case Page::Gps:
      g.s->hh_gps = !g.s->hh_gps;
      store_save(*g.s);
      show_toast(g.s->hh_gps ? "GPS ALWAYS ON" : "GPS AUTO", now);
      break;
    case Page::Hunt:
      g.hunt_best = -200;
      show_toast("BEST RESET", now);
      break;
    default: break;
  }
}

void handle_prg(BtnEvent e, uint32_t now) {
  if (e == BtnEvent::None) return;
  g.last_activity = now;
  g.ping_gate = false;
  if (g.wake_click) {  // the debouncer saw the wake press after all: let it decide
    g.wake_click = false;
    if (e == BtnEvent::Click) {
      g.swallow_long = false;
      command(MsgType::Shot, now);
      return;
    }
  }
  if (e == BtnEvent::VeryLong) {
    begin_shutdown(now, "OFF");
    return;
  }
  if (g.swallow_prg) {  // the press that woke us in two-stage mode
    g.swallow_prg = false;
    return;
  }
  if (e == BtnEvent::Long && g.swallow_long) {  // held to wake: just look, don't change page
    g.swallow_long = false;
    return;
  }
  if (!g.s->two_stage) {
    if (e == BtnEvent::Click) command(MsgType::Shot, now);
    else if (e == BtnEvent::Long) next_page(now);
  } else {
    if (e == BtnEvent::Click) next_page(now);
    else if (e == BtnEvent::Long) page_action(now);
  }
}

// The FULL stab that woke us was let go before TwoStage ever reported it: it was a complete
// press, so one focus-shoot-release (never a second one from its tail).
void wake_stab_shot(uint32_t now, bool tail_follows) {
  g.wake_full = false;
  command(MsgType::Shot, now);
  // Suppress the HALF/FULL tail until everything is up (AllUp clears it), but only if an
  // AllUp will come, i.e. TwoStage has left level 0.
  g.suppress_half = g.suppress_full = tail_follows;
}

void handle_stage(StageEvent e, uint32_t now) {
  if (e != StageEvent::None) g.ping_gate = false;
  if (g.stuck && e != StageEvent::None) g.stuck = false;  // it moved: not stuck after all
  switch (e) {
    case StageEvent::HalfDown:
      if (g.wake_full) {  // FULL opened before it was debounced: the wake stab, not a focus
        wake_stab_shot(now, true);
        break;
      }
      if (g.suppress_half) break;  // tail of the stab that woke us: its Shot already went
      g.held = true;
      g.held_full = false;  // S6: a new hold starts at half
      command(MsgType::Focus, now);
      break;
    case StageEvent::FullDown:
      g.wake_full = false;  // the normal path: the wake stab is reported as a full press
      if (g.suppress_full) break;  // the wake stab already fired its Shot: never a second
      g.held = true;
      g.held_full = true;  // S6
      command(MsgType::Shoot, now);
      break;
    case StageEvent::FullUp:
    case StageEvent::AllUp:
      if (e == StageEvent::AllUp) g.suppress_half = g.suppress_full = false;
      g.held_full = false;  // S6
      if (g.held) {
        g.held = false;
        g.release_ms = now;
        command(MsgType::Release, now);
      }
      break;
    default: break;
  }
}

// Feeds the debouncers. Also runs about every millisecond inside a blocking radio_send
// (radio_set_wait_hook), so a short press during a 1.5-2 s SF12 transmit is not missed;
// events found there are queued and acted on by inputs() afterwards.
void sample_buttons() {
  const uint32_t now = millis();
  if (g.ob) {
    const PressEvent pe = g.pc.update(digitalRead(pins::BTN_PRG) == LOW, now);
    if (pe.type != PressEvt::None && g.pev_n < PEVQ) g.pev[g.pev_n++] = pe;
    if (g.aux_on) {
      const PressEvent ae = g.auxpc.update(digitalRead(pins::BTN_HALF) == LOW, now);
      if (ae.type != PressEvt::None && g.aev_n < PEVQ) g.aev[g.aev_n++] = ae;
    }
    return;
  }
  const BtnEvent e = g.prg.update(digitalRead(pins::BTN_PRG) == LOW, now);
  if (e != BtnEvent::None && g.prg_n < EVQ) g.prg_ev[g.prg_n++] = e;
  if (g.prg.isDown()) g.prg_seen_down = true;
  if (g.s->two_stage) {
    const bool half = digitalRead(pins::BTN_HALF) == LOW;
    const bool full = digitalRead(pins::BTN_FULL) == LOW;
    const StageEvent se = g.two.update(half, full, now);
    if (se != StageEvent::None && g.st_n < EVQ) g.st_ev[g.st_n++] = se;
  }
}

void ob_inputs(uint32_t now);

void inputs(uint32_t now) {
  sample_buttons();
  if (g.ob) {
    ob_inputs(now);
    return;
  }
  for (uint8_t i = 0; i < g.prg_n; i++) handle_prg(g.prg_ev[i], now);
  g.prg_n = 0;
  for (uint8_t i = 0; i < g.st_n; i++) handle_stage(g.st_ev[i], now);
  g.st_n = 0;
  // S6: every queued event is handled, so a hold with the switch fully up means its AllUp was
  // dropped by a full queue: let go now, or the camera keeps AF / the shutter and we never sleep.
  if (g.s->two_stage && g.held && g.two.level() == 0) handle_stage(StageEvent::AllUp, now);
  // S6: the same for a dropped FullUp with the switch back on half: the full press is over,
  // so its Release goes out now and the camera does not keep the shutter down.
  if (g.s->two_stage && g.held && g.held_full && g.two.level() < 2)
    handle_stage(StageEvent::FullUp, now);
  // FULL woke us, still down at boot, then opened before TwoStage reported anything (both
  // contacts open, no event at all): still the wake stab.
  if (g.wake_full) {
    if (digitalRead(pins::BTN_FULL) == LOW || digitalRead(pins::BTN_HALF) == LOW) {
      if (digitalRead(pins::BTN_FULL) == LOW) g.wake_full_up_since = now;
    } else if (now - g.wake_full_up_since >= WAKE_STAB_UP_MS && g.two.level() == 0) {
      wake_stab_shot(now, false);
    }
  }
  // PRG woke us and was let go before the debouncer ever saw it held: still a click.
  if (g.wake_click) {
    if (digitalRead(pins::BTN_PRG) == LOW) {
      g.prg_up_since = now;
    } else if (!g.prg_seen_down && now - g.prg_up_since >= 30) {
      g.wake_click = false;
      g.swallow_long = false;
      g.ping_gate = false;
      command(MsgType::Shot, now);
    }
  }
}

void ob_wake(esp_sleep_wakeup_cause_t cause, uint32_t now);

// Act on the press that woke us before anything else.
void handle_wake(uint32_t now) {
  if (!hw_woke_from_sleep()) return;
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (g.ob) {
    ob_wake(cause, now);
    return;
  }
  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    const bool still_down = digitalRead(pins::BTN_PRG) == LOW;
    if (g.s->two_stage) {
      g.swallow_prg = still_down;
    } else if (still_down) {
      // Decided by what follows: Click -> Shot, Long -> just looking. If it is let go
      // before the debouncer registers it, inputs() still fires the Shot.
      g.swallow_long = true;
      g.wake_click = true;
      g.prg_seen_down = false;
      g.prg_up_since = now;
    } else {
      command(MsgType::Shot, now);  // quick click, released during boot
    }
  } else if (cause == ESP_SLEEP_WAKEUP_EXT1 && g.s->two_stage) {
    const uint64_t mask = esp_sleep_get_ext1_wakeup_status();
    // Debounced: FULL counts as still down if it reads LOW at any point in ~20 ms (setup
    // path, once per wake). One bouncy read must not make one stab both a Shot and a Shoot.
    bool full_now = false;
    for (uint8_t i = 0; i < 20 && !full_now; i++) {
      full_now = digitalRead(pins::BTN_FULL) == LOW;
      if (!full_now) delay(1);
    }
    // FULL closed the wake: a full stab. If FULL has already opened it was a complete
    // press: one focus-shoot-release, and the HALF contact still closing is its tail, not
    // a new focus press. Still fully held: TwoStage reports it on the first loop.
    if ((mask & (1ULL << pins::BTN_FULL)) && !full_now) {
      command(MsgType::Shot, now);
      g.suppress_half = digitalRead(pins::BTN_HALF) == LOW;
      g.suppress_full = g.suppress_half;  // cleared on AllUp, which then still comes
    } else if ((mask & (1ULL << pins::BTN_FULL)) && full_now) {
      // Still down: TwoStage should report FullDown on the first loop. If FULL opens before
      // it is debounced, inputs()/handle_stage still turn the stab into one Shot.
      g.wake_full = true;
      g.wake_full_up_since = now;
    }
  }
}

// ---------- one button (press style A, B, C) ----------

bool lora_fresh(uint32_t now);

ObSettings ob_settings(const Settings& s) {
  ObSettings o;
  o.style = (PressStyle)(s.press <= 3 ? s.press : 1);
  o.af_limit_s = s.af_limit_s;
  o.aflock_s = s.aflock_s;
  o.timer_s = s.timer_s;
  o.iv_every_s = s.iv_every_s;
  o.iv_count = s.iv_count;
  o.burst_ms = s.burst_ms;
  o.bulb_s = s.bulb_s;
  o.hold_cap_s = s.hold_cap_s;
  o.focus_ms = s.focus_ms;
  o.wake_act = s.wake_act;
  o.aux = s.aux_pin != 0 && !s.two_stage;
  o.n_modes = s.n_modes > OB_MAX_MODES ? OB_MAX_MODES : s.n_modes;
  for (uint8_t i = 0; i < OB_MAX_MODES; i++) o.modes[i] = (ObMode)s.modes[i];
  return o;
}

// The engine's settings for the buttons that really work this boot: aux only if the second
// button was set up at boot and is not stuck. Otherwise PRG must keep its MODES zones, or the
// mode card, views and OFF become unreachable (a typed 'aux 6' only applies after a reboot).
ObSettings ob_live_settings() {
  ObSettings o = ob_settings(*g.s);
  o.aux = o.aux && g.aux_on;
  return o;
}

// What the engine changed (card picks, value editor) goes back into Settings and NVS.
void ob_save() {
  const ObSettings& o = g.eng.settings();
  Settings& s = *g.s;
  s.timer_s = o.timer_s;
  s.aflock_s = o.aflock_s;
  s.iv_every_s = o.iv_every_s;
  s.iv_count = o.iv_count;
  s.burst_ms = o.burst_ms;
  s.bulb_s = o.bulb_s;
  // Only SHOT and AF LOCK are kept across a power-on (spec 1.3).
  s.ob_mode = g.eng.mode() == ObMode::AfLock ? 1 : 0;
  store_save(s);
}

ObEnv ob_env(uint32_t now) {
  ObEnv e;
  e.now = now;
  e.relay = g.relay;
  e.link_ok = g.relay ? lora_fresh(now) : fuji.ready();
  e.sf = g.relay && g.radio_ok ? radio_sf() : g.s->last_sf;
  e.hh_permille = g.relay ? duty_permille() : 0;
  e.cam_permille = g.relay ? g.cam_duty : 0;
  e.batt = g.batt;
  if (g.relay) {
    // A timed Shot leaves this early: its own airtime and the Ack, or the last round trip.
    const uint32_t est = radio_airtime_ms(PROTO_HDR + 6 + PROTO_TAG) + radio_airtime_ms(ACK_FRAME_LEN);
    e.lead_ms = g.rtt > est ? g.rtt : est;
  }
  uint8_t n;
  pages(n);
  e.n_views = n;
  // Relay: the camera board has acked the Focus. Direct: a Focus really went out on a live
  // camera link (command() sends none while BLE is still connecting).
  if (g.relay)
    e.af_confirmed = g.t.type == MsgType::Focus && (uint8_t)g.t.stage >= (uint8_t)ShotStage::Delivered &&
                     g.t.stage != ShotStage::Failed;
  else
    e.af_confirmed = fuji.ready() && g.t.open && g.t.type == MsgType::Focus && g.t.stage != ShotStage::Failed;
  return e;
}

ObOutcome outcome_of(const Track& t) {
  switch (t.stage) {
    case ShotStage::Accepted:
    case ShotStage::CamEvent: return ObOutcome::Ok;
    case ShotStage::Failed:
      if (t.duty) return ObOutcome::Duty;
      switch (t.err) {
        case apperr::NO_ACK: return ObOutcome::Fail;  // nothing heard: the link
        case apperr::NO_TX:
        case apperr::QUEUED:
        case apperr::CANCELLED:
        case apperr::BUSY: return ObOutcome::Busy;
        case apperr::NO_RESULT:
        case apperr::STALE: return ObOutcome::Unknown;
        default: return t.err == (uint8_t)FujiErr::Unsure ? ObOutcome::Unknown : ObOutcome::Fail;
      }
    case ShotStage::Sent:
    case ShotStage::Delivered: return ObOutcome::Unknown;
    case ShotStage::Idle: break;
  }
  return ObOutcome::Busy;  // never left the board
}

// Reports the engine's last shot once: when its track has settled, or (force) when a new
// track is about to replace it.
void ob_report(bool force, uint32_t now) {
  if (g.eng_reported || !g.eng_tag) return;
  if (g.t.id != g.eng_track) {
    g.eng_reported = true;
    g.eng.onOutcome(g.eng_tag, ObOutcome::Unknown, ob_env(now));
    return;
  }
  const bool settled = !g.t.open || final_stage(g.t.stage) ||
                       (g.t.stage == ShotStage::Delivered && g.t.err == apperr::NO_RESULT);
  if (!settled && !force) return;
  g.eng_reported = true;
  g.eng.onOutcome(g.eng_tag, outcome_of(g.t), ob_env(now));
}

// A Shot/Shoot command opened a new track: the engine's tag now follows it.
void ob_follow(uint16_t tag, uint16_t track_before, uint32_t now) {
  if (g.t.open && g.t.id != track_before) {
    g.eng_tag = tag;
    g.eng_track = g.t.id;
    g.eng_reported = false;
  } else {
    g.eng.onOutcome(tag, ObOutcome::Busy, ob_env(now));  // BUSY, NOT PAIRED, ...
  }
}

[[noreturn]] void ob_restart_into_menu(uint32_t now, MenuNext what = MenuNext::Setup) {
  begin_shutdown(now, "SETUP");  // never restart with a Release still unsent
  // S7: direct has no queue: wait on its unacknowledged Release too, and keep retrying it.
  while ((g.qn || (!g.relay && g.exec.releasePending())) && !after(millis(), g.shut_deadline)) {
    const uint32_t t = millis();
    if (g.relay) {
      relay_rx(t);
      relay_tx(t);
    } else {
      direct_step(t);
    }
    delay(5);
  }
  if (!g.relay) fuji.shutdown(200);
  ui_power(false);
  bootgest_restart_into_menu(what);
}

// A stopped run's shot that may not have left the board yet (ObAct::CancelPending). Direct:
// parked in g.defer waiting for the camera. Relay: still queued and never on air. Either way
// it is dropped and marked CANCELLED; one already out is reported as such (it may fire).
ObCancel ob_cancel_pending(uint16_t tag, uint32_t now) {
  if (!tag || tag != g.eng_tag || !g.eng_track) return ObCancel::None;
  const uint16_t tr = g.eng_track;
  if (!g.relay) {
    if (g.defer.active && g.defer.track == tr) {
      g.defer.active = false;
      if (g.t.id == tr) set_stage(ShotStage::Failed, apperr::CANCELLED, now);
      show_toast("CANCELLED", now);
      return ObCancel::Cancelled;
    }
  } else {
    for (uint8_t i = 0; i < g.qn; i++) {
      const TxCmd& c = g.q[i];
      if (c.track != tr || c.reask || (c.type != MsgType::Shot && c.type != MsgType::Shoot))
        continue;
      if (c.tries != 0) return ObCancel::AlreadySent;
      const TxCmd dead = c;
      remove_at(i);
      cmd_failed(dead, apperr::CANCELLED, now);  // Failed + CANCELLED on its track
      return ObCancel::Cancelled;
    }
  }
  if (g.t.id == tr && g.t.stage != ShotStage::Idle && g.t.stage != ShotStage::Failed)
    return ObCancel::AlreadySent;
  return ObCancel::None;
}

void ob_run_actions(uint32_t now) {
  ObAction a;
  for (uint8_t guard = 0; guard < 24 && g.eng.popAction(a); guard++) {
    switch (a.act) {
      case ObAct::Shot: {
        const uint16_t before = g.t.id;
        command(MsgType::Shot, now, a.arg);
        ob_follow(a.tag, before, now);
        break;
      }
      case ObAct::Focus:
        g.held = true;
        command(MsgType::Focus, now);
        break;
      case ObAct::Shoot: {
        const uint16_t before = g.t.id;
        g.held = true;
        command(MsgType::Shoot, now, FOCUS_DEFAULT, a.arg);  // arg: max hold (0 = held)
        ob_follow(a.tag, before, now);
        break;
      }
      case ObAct::Release:
        if (g.held) {
          g.held = false;
          g.release_ms = now;
        }
        command(MsgType::Release, now);
        break;
      case ObAct::Off:
        if (!g.shutting) begin_shutdown(now, a.arg ? "BUTTON STUCK" : "OFF");
        break;
      case ObAct::Setup:
        if (!g.shutting) ob_restart_into_menu(now);
        break;
      case ObAct::Save: ob_save(); break;
      case ObAct::Led:
        if (g.s->led_cues) g.led.start((uint8_t)a.arg, now);
        break;
      case ObAct::RunFlag: store_put_u8(NVS_RUN_FLAG, a.arg ? 1 : 0); break;
      case ObAct::CancelPending:
        g.eng.onCancel(a.tag, ob_cancel_pending(a.tag, now), ob_env(now));
        break;
    }
  }
}

void ob_force_stop(ObStop why, uint32_t now) {
  g.eng.forceStop(why, ob_env(now));
  ob_run_actions(now);
}

void ob_screen_on(uint32_t now) {
  g.last_press = now;
  if (g.dim) {
    if (g.dim == 2) g.last_ui = now - UI_PERIOD_MS;  // L3: back from dark: a new frame at once
    g.dim = 0;
    ui_power(true);
    ui_contrast(255);
  }
}

void ob_inputs(uint32_t now) {
  // g.pev_n is read again each pass: a blocking command inside ob_run_actions samples the
  // button (wait hooks) and can append events, which must be handled, never wiped.
  for (uint8_t i = 0; i < g.pev_n; i++) {
    const PressEvent e = g.pev[i];
    g.last_activity = now;
    g.ping_gate = false;
    ob_screen_on(now);
    g.eng.onPress(e, ob_env(now));
    // RELEASE NOT CONFIRMED stays up until a press, and that press does nothing else (a
    // run it lands on is still stopped by the press-down above: stop wins).
    if (e.type == PressEvt::Down && g.rel_alert) {
      g.rel_alert = false;
      g.eng.swallowPress();
    }
    ob_run_actions(now);
  }
  g.pev_n = 0;
  // The engine still has a press open but the button is up: its Up was lost (queue full).
  // Close it without firing; anything it held is released. Never left holding AF.
  if (g.eng.pressActive() && !g.pc.isDown()) {
    g.eng.abortPress(ob_env(now));
    ob_run_actions(now);
  }
  for (uint8_t i = 0; i < g.aev_n; i++) {
    const PressEvent e = g.aev[i];
    g.last_activity = now;
    g.ping_gate = false;
    ob_screen_on(now);
    if (g.aux_swallow) {  // the aux press that woke us: wake only
      if (e.type == PressEvt::Up) g.aux_swallow = false;
      continue;
    }
    if (e.type == PressEvt::Enter && e.zone == Zone::Stuck) {
      g.aux_on = false;  // held 15 s: not a finger. Ignored until the next boot
      show_toast("AUX STUCK", now);
      g.eng.configure(ob_live_settings());  // PRG gets its MODES zones back
      continue;
    }
    g.eng.onAux(e, ob_env(now));
    ob_run_actions(now);
  }
  g.aev_n = 0;
  g.pc.setTable(g.eng.table());
}

void ob_wake(esp_sleep_wakeup_cause_t cause, uint32_t now) {
  const ObEnv e = ob_env(now);
  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    if (digitalRead(pins::BTN_PRG) == LOW) {
      // Still down: decided by how long it is held (tap = the wake table, 0.6 s = modes).
      // Hold-to-wake never sends AF: the boot took ~300 ms, the press was not seen cleanly.
      g.pc.setTable(g.eng.wakeTable());
      g.pc.assumeDown(now, now);
      g.eng.wakeDown(e);
    } else {
      g.eng.wakeTap(e);
    }
  } else if (cause == ESP_SLEEP_WAKEUP_EXT1 && g.aux_on) {
    g.eng.wakeTap(e, true);  // the second button only wakes, never fires
    if (digitalRead(pins::BTN_HALF) == LOW) {
      g.auxpc.assumeDown(now, now);
      g.aux_swallow = true;
    }
  }
  g.last_press = now;
  ob_run_actions(now);
}

void ob_step(uint32_t now) {
  if (!g.ob) return;
  if (!g.shutting) {
    g.eng.tick(ob_env(now));
    ob_run_actions(now);
    ob_report(false, now);
    g.pc.setTable(g.eng.table());
  }
  // The engine changes views itself (handle_prg / next_page never run in one-button mode):
  // opening HUNT starts a fresh best, and the duty note may show again, as in classic.
  if (g.views.update(g.eng.view(), now)) {
    if (page() == Page::Hunt) g.hunt_best = -200;
    g.hunt_slowed = false;
  }
  // Direct: a Release the camera has not acknowledged is being retried: say so until a press.
  const bool rp = !g.relay && g.exec.releasePending();
  if (rp && !g.rel_pending_was) g.rel_alert = true;
  g.rel_pending_was = rp;
  // Tally LED: lit while the shutter is down here (direct) and for a moment after any accepted shot.
  // C1: the stamp can be later than now by one blocking camera write (a few seconds at most).
  const int32_t fired_ago = (int32_t)(now - g.t.stage_ms);
  const bool fired = g.t.stage == ShotStage::Accepted && g.t.type != MsgType::Focus &&
                     fired_ago > -10000 && fired_ago < 250;
  hw_tally(fired || (!g.relay && g.exec.shutterDown()));
  const bool on = g.s->led_cues && g.led.on(now);
  if (on != g.led_was) {
    hw_led(on);
    g.led_was = on;
  }
  // A long run on a quiet screen: dim at 15 s, dark at 60 s. A press brings it back (and is
  // still STOP: the engine sees it first).
  uint8_t want = 0;
  if (g.eng.running() && !g.shutting) {
    const uint32_t q = now - g.last_press;
    want = q >= DARK_AFTER_MS ? 2 : q >= DIM_AFTER_MS ? 1 : 0;
  }
  if (want != g.dim) {
    if (want == 2) ui_power(false);
    else {
      if (g.dim == 2) {
        ui_power(true);
        g.last_ui = now - UI_PERIOD_MS;  // L3: back from dark: a new frame at once
      }
      ui_contrast(want == 1 ? DIM_CONTRAST : 255);
    }
    g.dim = want;
  }
}

void ob_fill_ui(UiModel& m, uint32_t now) {
  m.ob.active = true;
  g.eng.screen(m.ob.scr, ob_env(now));
  // A view page (LINK, HUNT, GPS, RANGE) is laid out for all 64 rows: its bottom rows sit
  // where the label bar goes. The bar shows there only while a press is down or just after
  // the view changed, so the page's own last rows are readable the rest of the time.
  m.ob.view_bar = g.views.barVisible(g.eng.view(), g.eng.pressActive(), now);
  m.ob.alert = g.rel_alert;
  m.ob.alert_line = g.relay ? "retrying while the link allows" : "retrying in the background";
  char lat[12] = "";
  if (g.t.rtt) snprintf(lat, sizeof lat, " %u.%us", g.t.rtt / 1000, g.t.rtt / 100 % 10);
  if (g.relay) {
    const uint16_t used = max<uint16_t>(duty_permille(), g.cam_duty);
    snprintf(m.ob.info, sizeof m.ob.info, "#%u%s  AIR %u%%", g.shot_count, lat,
             used >= 1000 ? 0 : (1000 - used) / 10);
  } else {
    snprintf(m.ob.info, sizeof m.ob.info, "#%u%s", g.shot_count, lat);
  }
}

// ---------- GPS, battery, UI ----------

void gps_step(uint32_t now) {
  const Page pg = page();
  const bool want = g.s->hh_gps || pg == Page::Gps || pg == Page::Range || range_on();
  if (want != gps_is_on()) gps_power(want);
  gps_loop();
  if (now - g.last_gps >= GPS_PERIOD_MS) {
    g.last_gps = now;
    g.fix = gps_is_on() ? gps_fix() : GpsFix{};
  }
  if (g.cam_fix && now - g.cam_fix_at > CAM_FIX_FRESH_MS) g.cam_fix = false;
}

void batt_step(uint32_t now) {
  if (g.last_batt && now - g.last_batt < BATT_PERIOD_MS) return;
  g.last_batt = now;
  g.batt = hw_batt_pct();
}

bool lora_fresh(uint32_t now) {
  if (!g.last_ack) return false;
  const uint32_t fresh = max<uint32_t>(10000, 3 * ping_interval());
  return now - g.last_ack < fresh;
}

void fill_link(UiModel& m, uint32_t now) {
  if (g.relay) {
    const bool fresh = lora_fresh(now);
    m.cam = fresh ? g.cam : CamLink::None;
    m.cam_text = fresh ? app_camlink_text(g.cam) : "NO LINK";
    m.ble_rssi = fresh ? g.cam_ble : 0;
    m.lora_ok = fresh;
    m.bars = fresh ? g.link.bars() : 0;
    m.margin_db = g.link.lastMarginDb();
    // Old figures from a link that has gone would read as signal we do not have.
    m.rssi_up = fresh ? g.rssi_up : NAN;
    m.snr_up = fresh ? g.snr_up : NAN;
    m.rssi_dn = fresh ? g.rssi_dn : NAN;
    m.snr_dn = fresh ? g.snr_dn : NAN;
    m.sf = g.radio_ok ? radio_sf() : g.s->last_sf;
    m.tx_out_dbm = g.radio_ok ? radio_tx_out_dbm() : 0;
    m.success_pct = g.link.successPct();
    m.rtt_ms = fresh ? g.rtt : 0;  // "RTT --" once the link is gone, not a stale figure
    m.link_unchecked = g.ping_gate && !g.last_ack;  // power-on: nothing sent yet
    // Gate open, but no reply has been due yet (boot hold-off, first frame or its Ack slot
    // still running): "checking", never "no reply" before anything was asked.
    m.link_checking = !g.ping_gate && !g.last_ack && !g.link_asked;
    m.rescue = g.rescue;
    m.adaptive = g.s->adaptive_sf;
    m.duty_permille = duty_permille();
    m.cam_batt = fresh ? g.cam_batt : 255;
    m.hunt_rssi = now - g.last_ack <= 2 * ping_interval() + ack_timeout(PING_FRAME_LEN)
                      ? g.hunt_rssi
                      : 0;
    m.hunt_best = g.hunt_best;
  } else {
    m.cam = fuji_camlink();
    m.cam_text = fuji.stateText();
    m.cam_name = fuji.cameraName();
    m.ble_rssi = fuji.rssi();
    m.bars = ble_bars(m.ble_rssi);
    m.hunt_rssi = m.ble_rssi;
    m.sf = 0;
  }
}

void fill_gps(UiModel& m) {
  m.gps_on = gps_is_on();
  m.hh_fix = g.fix.valid;
  m.cam_fix = g.relay && g.cam_fix;
  m.sats = g.fix.sats;
  m.hdop = g.fix.hdop;
  m.own_lat = g.fix.lat;
  m.own_lon = g.fix.lon;
  if (m.hh_fix && m.cam_fix) {
    m.dist_m = geo_distance_m(g.fix.lat, g.fix.lon, g.cam_lat, g.cam_lon);
    m.bearing_deg = geo_bearing_deg(g.fix.lat, g.fix.lon, g.cam_lat, g.cam_lon);
  }
}

void ui_step(uint32_t now) {
  // L3: the screen is dark (long run, no press): no frames, each one blocks the loop ~30 ms.
  // Toasts still run out, so none is shown stale when the screen comes back.
  if (g.dim == 2) {
    if (g.toast && after(now, g.toast_until)) g.toast = nullptr;
    return;
  }
  if (now - g.last_ui < (ui_animating(now) ? 30u : UI_PERIOD_MS)) return;
  g.last_ui = now;
  UiModel m;
  m.role = Role::Handheld;
  m.mode = g.relay ? Mode::Relay : Mode::Direct;  // what runs now, not a console edit
  m.page = page();
  fill_link(m, now);
  fill_gps(m);
  m.hh_batt = g.batt;
  m.shot.stage = g.t.stage;
  m.shot.at_ms = g.t.stage_ms;
  m.shot.latency_ms = g.t.rtt;
  m.shot.err = g.t.err;
  m.shot.count = g.shot_count;
  m.shot.focus_only = g.t.type == MsgType::Focus;
  m.shot.late = g.t.late;
  // A press not started yet (direct: waiting for the BLE link; relay: not on air yet).
  if (g.t.open && g.t.stage == ShotStage::Idle) {
    if (g.defer.active && g.defer.track == g.t.id) {
      m.shot.pending = true;
      m.shot.pending_until = g.defer.at + DEFER_SHOT_MS;
    }
    for (uint8_t i = 0; i < g.qn; i++)
      if (g.q[i].track == g.t.id && g.q[i].tries == 0) m.shot.pending = true;
  }
  m.range_on = range_on();
  m.range_n = g.range_n;
  m.range_ok_pct = g.range_n ? 100.f * g.range_ok / g.range_n : 0.f;
  const char* setup_how = g.s->rst_gest ? "Tap RST twice" : "Hold PRG at boot";
  if (g.shutting) {
    m.big1 = g.shut_msg;
    // S7: say why OFF is waiting while a direct Release is still being retried.
    m.big2 = g.qn ? "Telling camera..."
             : !g.relay && g.exec.releasePending() ? "Releasing camera" : "Release button";
  } else if (g.relay && !g.paired) {
    m.big1 = "PAIR BOARDS";
    m.big2 = setup_how;
  } else if (!g.relay && !fuji.hasSaved()) {
    m.big1 = "PAIR CAMERA";
    m.big2 = setup_how;
  } else if (g.relay && !g.radio_ok && m.page == Page::Main) {
    m.big1 = "RADIO FAILED";
    m.big2 = "Presses not sent";
  }
  if (g.toast && after(now, g.toast_until)) g.toast = nullptr;
  m.toast = g.toast;
  // S4: also when the receiver cannot be re-armed (deaf).
  if (!m.toast && g.relay && g.radio_ok && (radio_tx_fault_streak() >= 2 || radio_rx_fault()))
    m.toast = "RADIO FAULT";
  if (!m.toast && duty_store_failed()) m.toast = "DUTY STORE FAIL";
  if (!m.toast && g.relay && g.ping_gate && now - hw_boot_ms() < ANTENNA_HINT_MS)
    m.toast = "ANTENNA ON?";
  if (bootgest_hint()) m.toast = "RST AGAIN NOW = SETUP";  // R1: first 3 s after a power-on
  if (g.ob) {
    ob_fill_ui(m, now);
    // While a finger is down the bar is the label: no toast may cover it.
    if (g.eng.pressActive()) m.toast = nullptr;
  }
  ui_render(m, now);
}

}  // namespace

void app_handheld_setup(Settings& s) {
  g.s = &s;
  hw_led_level(s.led_bright == 1 ? 20 : 255);
  hw_tally_begin(s.tally_pin);
  const uint32_t now = millis();
  g.relay = s.mode == Mode::Relay;
  g.last_activity = now;
  pinMode(pins::BTN_PRG, INPUT_PULLUP);
  if (s.two_stage) {
    pinMode(pins::BTN_HALF, INPUT_PULLUP);
    pinMode(pins::BTN_FULL, INPUT_PULLUP);
  }

  // Power-on (not a wake): no automatic transmissions until the user touches a button, in
  // case the antenna is off. A wake press is user input, and opens it at once.
  g.ping_gate = !hw_woke_from_sleep();
  // Power-on (or a reset mid-session): the camera board may have dropped to SF12 while we
  // were gone. The first commands then try the saved SF and SF12 in turn.
  if (!hw_woke_from_sleep()) {
    rtc_sf_unconfirmed = true;
    rtc_rescue_i = 0;
  }
  g.sf_unconfirmed = rtc_sf_unconfirmed;
  radio_set_wait_hook(sample_buttons);
  // Power-on: forget link state from before the reset (RTC memory is not cleared by every
  // kind of reset).
  if (!hw_woke_from_sleep()) {
    rtc_pending_sf = 0;
    rtc_link_valid = false;
    rtc_fixed_backoff = 0;  // K1: with the rest of the link state
    rtc_fixed_waited = 0;
  }
  // K1: carry on the back-off where the last wake left it, not from the start of this wake.
  if (rtc_fixed_backoff) g.last_fixed_try = now - rtc_fixed_waited;
  g.pending_sf = rtc_pending_sf;
  if (hw_woke_from_sleep()) {  // RTC values are only meaningful after a deep-sleep wake
    g.page_i = rtc_page_i;
    g.range_toggle = rtc_range_toggle;
    g.range_n = rtc_range_n;
    g.range_ok = rtc_range_ok;
  } else {
    rtc_page_i = 0;
    rtc_range_toggle = false;
    rtc_range_n = rtc_range_ok = 0;
  }
  if (g.relay) {
    store_load_pair(g.pair);
    g.paired = g.pair.valid;
    g.guard = ReplayGuard(g.pair.peer_ctr);
    const uint8_t sf = constrain(s.last_sf, radiocfg::SF_MIN, radiocfg::SF_MAX);
    g.link = LinkManager(sf);
    g.link.setAdaptive(s.adaptive_sf);
    if (rtc_link_valid) g.link.restore(rtc_link);  // ignored unless still on that SF
    if (g.paired) g.radio_ok = radio_begin(sf, s.ant_gain_dbi);
    if (g.paired && !g.radio_ok) show_toast("RADIO FAILED", now);
    // L5: a wake always transmits: store this boot's counter block now (one NVS write, at times
    // a slow page clean-up), not inside the first press. Costs one unused counter.
    if (g.radio_ok && hw_woke_from_sleep()) store_skip_ctr(0);
    // First ping soon after the boot hold-off, not one full interval (72 s at SF12, longer
    // than a whole wake) after boot: every wake then yields a link sample.
    if (g.radio_ok) g.last_ping_try = now - normal_ping_interval() + timing::PING_MIN_INTERVAL_MS;
  } else {
    app_fuji_start();
    fuji.setAutoConnect(true);
    // Keep sampling the buttons while a BLE write blocks the loop (up to ~2 s), so a quick
    // full press during the focus write is not lost.
    fuji.setWaitHook(sample_buttons);
  }

  g.batt = hw_batt_pct();
  g.last_batt = now;
  // One button: press style A, B or C, and not the two-stage switch (that keeps v0.1).
  g.ob = s.press != 0 && !s.two_stage;
  if (g.ob) {
    g.eng.configure(ob_settings(s));
    if (hw_woke_from_sleep() && rtc_ob_valid) {
      g.eng.setMode((ObMode)rtc_ob_mode);
      g.eng.setView(rtc_page_i);
      // We slept while VIDEO may have been recording: never show TAP: REC (it would stop it).
      if (rtc_ob_rec) g.eng.recUnknownAfterSleep();
    } else {
      g.eng.setMode(s.ob_mode == 1 ? ObMode::AfLock : ObMode::Shot);
      g.eng.setView(0);
    }
    g.views.seed(g.eng.view(), now);  // a wake onto HUNT keeps its value, as classic does
    // A run cut by a reset or power loss: say so once.
    if (store_get_u8(NVS_RUN_FLAG, 0) == 1) {
      store_put_u8(NVS_RUN_FLAG, 0);
      g.eng.noteReset();
    }
    g.aux_on = s.aux_pin == pins::BTN_HALF;
    if (g.aux_on) {
      pinMode(pins::BTN_HALF, INPUT_PULLUP);
      g.auxpc.setTable(g.eng.auxTable());
      // Held LOW at a power-on (not an aux wake): stuck or shorted. Ignored this boot.
      const bool aux_wake = hw_woke_from_sleep() &&
                            esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
      if (!aux_wake && digitalRead(pins::BTN_HALF) == LOW) {
        delay(20);
        if (digitalRead(pins::BTN_HALF) == LOW) {
          g.aux_on = false;
          show_toast("AUX STUCK", now);
        }
      }
    }
    g.eng.configure(ob_live_settings());  // aux only if the second button really works
    g.pc.setTable(g.eng.table());
    g.last_press = now;
  }
  handle_wake(now);  // the waking press is queued before the screen is drawn
  ui_power(true);
}

void app_handheld_loop() {
  const uint32_t now = millis();
  if (g.relay) {
    g.link.setAdaptive(g.s->adaptive_sf);
    relay_rx(now);
  } else {
    direct_step(now);
  }
  if (!g.shutting) inputs(now);
  else if (g.shut_idle) sample_buttons();  // C2: a press during an idle SLEEP is still seen
  ob_step(now);
  if (g.relay) relay_tx(now);
  track_step(now);
  flush_log(now);
  gps_step(now);
  batt_step(now);
  if (g.shutting) shutdown_step(now);
  else idle_check(now);
  ui_step(now);
  // NVS duty snapshot only at an idle moment (as the range log), never inside a press.
  if (!g.qn && !g.held && !g.exec.busy() && !g.defer.active && !g.shutting) { duty_utc_tick(); duty_idle(); }
}

void app_handheld_print_stat() {
  const uint32_t now = millis();
  Serial.printf("handheld: mode=%s paired=%d radio=%d\n", g.relay ? "relay" : "direct", g.paired,
                g.radio_ok);
  if (g.relay) {
    Serial.printf("lora: sf=%u link_sf=%u req_sf=%u rescue=%d tx_set=%d tx_out=%d dBm adaptive=%d\n",
                  g.radio_ok ? radio_sf() : 0, g.link.sf(), g.link.requestedSf(), g.rescue,
                  g.radio_ok ? radio_tx_dbm() : 0, g.radio_ok ? radio_tx_out_dbm() : 0,
                  g.link.adaptive());
    Serial.printf("link: ok=%d last_ack=%ldms ago success=%.0f%% margin=%.1f dB misses=%u rtt=%u ms\n",
                  lora_fresh(now), g.last_ack ? (long)(now - g.last_ack) : -1L,
                  g.link.successPct(), g.link.lastMarginDb(), g.link.consecutiveMisses(), g.rtt);
    Serial.printf("up: rssi=%.0f snr=%.1f  down: rssi=%.0f snr=%.1f\n", g.rssi_up, g.snr_up,
                  g.rssi_dn, g.snr_dn);
    Serial.printf("camera board: cam=%s ble_rssi=%d batt=%u duty=%u/1000 fix=%d\n",
                  app_camlink_text(g.cam), g.cam_ble, g.cam_batt, g.cam_duty, g.cam_fix);
    Serial.printf("duty: used=%lu ms (%u/1000) queue=%u\n", (unsigned long)duty_used_ms(),
                  duty_permille(), g.qn);
  } else {
    Serial.printf("ble: state=%s ready=%d saved=%d rssi=%d name=%s\n", fuji.stateText(),
                  fuji.ready(), fuji.hasSaved(), fuji.rssi(), fuji.cameraName());
  }
  Serial.printf("shot: n=%u stage=%u err=%u latency=%u ms tries=%u\n", g.shot_count,
                (unsigned)g.t.stage, g.t.err, g.t.rtt, g.t.tries);
  Serial.printf("gps: on=%d fix=%d sats=%u hdop=%.1f lat=%.7f lon=%.7f\n", gps_is_on(),
                g.fix.valid, g.fix.sats, g.fix.hdop, g.fix.lat, g.fix.lon);
  if (g.fix.valid && g.cam_fix)
    Serial.printf("to camera: %.1f m bearing %.0f\n",
                  geo_distance_m(g.fix.lat, g.fix.lon, g.cam_lat, g.cam_lon),
                  geo_bearing_deg(g.fix.lat, g.fix.lon, g.cam_lat, g.cam_lon));
  Serial.printf("batt=%u%% range_on=%d rows=%u ok=%u\n", g.batt, range_on(), g.range_n,
                g.range_ok);
  if (g.ob)
    Serial.printf("one button: press=%s mode=%s view=%u running=%d held=%d busy=%d\n",
                  ob_style_key(g.eng.settings().style), ob_mode_key(g.eng.mode()), g.eng.view(),
                  g.eng.running(), g.held, g.eng.busy());
  else
    Serial.printf("one button: off (%s)\n", g.s->two_stage ? "two-stage switch" : "press classic");
}

bool app_handheld_request_menu(uint8_t what) {
  if (!g.s) return false;
  ob_restart_into_menu(millis(), (MenuNext)what);
}

void app_handheld_settings_changed() {
  if (g.s) hw_led_level(g.s->led_bright == 1 ? 20 : 255);
  if (!g.s || !g.ob) return;
  // Values apply now (a run in progress keeps the values it started with). The press style
  // itself, aux and two-stage only change at the next boot.
  g.eng.configure(ob_live_settings());
  g.pc.setTable(g.eng.table());
}

void app_handheld_note_activity() {
  if (g.s) g.last_activity = millis();
}

void app_handheld_print_json_status() {
  static LineBuf b;  // one write: a [fuji] log line from core 0 can never split it
  const uint32_t now = millis();
  b.put('{');
  b.jstr("role", "handheld");
  b.jstr("link_mode", g.relay ? "relay" : "direct");
  b.printf("\"one_button\":%s,", g.ob ? "true" : "false");
  b.jstr("press", ob_style_key(g.ob ? g.eng.settings().style : PressStyle::Classic));
  b.jstr("shoot_mode", g.ob ? ob_mode_key(g.eng.mode()) : "shot");
  b.printf("\"view\":%u,\"running\":%s,\"held\":%s,\"busy\":%s,", g.ob ? g.eng.view() : g.page_i,
           g.ob && g.eng.running() ? "true" : "false", g.held ? "true" : "false",
           g.ob && g.eng.busy() ? "true" : "false");
  if (g.ob) {
    ObScreen sc;
    g.eng.screen(sc, ob_env(now));
    b.jstr("screen", sc.big);
    b.jstr("line1", sc.l1);
    b.jstr("line2", sc.l2);
    b.jstr("bar", sc.bar);
  }
  b.printf("\"shots\":%u,\"stage\":%u,\"err\":%u,\"latency_ms\":%u,", g.shot_count,
           (unsigned)g.t.stage, g.t.err, g.t.rtt);
  b.printf("\"batt\":%u,", g.batt);
  if (g.relay) {
    b.printf("\"paired\":%s,\"radio\":%s,\"link_ok\":%s,\"sf\":%u,\"duty_permille\":%u,"
             "\"cam_duty_permille\":%u,\"cam_batt\":%u,",
             g.paired ? "true" : "false", g.radio_ok ? "true" : "false",
             lora_fresh(now) ? "true" : "false", g.radio_ok ? radio_sf() : 0, duty_permille(),
             g.cam_duty, g.cam_batt);
    b.jstr("camera", app_camlink_text(g.cam));
  } else {
    b.printf("\"camera_ready\":%s,\"camera_saved\":%s,\"ble_rssi\":%d,",
             fuji.ready() ? "true" : "false", fuji.hasSaved() ? "true" : "false", fuji.rssi());
    b.jstr("camera", fuji.stateText());
  }
  b.printf("\"release_alert\":%s}", g.rel_alert ? "true" : "false");
  b.send();
}
