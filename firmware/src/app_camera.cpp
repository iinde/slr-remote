// Camera board role: sits in the hot shoe, holds the BLE link to the X-E5 and relays
// authenticated LoRa commands from the handheld. Every valid frame is acked at once,
// before any BLE work; the BLE outcome follows in a Result frame. No LoRa TX ever starts
// while a one-press holds the shutter down (it would hold it for the whole airtime).
#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "app.h"
#include "app_internal.h"
#include "buttons.h"
#include "bootgest.h"
#include "cam_exec.h"
#include "config.h"
#include "duty.h"
#include "utc.h"
#include "fuji.h"
#include "gps.h"
#include "hw.h"
#include "linkmgr.h"
#include "proto.h"
#include "radio.h"
#include "store.h"
#include "ui.h"

namespace {

constexpr uint32_t UI_PERIOD_MS = 100;
constexpr uint32_t BATT_PERIOD_MS = 10000;
constexpr uint32_t GEOTAG_PERIOD_MS = 1000;
constexpr uint32_t GPS_WANT_HOLD_MS = 30000;  // handheld asked for our fix recently
constexpr uint32_t LINK_FRESH_MS = 30000;
constexpr uint16_t MAX_FOCUS_MS = 5000;
constexpr uint32_t ANTENNA_HINT_MS = 6000;
constexpr uint8_t PING_FLEN = PROTO_HDR + PING_PAYLOAD_LEN + PROTO_TAG;
constexpr uint8_t ACK_FLEN = PROTO_HDR + ACK_PAYLOAD_LEN + PROTO_TAG;
// A shutter command (Shot/Focus/Shoot) runs only if it echoes the counter of a frame we sent
// within this window: a recording of it replayed later (after jamming us while the user
// pressed) is refused NEED_FRESH. Release is exempt (idempotent, never fires anything).
constexpr uint32_t FRESH_WINDOW_MS = 30000;
constexpr uint32_t CAM_TOAST_MS = 4000;
constexpr uint32_t OFF_STUCK_MS = 10000;  // OFF asked, PRG still LOW this long: stuck
constexpr uint32_t RESULT_GAP_MS = 250;   // quiet time between our Ack and a Result
// S5: a Ping kept back over one of our own transmits (radio.cpp stash) is not Acked once it
// is older than one ping airtime plus this: the handheld's Ack slot (ping + Ack airtime +
// 150 ms from the end of its ping) would close before our Ack was through.
constexpr uint32_t PING_ACK_LATE_MS = 100;

const Page PAGES[] = {Page::Camera, Page::Link, Page::Gps};

struct Camera {
  Settings* s = nullptr;
  PairRecord pair;
  bool radio_ok = false;
  ReplayGuard guard;
  uint32_t boot_floor = 0;  // reserved peer counter at boot: commands first sent at or below
                            // it may already have run before a reset, so never run them
  SeqDedupe dedupe;
  SilenceWatch watch;
  CamExec exec;
  CamOutcome pend[8];       // Results waiting for the Release step: our own (BUSY/STALE/
                            // STORE) and exec outcomes held back while the button is down
  uint8_t pend_n = 0;
  struct TagId {
    uint32_t tag = 0;       // make_tag(type, seq): what CamExec outcomes carry
    uint32_t id = 0;        // full command id (cmd_first_ctr)
  } ids[12];                 // recent commands, newest at ids_head - 1
  uint8_t ids_head = 0;
  struct {
    bool valid = false;
    uint32_t id = 0;        // full command id the cached Result answers
    MsgType type = MsgType::Shot;
    AckPayload a{};
  } last_res;               // resent in place of an Ack when the handheld retries
  ClickButton prg;

  // last frame from the handheld
  uint32_t last_rx = 0;
  uint32_t prev_rx = 0;          // last_rx before the frame being handled now
  uint32_t hold_silence_ms = 0;  // hold watchdog limit at the SF the handheld was last heard on
  uint32_t fresh_ms = 0;         // L2: fresh window at that SF (our own rescue must not widen it)
  float rssi_up = NAN, snr_up = NAN;   // what we measured (handheld -> camera board)
  float rssi_dn = NAN, snr_dn = NAN;   // what the handheld reported hearing from us
  uint8_t hh_batt = 255;
  uint32_t gps_want_at = 0;
  bool rescued = false;

  UiShot shot;
  uint8_t page_i = 0;
  bool screen_on = true;
  uint32_t screen_at = 0, last_ui = 0;

  GpsFix fix{};
  uint32_t last_geotag = 0;
  bool geotag_applied = false;
  uint8_t batt = 255;
  uint32_t last_batt = 0;

  bool off = false;
  uint32_t off_at = 0;
  bool antenna_hint = false;
  bool store_fail = false;  // own TX counter could not be persisted: nothing can be sent

  struct OwnTx {
    uint32_t ctr = 0, at = 0;
  } own_tx[16];             // our recent frames on air (freshness binding of commands)
  uint8_t own_head = 0;
  uint32_t held_tag = 0;    // the Focus/Shoot holding the button (hold watchdog Result)
  bool hold_live = false;   // a Focus hold is being kept: its Shoot needs no fresh echo
  uint32_t hold_floor = 0;  // newest own counter sent before that Focus was answered
  // A timed Shoot lets go by itself, but the handheld still sends its backup Release about
  // OB_SHUTTER_HOLD_MS later. A Result sent in that gap collides with it (half duplex) and is
  // lost (seen on the bench: hold-then-let-go never showed ACCEPTED). Results wait for the
  // Release, or until this time if it never comes.
  uint32_t hold_results_until = 0;
  uint32_t last_reply_at = 0;  // millis() when our last Ack/Result finished going out
  const char* toast = nullptr;
  uint32_t toast_until = 0;
};

Camera c;

void wake_screen(uint32_t now) {
  c.screen_at = now;
  if (!c.screen_on) {
    c.screen_on = true;
    ui_power(true);
  }
}

CamLink cam_link() {
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

void fill_status(AckPayload& a, uint32_t now) {
  a.utc = utc_now();
  a.rx_rssi = app_clamp8(c.rssi_up);
  a.rx_snr_q4 = app_clamp8(c.snr_up * 4.f);
  a.cam_batt = c.batt;
  a.cam_link = cam_link();
  a.ble_rssi = fuji.rssi();
  a.sf = radio_sf();
  a.gps_flags = (c.fix.valid ? 1 : 0) | (c.s->geotag ? 2 : 0);
  if (c.fix.valid) {
    a.lat_e7 = (int32_t)lround(c.fix.lat * 1e7);
    a.lon_e7 = (int32_t)lround(c.fix.lon * 1e7);
  }
  a.duty_permille = duty_permille();
  (void)now;
}

// priority: answers to Shot/Focus/Shoot/Release/SetSf/Sleep may use the 20 % reserve. Ping
// acks are housekeeping and must leave it for commands (a Sleep Ack is short, once per
// session, and an unanswered Sleep costs the handheld 4 SF12 retries and an unconfirmed SF).
// Returns NotStarted if nothing went on air; TimedOut (started, TX_DONE missed) has most
// likely gone out and is booked like Done.
RadioTx send_reply(MsgType type, uint16_t seq, const AckPayload& a, uint32_t now, bool priority) {
  if (!c.radio_ok || now - hw_boot_ms() < radiocfg::BOOT_TX_HOLDOFF_MS) return RadioTx::NotStarted;
  const uint32_t air = radio_airtime_ms(ACK_FLEN);
  if (!duty_can_send(air, priority)) return RadioTx::NotStarted;
  // Never burn a counter on a refused send (checked above).
  Frame f{};
  f.type = type;
  f.net = c.pair.net;
  f.ctr = store_next_ctr();
  if (f.ctr == 0) {  // counter block not persisted: reusing counters would break the link
    c.store_fail = true;
    return RadioTx::NotStarted;
  }
  f.seq = seq;
  f.len = ACK_PAYLOAD_LEN;
  ack_pack(a, f.payload);
  uint8_t buf[PROTO_MAX_FRAME];
  const size_t n = frame_encode(f, c.pair.key, buf, sizeof buf);
  if (!n) return RadioTx::NotStarted;
  const RadioTx r = radio_send(buf, (uint8_t)n);
  if (radio_tx_started(r)) {
    duty_record(air);
    c.own_tx[c.own_head] = {f.ctr, millis()};
    c.last_reply_at = millis();
    c.own_head = (uint8_t)((c.own_head + 1) % (sizeof(c.own_tx) / sizeof(c.own_tx[0])));
  }
  return r;
}

// Never below FRESH_WINDOW_MS. At SF11/12 the idle ping interval is longer than 30 s, so the
// window follows the link cadence: a press between two answered pings must still be fresh.
uint32_t fresh_window_ms() {
  const uint32_t ack = radio_airtime_ms(ACK_FLEN);
  const uint32_t w = ping_interval_ms(radio_airtime_ms(PING_FLEN), ack,
                                      timing::PING_MIN_INTERVAL_MS) + 2 * ack + 2000;
  return w > FRESH_WINDOW_MS ? w : FRESH_WINDOW_MS;
}

uint32_t silence_ms();

// The command echoes the counter of a frame we put on air within fresh_window_ms().
bool fresh_echo(const Frame& f, uint32_t now) {
  const uint8_t off = f.type == MsgType::Shot ? 2 : 0;
  if (f.len < off + 4) return false;
  const uint32_t echo = (uint32_t)f.payload[off] | (uint32_t)f.payload[off + 1] << 8 |
                        (uint32_t)f.payload[off + 2] << 16 | (uint32_t)f.payload[off + 3] << 24;
  if (echo == 0) return false;
  const uint32_t t = millis();  // own_tx times are millis() at send, possibly after loop's now
  // L2: the window for the SF the handheld was last heard on. Our own SF12 rescue (silence,
  // which jamming can cause) must not stretch it for a recorded command.
  const uint32_t win = c.fresh_ms ? c.fresh_ms : fresh_window_ms();
  // We send nothing during a successful half-press, so the Shoot that ends it can only echo a
  // frame from the start of the hold. Accept that at any age while the same hold is still
  // kept and the handheld was heard within the hold watchdog's own limit just before this
  // frame (a recording can only pass the counter check if nothing newer was heard, so it ages
  // out with the hold). X11: not the fresh window, which is shorter than the watchdog, so one
  // lost keepalive at SF11/SF12 brought NEED_FRESH back while the hold was still kept.
  const uint32_t hold_lim = c.hold_silence_ms ? c.hold_silence_ms : silence_ms();
  const bool on_hold = f.type == MsgType::Shoot && c.hold_live && c.exec.buttonDown() &&
                       !c.exec.shutterDown() && !c.exec.oneShotRunning() &&
                       now - c.prev_rx <= hold_lim;
  for (const auto& e : c.own_tx)
    if (e.ctr == echo && (t - e.at <= win || (on_hold && e.ctr > c.hold_floor))) return true;
  return false;
}

void cam_toast(const char* msg, uint32_t now) {
  c.toast = msg;
  c.toast_until = now + CAM_TOAST_MS;
}

bool is_command(MsgType t) {
  return t == MsgType::Shot || t == MsgType::Focus || t == MsgType::Shoot ||
         t == MsgType::Release || t == MsgType::SetSf;
}

RadioTx send_ack(const Frame& f, uint32_t now) {
  AckPayload a{};
  a.ack_seq = f.seq;
  a.ack_type = f.type;
  a.ack_id_hi = cmd_id_hi(cmd_first_ctr(f.ctr, f.seq));
  a.stage = ShotStage::Delivered;
  fill_status(a, now);
  return send_reply(MsgType::Ack, f.seq, a, now, is_command(f.type) || f.type == MsgType::Sleep);
}

uint32_t make_tag(MsgType type, uint16_t seq) { return (uint32_t)type << 16 | seq; }

// Remember which full command id a CamExec tag stands for (tags are only type + 16-bit seq).
void note_id(uint32_t tag, uint32_t id) {
  c.ids[c.ids_head] = {tag, id};
  c.ids_head = (uint8_t)((c.ids_head + 1) % (sizeof(c.ids) / sizeof(c.ids[0])));
}

bool id_for_tag(uint32_t tag, uint32_t& id) {
  const uint8_t n = sizeof(c.ids) / sizeof(c.ids[0]);
  for (uint8_t k = 1; k <= n; k++) {  // newest first
    const auto& e = c.ids[(c.ids_head + n - k) % n];
    if (e.tag == tag && e.id) {
      id = e.id;
      return true;
    }
  }
  return false;
}

void queue_result(const CamOutcome& o) {
  if (c.pend_n < sizeof(c.pend) / sizeof(c.pend[0])) c.pend[c.pend_n++] = o;
}

void send_result(const CamOutcome& o, uint32_t now) {
  AckPayload a{};
  a.ack_seq = (uint16_t)(o.tag & 0xFFFF);
  a.ack_type = (MsgType)(o.tag >> 16);
  a.stage = o.stage;
  a.err = o.err;
  uint32_t id = 0;
  const bool known = id_for_tag(o.tag, id);
  a.ack_id_hi = cmd_id_hi(id);
  fill_status(a, now);
  if (o.release) {
    // A Release write's outcome (two-stage Release, or a one-press's own Release step under
    // the Shot's id): typed Release so the handheld never reads it as the shot's outcome.
    // Not cached for retries (it would hide the press's own Result).
    a.ack_type = MsgType::Release;
    // Tag 0 is the release-on-connect before any press: no command to answer, toast only.
    if (o.tag) send_reply(MsgType::Result, a.ack_seq, a, now, true);
    cam_toast(o.stage == ShotStage::Accepted ? "RELEASE OK"
              : o.err == apperr::GAVE_UP    ? "RELEASE GAVE UP"
                                            : "RELEASE FAILED",
              now);
    return;
  }
  // Cached for retries only when we know exactly which command it answers.
  c.last_res.valid = known;
  c.last_res.id = id;
  c.last_res.type = a.ack_type;
  c.last_res.a = a;
  send_reply(MsgType::Result, a.ack_seq, a, now, true);
  // CamEvent also after an Unsure shutter write (the camera fired after all).
  if (o.stage != ShotStage::CamEvent || c.shot.stage == ShotStage::Accepted ||
      (c.shot.stage == ShotStage::Failed && c.shot.err == (uint8_t)FujiErr::Unsure)) {
    c.shot.stage = o.stage;
    c.shot.err = o.err;
    c.shot.at_ms = now;
  }
}

void set_sf(uint8_t sf) {
  // Only remember an SF the radio is really on (a failed RX restart alone still counts: the
  // SF did change), or a reboot would start on an SF the radio never took.
  if (!radio_set_sf(sf) && radio_sf() != sf) return;
  if (c.s->last_sf != sf) {
    c.s->last_sf = sf;
    store_save(*c.s);
  }
}

// now must be taken after any blocking Ack (see handle_command): CamExec times the AF wait
// and shutter hold from it.
void run_command(const Frame& f, uint32_t now) {
  const uint32_t tag = make_tag(f.type, f.seq);
  if (f.type != MsgType::Release) {  // a Release is not a shot: keep the press's stage
    c.shot.stage = ShotStage::Delivered;  // before startShot, so a BUSY Result isn't overwritten
    c.shot.at_ms = now;
  }
  if (f.type != MsgType::Focus) c.hold_live = false;  // only a Focus hold skips the fresh echo
  switch (f.type) {
    case MsgType::Shot: {
      uint16_t focus = timing::DEFAULT_FOCUS_MS;
      if (f.len >= 2) focus = (uint16_t)(f.payload[0] | f.payload[1] << 8);
      if (focus > MAX_FOCUS_MS) focus = MAX_FOCUS_MS;
      if (c.exec.startShot(now, focus, tag)) c.shot.count++;  // only shots that started
      else queue_result({tag, ShotStage::Failed, apperr::BUSY});
      break;
    }
    case MsgType::Focus:
      c.held_tag = tag;
      c.exec.half(now, tag);
      break;
    case MsgType::Shoot: {
      c.held_tag = tag;
      c.shot.count++;
      // A timed Shoot (one-button "shoot now") lets go by itself; its Release frame is a
      // backup. A plain one (held burst/bulb, two-stage full) waits for the Release.
      const uint16_t hold = shoot_hold_ms(f.payload, f.len);
      if (hold) {
        const uint8_t rel_len = PROTO_HDR + PROTO_TAG;  // a Release carries no payload
        c.hold_results_until = millis() + hold + 2 * radio_airtime_ms(rel_len) + 600;
        c.exec.fullTimed(now, tag, hold);
      }
      else c.exec.full(now, tag);
      break;
    }
    case MsgType::Release:
      c.held_tag = 0;
      c.hold_results_until = 0;  // the backup Release is in: Results may go now
      c.exec.release(now, tag);
      break;
    default: return;
  }
  c.exec.poll(millis());
}

// NEED_FRESH: answered with a Result straight away (no Ack first: it proves delivery too, and
// saves an airtime). Cached, so a retry of the same id gets the same answer. Nothing ran.
void send_need_fresh(const Frame& f, uint32_t id, uint32_t now) {
  AckPayload a{};
  a.ack_seq = f.seq;
  a.ack_type = f.type;
  a.ack_id_hi = cmd_id_hi(id);
  a.stage = ShotStage::Failed;
  a.err = apperr::NEED_FRESH;
  fill_status(a, now);
  c.last_res.valid = true;
  c.last_res.id = id;
  c.last_res.type = f.type;
  c.last_res.a = a;
  send_reply(MsgType::Result, f.seq, a, now, true);
}

// A shutter-type command. Executes each command id once, ever: the RAM ring catches retries,
// boot_floor catches retries of commands first sent before a reset of this board.
void handle_command(const Frame& f, uint32_t now, bool persisted) {
  wake_screen(now);
  const uint32_t id = cmd_first_ctr(f.ctr, f.seq);
  if (!c.dedupe.firstTimeId(id)) {
    // A retry: our Ack (or Result) was lost. Resend the outcome if we have it already, but
    // only the outcome of this exact command (full id), never another one sharing its seq.
    if (c.last_res.valid && c.last_res.id == id && c.last_res.type == f.type) {
      AckPayload a = c.last_res.a;
      fill_status(a, now);
      send_reply(MsgType::Result, f.seq, a, now, true);
    } else {
      send_ack(f, now);
    }
    return;
  }
  // A new command: the cached Result belongs to an older one. Not for a Release: it has no
  // Result of its own to cache, and the press it ends may still be re-asked for its outcome.
  if (f.type != MsgType::Release) c.last_res.valid = false;
  const uint32_t tag = make_tag(f.type, f.seq);
  note_id(tag, id);
  // A press's Release outcome (one-press Release step, hold watchdog) is reported under it.
  if (f.type == MsgType::Shot || f.type == MsgType::Focus || f.type == MsgType::Shoot)
    note_id(make_tag(MsgType::Release, f.seq), id);
  if (f.type == MsgType::Release && c.exec.buttonDown()) {
    // Let go first, Ack after: a blocking Ack (up to 2.1 s at SF12) in front of the release
    // would hold AF or the shutter (a CH burst) that much longer than the user did. If the
    // Ack is lost the handheld's retry is harmless (deduped, answered with an Ack).
    run_command(f, millis());
    send_ack(f, millis());
    return;
  }
  if (f.type != MsgType::Release && !fresh_echo(f, now)) {
    send_need_fresh(f, id, now);
    return;
  }
  // Frames we send from here on belong to this command's hold (see fresh_echo).
  const uint8_t own_n = sizeof(c.own_tx) / sizeof(c.own_tx[0]);
  const uint32_t own_newest = c.own_tx[(c.own_head + own_n - 1) % own_n].ctr;
  send_ack(f, now);  // first, before any BLE work
  if (!persisted && f.type != MsgType::Release) {
    // Replay guard not in flash: after a reset this frame (or a retry) could run again.
    queue_result({tag, ShotStage::Failed, apperr::STORE});
    return;
  }
  if (id <= c.boot_floor && f.type != MsgType::Release) {
    // Might have run before we restarted: never risk a second photo; say so.
    queue_result({tag, ShotStage::Failed, apperr::STALE});
    return;
  }
  // The Ack blocked for its airtime: a fresh clock, or the one-press AF wait and shutter hold
  // would already be due when the focus write returns.
  run_command(f, millis());
  if (f.type == MsgType::Focus) {  // its Shoot may echo anything we send from the Ack on
    c.hold_live = true;
    c.hold_floor = own_newest;
  }
}

void release_orphan_hold(uint32_t now);
uint32_t silence_ms();

void handle_frame(const Frame& f, const RxPacket& p, uint32_t now, bool persisted) {
  c.watch.onValidRx(now);
  c.prev_rx = c.last_rx;  // when the handheld was heard before this frame (fresh_echo)
  c.last_rx = now;
  c.rssi_up = p.rssi;
  c.snr_up = p.snr;

  switch (f.type) {
    case MsgType::Ping: {
      // While the button is down the handheld only sends keepalive pings (no Ack expected):
      // an Ack now would make us deaf to its Release (half duplex) for the Ack's airtime.
      // S5: nor for a stale kept-back Ping: a late Ack is wasted duty and can collide with the
      // handheld's next command. Everything else in the Ping still counts.
      if (!c.exec.buttonDown() &&
          millis() - p.at_ms <= radio_airtime_ms(PING_FLEN) + PING_ACK_LATE_MS)
        send_ack(f, now);
      PingPayload pp;
      if (ping_unpack(f.payload, f.len, pp)) {
        c.hh_batt = pp.hh_batt;
        if (pp.last_rssi != 0) {
          c.rssi_dn = pp.last_rssi;
          c.snr_dn = pp.last_snr_q4 / 4.f;
        }
        if (pp.flags & 1) c.gps_want_at = now;
        // bit1 clear = "I am not holding": a Focus/Shoot hold we keep for the handheld has
        // lost its owner (it was reset mid-hold). Let go now, not after the silence watchdog
        // (minutes at SF12). One-press shots are not holds and run to their own Release.
        if (!(pp.flags & 2)) release_orphan_hold(now);
      }
      break;
    }
    case MsgType::Sleep:
      // Replay guard not in flash: a recorded copy could pass again after a reset and stop a
      // needed SF12 rescue. Neither act nor ack (the handheld then marks our SF unconfirmed).
      if (!persisted) break;
      send_ack(f, now);
      c.watch.onPeerSleep(now);
      break;
    case MsgType::SetSf: {
      // Same: a replayed SetSf after a reset would split the two ends. Not acked, so the
      // handheld stays on the old SF and retries.
      if (!persisted) break;
      // Switch only if our Ack went on air (at the old SF). A TimedOut Ack (TX started,
      // TX_DONE missed) has almost certainly gone out and the handheld switches on it, so
      // that switches too; only an Ack that never started keeps us on the old SF.
      const RadioTx r = send_ack(f, now);
      if (radio_tx_started(r) && f.len >= 1 && f.payload[0] >= radiocfg::SF_MIN &&
          f.payload[0] <= radiocfg::SF_MAX) {
        set_sf(f.payload[0]);
        c.rescued = false;
      }
      break;
    }
    case MsgType::Focus:
    case MsgType::Shoot:
    case MsgType::Release:
    case MsgType::Shot: handle_command(f, now, persisted); break;
    default: break;
  }
  // Latched after the switch (a SetSf's new SF counts): our own SF12 rescue must not stretch
  // the hold watchdog to the SF12 figure when the handheld was last heard on a faster SF.
  c.hold_silence_ms = silence_ms();
  c.fresh_ms = fresh_window_ms();  // L2: latched the same way, for fresh_echo
}

bool from_handheld(MsgType t) {
  switch (t) {
    case MsgType::Ping:
    case MsgType::Focus:
    case MsgType::Shoot:
    case MsgType::Release:
    case MsgType::Shot:
    case MsgType::SetSf:
    case MsgType::Sleep: return true;
    default: return false;
  }
}

// Same cadence formula as the handheld (linkmgr.h); wait at least two missed ping slots.
uint32_t silence_ms() {
  const uint32_t ping = radio_airtime_ms(PING_FLEN), ack = radio_airtime_ms(ACK_FLEN);
  const uint32_t iv = ping_interval_ms(ping, ack, timing::PING_MIN_INTERVAL_MS);
  return rescue_silence_ms(iv, ping, timing::CAM_RESCUE_SILENCE_MS);
}

void radio_step(uint32_t now) {
  if (!c.radio_ok) return;
  // Shutter down with its Release due within SHUTTER_HOLD_MS: an Ack now would block for its
  // airtime and hold the shutter that long. The packet waits in the SX1262 until then.
  if (c.exec.holdingForRelease()) return;
  RxPacket p;
  for (uint8_t i = 0; i < 4 && radio_poll(p); i++) {
    Frame f;
    if (!frame_decode(p.data, p.len, c.pair.key, f) || f.net != c.pair.net) continue;
    if (!from_handheld(f.type) || !c.guard.accept(f.ctr)) continue;
    // Persist before acting, so no frame accepted before a reset is accepted after it. If
    // that write fails, shutter commands are refused (acked, Result STORE).
    const bool persisted = store_reserve_peer_ctr(f.ctr);
    handle_frame(f, p, now, persisted);
    if (c.exec.holdingForRelease()) break;
  }
  if (c.watch.shouldRescue(now, radio_sf(), silence_ms())) {
    set_sf(radiocfg::SF_MAX);
    c.rescued = true;
  }
}

// The handheld says it holds nothing while we hold a Focus/Shoot for it: release now.
void release_orphan_hold(uint32_t now) {
  if (!c.held_tag || !c.exec.buttonDown() || c.exec.holdingForRelease()) return;
  Serial.println("[cam] handheld holds nothing (reset mid-hold?): releasing");
  const uint32_t held = c.held_tag;
  c.held_tag = 0;
  c.hold_live = false;  // the hold is over: a late Shoot needs a fresh echo again
  c.exec.release(now, make_tag(MsgType::Release, (uint16_t)(held & 0xFFFF)));
  queue_result({held, ShotStage::Failed, apperr::HOLD_LOST});
  cam_toast("RELEASED: HH RESET", now);
}

// Failsafe: the shutter/AF button must not stay down once we have lost the handheld (its
// Release may never arrive). One-press holds release on their own schedule.
void hold_watchdog(uint32_t now) {
  if (!c.exec.buttonDown() || c.exec.holdingForRelease()) return;
  const uint32_t since = (int32_t)(c.last_rx - c.exec.downSince()) > 0 ? c.last_rx
                                                                       : c.exec.downSince();
  // The figure for the SF the handheld was last heard on, not the SF a rescue moved us to.
  const uint32_t lim = c.hold_silence_ms ? c.hold_silence_ms : silence_ms();
  if (now - since < lim) return;
  Serial.println("[cam] no word from the handheld while held: releasing");
  const uint32_t held = c.held_tag;
  c.held_tag = 0;
  c.hold_live = false;  // the hold is over: a late Shoot needs a fresh echo again
  c.exec.release(now, held ? make_tag(MsgType::Release, (uint16_t)(held & 0xFFFF)) : 0);
  // Never silent: the handheld still shows the press held. Its Result says why it ended.
  if (held) queue_result({held, ShotStage::Failed, apperr::HOLD_LOST});
  cam_toast("HOLD RELEASED: NO LINK", now);
}

void exec_step(uint32_t now) {
  c.exec.poll(now);
  hold_watchdog(millis());
  // Results go out after the Release step while the SHUTTER is down (one-press AND two-stage):
  // a blocking SF12 Result then would make us deaf to the Release (half duplex), and the
  // camera would keep the shutter down (a burst in CH) until its retry got through.
  if (c.exec.buttonDown()) {
    // Only focus held, and no one-press about to press the shutter: a failure (AF write
    // failed, BUSY/STALE/STORE) goes out now, while the user still holds the half-press.
    // Successes wait for the Release: an Ack/Result on air now would delay the Shoot.
    if (c.exec.shutterDown() || c.exec.oneShotRunning()) return;
    // X14: the same rule as below also here: never straight behind our own Ack or another
    // Result, and one Failed Result per pass. The rest (and every success) wait in pend.
    CamOutcome o;
    while (c.pend_n < sizeof(c.pend) / sizeof(c.pend[0]) && c.exec.next(o)) queue_result(o);
    if (c.last_reply_at && millis() - c.last_reply_at < RESULT_GAP_MS) return;
    for (uint8_t i = 0; i < c.pend_n; i++) {
      if (c.pend[i].stage != ShotStage::Failed) continue;
      const CamOutcome failed = c.pend[i];
      for (uint8_t k = i + 1; k < c.pend_n; k++) c.pend[k - 1] = c.pend[k];
      c.pend_n--;
      send_result(failed, millis());
      break;
    }
    return;
  }
  // Never a Result straight behind our own Ack: the handheld is still handling the Ack and
  // re-arming its receiver, and misses a frame that starts within a few ms (seen on the
  // bench: the Release Ack + the Shoot's Result back to back lost the Result every time).
  if (c.last_reply_at && millis() - c.last_reply_at < RESULT_GAP_MS) return;
  if (c.hold_results_until) {
    if ((int32_t)(millis() - c.hold_results_until) < 0) {
      CamOutcome o;
      while (c.exec.next(o)) queue_result(o);  // until the backup Release is in
      return;
    }
    c.hold_results_until = 0;
  }
  // One Result per pass: send_reply stamps last_reply_at, so the check above also keeps
  // RESULT_GAP_MS between Results, not only between an Ack and the first Result.
  if (c.pend_n) {
    send_result(c.pend[0], millis());
    for (uint8_t i = 1; i < c.pend_n; i++) c.pend[i - 1] = c.pend[i];
    c.pend_n--;
    return;
  }
  CamOutcome o;
  if (c.exec.next(o)) send_result(o, millis());
}

void gps_step(uint32_t now) {
  const bool want = c.s->geotag || (c.gps_want_at && now - c.gps_want_at < GPS_WANT_HOLD_MS);
  if (want != gps_is_on()) gps_power(want);
  gps_loop();
  if (c.geotag_applied != c.s->geotag) {
    fuji.setGeotag(c.s->geotag);
    c.geotag_applied = c.s->geotag;
  }
  if (now - c.last_geotag < GEOTAG_PERIOD_MS) return;
  c.last_geotag = now;
  c.fix = gps_is_on() ? gps_fix() : GpsFix{};
  // Only with a position fix: a receiver without one can report a default or rolled-over date.
  if (c.fix.valid && c.fix.time_valid && c.fix.year >= 2026)   // GPS time: the clock for both boards (sent in every reply)
    utc_set(utc_from_ymdhms(c.fix.year, c.fix.month, c.fix.day, c.fix.hour, c.fix.minute, c.fix.second));
  if (c.s->geotag) {
    const GpsFix& x = c.fix;
    fuji.updateGps(x.lat, x.lon, x.alt_m, x.year, x.month, x.day, x.hour, x.minute, x.second,
                   x.valid && x.time_valid);
  }
}

[[noreturn]] void power_off(bool prg_stuck) {
  ui_power(false);
  // Never sleep holding the camera's button, or with a Release still unconfirmed: write the
  // Release first (returns at once with no link, blocks at most the BLE command timeout).
  if (c.exec.buttonDown() || c.exec.oneShotRunning() || c.exec.releasePending()) {
    c.held_tag = 0;
    c.exec.release(millis(), 0);
    c.exec.poll(millis());
  }
  fuji.shutdown(200);  // let the camera see the disconnect (documented <= 200 ms block)
  gps_power(false);
  radio_sleep();  // also when the radio never started: SX1262 otherwise idles in standby
  duty_checkpoint(true);
  bootgest_settle();
  hw_deep_sleep(0, prg_stuck);
}

void input_step(uint32_t now) {
  const BtnEvent e = c.prg.update(digitalRead(pins::BTN_PRG) == LOW, now);
  if (e == BtnEvent::None) return;
  if (e == BtnEvent::VeryLong) {
    if (!c.off) c.off_at = now;
    c.off = true;
    wake_screen(now);
    return;
  }
  if (e == BtnEvent::Click && c.screen_on) {
    c.page_i = (uint8_t)((c.page_i + 1) % (sizeof(PAGES) / sizeof(PAGES[0])));
  }
  wake_screen(now);
}

void ui_step(uint32_t now) {
  if (c.screen_on && !c.off && now - c.screen_at >= timing::CAM_SCREEN_SLEEP_MS) {
    c.screen_on = false;
    ui_power(false);
  }
  if (!c.screen_on || now - c.last_ui < (ui_animating(now) ? 30u : UI_PERIOD_MS)) return;
  c.last_ui = now;

  UiModel m;
  m.role = Role::Camera;
  m.page = PAGES[c.page_i];
  m.cam = cam_link();
  m.cam_text = fuji.stateText();
  m.shot.stage = c.shot.stage;   // drives the shutter animation
  m.shot.at_ms = c.shot.at_ms;
  m.cam_name = fuji.cameraName();
  m.ble_rssi = fuji.rssi();
  const bool fresh = c.pair.valid && c.last_rx && now - c.last_rx < LINK_FRESH_MS;
  m.lora_ok = fresh;
  // Figures from a handheld that has gone to sleep would read as a live link.
  m.rssi_up = fresh ? c.rssi_up : NAN;
  m.snr_up = fresh ? c.snr_up : NAN;
  m.rssi_dn = fresh ? c.rssi_dn : NAN;
  m.snr_dn = fresh ? c.snr_dn : NAN;
  m.sf = c.radio_ok ? radio_sf() : c.s->last_sf;
  if (fresh && !isnan(c.snr_up)) {
    float mg = margin_db(c.snr_up, m.sf);
    if (!isnan(c.snr_dn)) mg = min(mg, margin_db(c.snr_dn, m.sf));
    m.margin_db = mg;
    m.bars = bars_from_margin(mg);
  }
  m.tx_out_dbm = c.radio_ok ? radio_tx_out_dbm() : 0;
  m.rescue = c.rescued;
  m.adaptive = c.s->adaptive_sf;
  m.duty_permille = duty_permille();
  m.success_pct = NAN;  // the camera board cannot know how many pings it missed
  m.hh_batt = fresh ? c.hh_batt : 255;
  m.cam_batt = c.batt;
  m.shot = c.shot;
  m.gps_on = gps_is_on();
  m.hh_fix = c.fix.valid;  // this board's own fix (drawGps headline)
  m.cam_fix = false;       // no "camera position" here: it would just repeat our own fix
  m.own_lat = c.fix.lat;
  m.own_lon = c.fix.lon;
  m.sats = c.fix.sats;
  m.hdop = c.fix.hdop;
  if (c.off) {
    m.big1 = "OFF";
    m.big2 = "Release button";
  } else if (!c.pair.valid) {
    m.toast = "BOARDS NOT PAIRED";
  } else if (!c.radio_ok) {
    m.toast = "RADIO FAILED";
  } else if (c.store_fail) {
    m.toast = "STORAGE FAIL";
  } else if (radio_tx_fault_streak() >= 2 || radio_rx_fault()) {  // S4: deaf counts too
    m.toast = "RADIO FAULT";
  } else if (duty_store_failed()) {
    m.toast = "DUTY STORE FAIL";
  } else if (c.exec.releasePending()) {
    m.toast = "RELEASE RETRYING";
  } else if (c.toast && (int32_t)(c.toast_until - now) > 0) {
    m.toast = c.toast;
  } else if (c.antenna_hint && now - hw_boot_ms() < ANTENNA_HINT_MS) {
    m.toast = "ANTENNA ON?";
  } else if (c.s->geotag) {
    // Geotag failures must be visible, not just on Serial.
    const GeoStatus gs = fuji.geotagStatus();
    if (gs == GeoStatus::NoChar) m.toast = "GEO: NO CHAR";
    else if (gs == GeoStatus::LastFail) m.toast = "GEO FAIL";
    else if (gs == GeoStatus::NoFix) m.toast = "GEO: NO FIX";
    else if (gs == GeoStatus::NoReq) m.toast = "GEO: NO REQ";
  }
  if (!c.off && bootgest_hint()) m.toast = "RST AGAIN NOW = SETUP";  // R1, first 3 s
  ui_render(m, now);
}

}  // namespace

void app_camera_setup(Settings& s) {
  c.s = &s;
  hw_led_level(s.led_bright == 1 ? 20 : 255);
  hw_tally_begin(s.tally_pin);
  const uint32_t now = millis();
  pinMode(pins::BTN_PRG, INPUT_PULLUP);
  app_fuji_start();
  fuji.setAutoConnect(true);
  fuji.setGeotag(s.geotag);
  c.geotag_applied = s.geotag;

  store_load_pair(c.pair);
  c.antenna_hint = !hw_woke_from_sleep();
  if (c.pair.valid) {
    c.guard = ReplayGuard(c.pair.peer_ctr);
    c.boot_floor = c.pair.peer_ctr;
    const uint8_t sf = constrain(s.last_sf, radiocfg::SF_MIN, radiocfg::SF_MAX);
    c.radio_ok = radio_begin(sf, s.ant_gain_dbi);
  }
  // Wait on the agreed SF until the handheld speaks; only unexpected silence rescues.
  c.watch.onPeerSleep(now);
  c.batt = hw_batt_pct();
  c.last_batt = now;
  c.screen_at = now;
  ui_power(true);
}

void app_camera_loop() {
  const uint32_t now = millis();
  fuji.loop(now);
  // Release on connect: the first write on a new camera link is a Release (never fires; it
  // clears a press the camera may still hold from a link that dropped mid-hold).
  static bool was_ready = false;
  const bool ready = fuji.ready();
  if (ready && !was_ready && !c.exec.buttonDown() && !c.exec.oneShotRunning()) {
    c.exec.releaseOnConnect(now);  // retried if this first write fails
    c.exec.poll(millis());
  }
  was_ready = ready;
  radio_step(now);
  exec_step(millis());
  {   // tally + board LED: lit while the shutter is down, and a short flash for each fired shot
    const uint32_t t = millis();
    const bool lit = c.exec.shutterDown() || (c.shot.stage == ShotStage::Accepted && t - c.shot.at_ms < 250);
    hw_tally(lit);
    static bool led_was = false;
    const bool led = c.s->led_cues && lit;
    if (led != led_was) { hw_led(led); led_was = led; }
  }
  input_step(now);
  gps_step(now);
  if (now - c.last_batt >= BATT_PERIOD_MS) {
    c.last_batt = now;
    c.batt = hw_batt_pct();
  }
  ui_step(now);
  // NVS duty snapshot only at an idle moment, never between an Ack and a shutter write.
  if (!c.exec.buttonDown() && !c.exec.oneShotRunning() && c.pend_n == 0) { duty_utc_tick(); duty_idle(); }
  if (c.off) {
    if (digitalRead(pins::BTN_PRG) == HIGH) power_off(false);
    // PRG held or stuck LOW (in a bag): do not stay awake until the battery is flat. Sleep
    // with PRG out of the wake sources, a timer checks it again later.
    else if (now - c.off_at >= OFF_STUCK_MS) power_off(true);
  }
}

void app_camera_request_menu(uint8_t what) {
  if (c.exec.buttonDown()) {  // never restart holding the camera's button
    c.held_tag = 0;
    c.exec.release(millis(), 0);
    c.exec.poll(millis());
  }
  fuji.shutdown(200);
  ui_power(false);
  bootgest_restart_into_menu((MenuNext)what);
}

void app_camera_print_stat() {
  const uint32_t now = millis();
  Serial.printf("camera: paired=%d radio=%d sf=%u rescued=%d tx_set=%d tx_out=%d dBm\n",
                c.pair.valid, c.radio_ok, c.radio_ok ? radio_sf() : 0, c.rescued,
                c.radio_ok ? radio_tx_dbm() : 0, c.radio_ok ? radio_tx_out_dbm() : 0);
  Serial.printf("link: last_rx=%ldms ago up rssi=%.0f snr=%.1f  hh heard rssi=%.0f snr=%.1f\n",
                c.last_rx ? (long)(now - c.last_rx) : -1L, c.rssi_up, c.snr_up, c.rssi_dn,
                c.snr_dn);
  Serial.printf("duty: used=%lu ms (%u/1000)\n", (unsigned long)duty_used_ms(), duty_permille());
  Serial.printf("ble: state=%s ready=%d saved=%d rssi=%d name=%s notifies=%u\n",
                fuji.stateText(), fuji.ready(), fuji.hasSaved(), fuji.rssi(), fuji.cameraName(),
                fuji.notifyCount());
  Serial.printf("shot: n=%u stage=%u err=%u\n", c.shot.count, (unsigned)c.shot.stage,
                c.shot.err);
  Serial.printf("gps: on=%d geotag=%d fix=%d sats=%u hdop=%.1f lat=%.7f lon=%.7f\n", gps_is_on(),
                c.s->geotag, c.fix.valid, c.fix.sats, c.fix.hdop, c.fix.lat, c.fix.lon);
  Serial.printf("batt=%u%% hh_batt=%u%%\n", c.batt, c.hh_batt);
}
