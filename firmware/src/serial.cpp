// USB serial console (line based, 115200) plus the small helpers shared by the apps.
// Never prints pairing keys or BLE bond material.
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_internal.h"
#include "bootgest.h"
#include "duty.h"
#include "fuji.h"
#include "radio.h"
#include "hw.h"
#include "linebuf.h"
#include "modes.h"
#include "store.h"

#ifndef XE5_FW_VERSION
#define XE5_FW_VERSION "dev"
#endif

namespace {
char g_line[72];
uint8_t g_len = 0;
bool g_overflow = false;
bool g_fuji_started = false;
bool g_in_menu = false;  // setup menu: no role app is running (stat must not touch it)
uint8_t g_menu_req = 0;  // 'pair' typed while in the menu: the menu starts it (MenuNext)
char g_name[12];

void help() {
  Serial.println(F(
      "commands: help stat dump clear notify forget-cam forget-pair\n"
      "  role <h|c>  mode <direct|relay>  sf <auto|7..12>  focus <ms>\n"
      "  gain <dBi> [force]  geotag <on|off>  reset  reboot\n"
      "one button (handheld):\n"
      "  cfg                      every setting, one pasteable line each\n"
      "  press classic|a|b|c      press style (reboot to apply)\n"
      "  modes shot,aflock,timer,interval[,burst,bulb,video,holdaf]\n"
      "  afmax <2..15>  aflock <5..120>  timer <1..60>  iv <every 2..3600> <count 0..9999>\n"
      "  burst <200..10000 ms>  bulb <1..1800 s>  holdcap <1..1800 s>\n"
      "  wake act|wake  rstgest on|off  aux off|6  led on|off\n"
      "  setup                    restart into the setup menu\n"
      "  pair boards | pair cam   start pairing (still confirmed by a hold on the board)\n"
      "machine: get <key>  set <key> <value>  json  jstat\n"
      "  (settings take effect after a reboot unless they say otherwise)"));
}

// ---------- one-button settings ----------

void print_modes(const Settings& s, char sep) {
  for (uint8_t i = 0; i < s.n_modes && i < 8; i++) {
    if (i) Serial.print(sep);
    Serial.print(ob_mode_key((ObMode)s.modes[i]));
  }
}

// One "key value" line, the same text 'cfg' prints and the console accepts back.
bool print_kv(const Settings& s, const char* k) {
  if (!strcmp(k, "press")) Serial.printf("press %s\n", ob_style_key((PressStyle)s.press));
  else if (!strcmp(k, "modes")) { Serial.print("modes "); print_modes(s, ','); Serial.println(); }
  else if (!strcmp(k, "afmax")) Serial.printf("afmax %u\n", s.af_limit_s);
  else if (!strcmp(k, "aflock")) Serial.printf("aflock %u\n", s.aflock_s);
  else if (!strcmp(k, "timer")) Serial.printf("timer %u\n", s.timer_s);
  else if (!strcmp(k, "iv")) Serial.printf("iv %u %u\n", s.iv_every_s, s.iv_count);
  else if (!strcmp(k, "burst")) Serial.printf("burst %u\n", s.burst_ms);
  else if (!strcmp(k, "bulb")) Serial.printf("bulb %u\n", s.bulb_s);
  else if (!strcmp(k, "holdcap")) Serial.printf("holdcap %u\n", s.hold_cap_s);
  else if (!strcmp(k, "wake")) Serial.printf("wake %s\n", s.wake_act ? "act" : "wake");
  else if (!strcmp(k, "rstgest")) Serial.printf("rstgest %s\n", s.rst_gest ? "on" : "off");
  else if (!strcmp(k, "aux")) Serial.printf("aux %s\n", s.aux_pin ? "6" : "off");
  else if (!strcmp(k, "led")) Serial.printf("led %s\n", !s.led_cues ? "off" : s.led_bright == 1 ? "dim" : "on");
  else if (!strcmp(k, "tally")) { if (s.tally_pin) Serial.printf("tally %u\n", s.tally_pin); else Serial.println("tally off"); }
  else if (!strcmp(k, "rlog")) Serial.printf("rlog %s\n", s.rlog_always ? "on" : "off");
  else if (!strcmp(k, "focus")) Serial.printf("focus %u\n", s.focus_ms);
  else if (!strcmp(k, "sf")) {
    if (s.adaptive_sf) Serial.println("sf auto");
    else Serial.printf("sf %u\n", s.fixed_sf);
  } else if (!strcmp(k, "mode")) Serial.printf("mode %s\n", s.mode == Mode::Relay ? "relay" : "direct");
  else if (!strcmp(k, "role")) Serial.printf("role %s\n", s.role == Role::Camera ? "c" : "h");
  else if (!strcmp(k, "geotag")) Serial.printf("geotag %s\n", s.geotag ? "on" : "off");
  else if (!strcmp(k, "gain")) Serial.printf("gain %d\n", (int)s.ant_gain_dbi);
  else return false;
  return true;
}

// The profile: shooting and one-button settings only. Role, link mode, SF, antenna and
// geotag belong to one board and are never part of a profile.
const char* const kProfileKeys[] = {"press", "modes", "afmax", "aflock", "timer", "iv", "burst",
                                    "bulb", "holdcap", "wake", "rstgest", "aux", "led", "focus"};

void print_cfg(const Settings& s) {
  Serial.printf("# xe5 profile, fw %s, this board: role %s, mode %s\n", XE5_FW_VERSION,
                s.role == Role::Camera ? "c" : "h", s.mode == Mode::Relay ? "relay" : "direct");
  for (const char* k : kProfileKeys) print_kv(s, k);
}

void print_json(const Settings& s) {
  static LineBuf b;  // one write: a [fuji] log line from core 0 can never split it
  b.printf("{\"fw\":\"%s\",\"role\":\"%s\",\"mode\":\"%s\",\"press\":\"%s\",\"modes\":[",
           XE5_FW_VERSION, s.role == Role::Camera ? "camera" : s.role == Role::Handheld ? "handheld" : "unset",
           s.mode == Mode::Relay ? "relay" : "direct", ob_style_key((PressStyle)s.press));
  for (uint8_t i = 0; i < s.n_modes && i < 8; i++)
    b.printf("%s\"%s\"", i ? "," : "", ob_mode_key((ObMode)s.modes[i]));
  b.printf("],\"afmax\":%u,\"aflock\":%u,\"timer\":%u,\"iv_every\":%u,\"iv_count\":%u,"
           "\"burst\":%u,\"bulb\":%u,\"holdcap\":%u,\"wake\":\"%s\",\"rstgest\":%s,"
           "\"aux\":%u,\"led\":%s,\"focus\":%u,\"sf\":\"%s\",\"fixed_sf\":%u,"
           "\"geotag\":%s,\"gps\":%s,\"two_stage\":%s,\"gain\":%d}",
           s.af_limit_s, s.aflock_s, s.timer_s, s.iv_every_s, s.iv_count, s.burst_ms, s.bulb_s,
           s.hold_cap_s, s.wake_act ? "act" : "wake", s.rst_gest ? "true" : "false", s.aux_pin,
           s.led_cues ? "true" : "false", s.focus_ms, s.adaptive_sf ? "auto" : "fixed",
           s.fixed_sf, s.geotag ? "true" : "false", s.hh_gps ? "true" : "false",
           s.two_stage ? "true" : "false", (int)s.ant_gain_dbi);
  b.send();
}

void refuse(const char* why) {
  Serial.print(F("error: "));
  Serial.println(why);
}

void print_notify() {
  if (!g_fuji_started) {
    Serial.println(F("BLE not running in this role/mode"));
    return;
  }
  const uint8_t n = fuji.notifyCount();
  Serial.printf("notifications: %u (newest first), now=%lu\n", (unsigned)n,
                (unsigned long)millis());
  for (uint8_t i = 0; i < n; i++) {
    FujiNotify fn;
    if (!fuji.notifyAt(i, fn)) break;
    Serial.printf("#%u t=%lu uuid=%08lx len=%u data=", (unsigned)i, (unsigned long)fn.at_ms,
                  (unsigned long)fn.uuid32, (unsigned)fn.len);
    const uint8_t shown = fn.len < sizeof fn.data ? fn.len : sizeof fn.data;
    for (uint8_t b = 0; b < shown; b++) Serial.printf("%02x", fn.data[b]);
    Serial.println(fn.len > shown ? "..." : "");
  }
}

void print_stat(const Settings& s) {
  Serial.printf("fw=%s role=%s mode=%s up=%lus batt=%u%% vbat=%.2f\n", XE5_FW_VERSION,
                s.role == Role::Camera ? "camera" : "handheld",
                s.mode == Mode::Relay ? "relay" : "direct", (unsigned long)(millis() / 1000),
                (unsigned)hw_batt_pct(), hw_vbat());
  Serial.printf("settings: two_stage=%d focus=%u sf=%s%u last_sf=%u gain=%d geotag=%d hh_gps=%d\n",
                s.two_stage, (unsigned)s.focus_ms, s.adaptive_sf ? "auto/" : "fixed/",
                (unsigned)s.fixed_sf, (unsigned)s.last_sf, (int)s.ant_gain_dbi, s.geotag,
                s.hh_gps);
  if (g_in_menu) Serial.println(F("in setup menu (no role running)"));
  else if (s.role == Role::Camera) app_camera_print_stat();
  else app_handheld_print_stat();
  // No full-file scan from loop(): the cached count if known, else the size (one stat).
  if (rlog_rows_known()) Serial.printf("range log rows: %lu\n", (unsigned long)rlog_rows());
  else Serial.printf("range log: %lu bytes (rows not counted; 'dump' to read)\n",
                     (unsigned long)rlog_size());
}

// Whole-argument integer: "12" ok, "", "x", "9x", "1.5" rejected (trailing spaces allowed).
bool parse_long(const char* a, long& out) {
  if (!a || !*a) return false;
  char* end = nullptr;
  const long v = strtol(a, &end, 10);
  if (end == a) return false;
  while (*end == ' ') end++;
  if (*end) return false;
  out = v;
  return true;
}

bool parse_onoff(const char* a, bool& out) {
  if (!strcmp(a, "on")) out = true;
  else if (!strcmp(a, "off")) out = false;
  else return false;
  return true;
}

void saved(const Settings& s, bool reboot) {
  const bool stored = store_save(s);
  if (!g_in_menu) app_handheld_settings_changed();  // no-op unless the handheld app runs
  // C5: never say ok for a setting that will be gone after the next reboot.
  if (!stored) { Serial.println(F("error: save failed (setting is live until reboot only)")); return; }
  Serial.println(reboot ? F("ok (reboot to apply)") : F("ok"));
}

// Whole-argument integer in [lo, hi], or refuse with a reason. name: for the message.
bool arg_range(const char* a, long lo, long hi, const char* name, long& out) {
  long v = 0;
  if (!parse_long(a, v)) {
    Serial.printf("error: %s needs a whole number\n", name);
    return false;
  }
  if (v < lo || v > hi) {
    Serial.printf("error: %s %ld is out of range %ld..%ld\n", name, v, lo, hi);
    return false;
  }
  out = v;
  return true;
}

// Splits "a b" into a and b (b may be empty).
char* split_arg(char* a) {
  char* b = strchr(a, ' ');
  if (!b) return a + strlen(a);
  *b++ = 0;
  while (*b == ' ') b++;
  return b;
}

// One-button commands. Returns false if line is not one of them.
bool handle_ob(Settings& s, const char* line, char* arg) {
  long v = 0;
  if (!strcmp(line, "cfg")) {
    print_cfg(s);
  } else if (!strcmp(line, "press")) {
    PressStyle p;
    if (!ob_style_from_key(arg, p)) { refuse("press classic|a|b|c"); return true; }
    s.press = (uint8_t)p;
    saved(s, true);
  } else if (!strcmp(line, "modes")) {
    uint8_t out[8];
    uint8_t n = 0;
    out[n++] = (uint8_t)ObMode::Shot;  // SHOT is always on the card, first
    char* p = arg;
    while (*p) {
      char* comma = strchr(p, ',');
      if (comma) *comma = 0;
      ObMode m;
      if (!ob_mode_from_key(p, m)) {
        Serial.printf("error: unknown mode '%s' (shot aflock timer interval burst bulb video holdaf)\n", p);
        return true;
      }
      bool seen = false;
      for (uint8_t i = 0; i < n; i++) seen = seen || out[i] == (uint8_t)m;
      if (!seen) {
        if (n >= 8) { refuse("at most 8 modes"); return true; }
        out[n++] = (uint8_t)m;
      }
      if (!comma) break;
      p = comma + 1;
    }
    s.n_modes = n;
    memset(s.modes, 0, sizeof s.modes);
    memcpy(s.modes, out, n);
    saved(s, false);
  } else if (!strcmp(line, "afmax")) {
    if (!arg_range(arg, 2, 15, "afmax", v)) return true;
    s.af_limit_s = (uint8_t)v;
    saved(s, false);
  } else if (!strcmp(line, "aflock")) {
    if (!arg_range(arg, 5, 120, "aflock", v)) return true;
    s.aflock_s = (uint8_t)v;
    saved(s, false);
  } else if (!strcmp(line, "timer")) {
    if (!arg_range(arg, 1, 60, "timer", v)) return true;
    s.timer_s = (uint8_t)v;
    saved(s, false);
  } else if (!strcmp(line, "iv")) {
    char* b = split_arg(arg);
    long every = 0, count = 0;
    if (!parse_long(arg, every)) { refuse("iv <every s> <count, 0 = until stopped>"); return true; }
    if (every < 2) { refuse("every too short: min 2 s with focus each shot"); return true; }
    if (every > 3600) { refuse("every too long: max 3600 s"); return true; }
    if ((every * 1000) < (long)s.focus_ms + 1000) {
      Serial.printf("error: every %ld s leaves no room after a %u ms focus wait\n", every, s.focus_ms);
      return true;
    }
    if (!arg_range(b, 0, 9999, "count", count)) return true;
    if (count && (count - 1) * every > 4L * 3600) { refuse("that run is longer than the 4 h limit"); return true; }
    s.iv_every_s = (uint16_t)every;
    s.iv_count = (uint16_t)count;
    saved(s, false);
  } else if (!strcmp(line, "burst")) {
    if (!arg_range(arg, 200, 10000, "burst ms", v)) return true;
    s.burst_ms = (uint16_t)v;
    saved(s, false);
    if (v > (long)s.hold_cap_s * 1000) Serial.printf("note: capped at holdcap %u s\n", s.hold_cap_s);
  } else if (!strcmp(line, "bulb")) {
    if (!arg_range(arg, 1, 1800, "bulb s", v)) return true;
    s.bulb_s = (uint16_t)v;
    saved(s, false);
    if (v > s.hold_cap_s) Serial.printf("note: capped at holdcap %u s\n", s.hold_cap_s);
  } else if (!strcmp(line, "holdcap")) {
    if (!arg_range(arg, 1, 1800, "holdcap s", v)) return true;
    s.hold_cap_s = (uint16_t)v;
    saved(s, false);
  } else if (!strcmp(line, "wake")) {
    if (!strcmp(arg, "act")) s.wake_act = true;
    else if (!strcmp(arg, "wake")) s.wake_act = false;
    else { refuse("wake act|wake"); return true; }
    saved(s, false);
  } else if (!strcmp(line, "rstgest")) {
    bool on;
    if (!parse_onoff(arg, on)) { refuse("rstgest on|off"); return true; }
    s.rst_gest = on;
    saved(s, false);
  } else if (!strcmp(line, "aux")) {
    if (!strcmp(arg, "off")) s.aux_pin = 0;
    else if (!strcmp(arg, "6")) {
      if (s.two_stage) { refuse("the two-stage switch uses GPIO6: turn it off first"); return true; }
      // C8: the reverse of the tally check below, or the tally LED dies and GPIO6 cannot wake.
      if (s.role != Role::Camera && s.tally_pin == pins::BTN_HALF) { refuse("GPIO6 is the tally: turn it off first"); return true; }
      s.aux_pin = pins::BTN_HALF;
    } else { refuse("aux off|6"); return true; }
    saved(s, true);
  } else if (!strcmp(line, "led")) {
    if (!strcmp(arg, "dim")) { s.led_cues = true; s.led_bright = 1; }
    else if (!strcmp(arg, "on") || !strcmp(arg, "bright")) { s.led_cues = true; s.led_bright = 2; }
    else if (!strcmp(arg, "off")) s.led_cues = false;
    else { refuse("led off|dim|on"); return true; }
    saved(s, false);
  } else if (!strcmp(line, "tally")) {
    long v = 0;
    if (!strcmp(arg, "off")) s.tally_pin = 0;
    else if (parse_long(arg, v) && (v == 3 || v == 4 || v == 6 || v == 34)) {
      if (s.role != Role::Camera && (s.two_stage && (v == 4 || v == 6 || v == 34))) { refuse("GPIO 4/6 are the two-stage switch here"); return true; }
      if (s.role != Role::Camera && s.aux_pin == v) { refuse("that pin is the aux button"); return true; }
      s.tally_pin = (uint8_t)v;
    } else { refuse("tally off|3|4|6|34 (34 = LED across GPIO3 and GPIO4)"); return true; }
    saved(s, true);
  } else if (!strcmp(line, "get")) {
    if (!print_kv(s, arg)) refuse("unknown key (press modes afmax aflock timer iv burst bulb holdcap wake rstgest aux led tally rlog focus sf mode role geotag gain)");
  } else if (!strcmp(line, "json")) {
    print_json(s);
  } else if (!strcmp(line, "jstat")) {
    if (g_in_menu) Serial.println(F("{\"in_menu\":true}"));
    else if (s.role == Role::Camera) Serial.println(F("{\"role\":\"camera\"}"));
    else app_handheld_print_json_status();
  } else if (!strcmp(line, "setup")) {
    if (g_in_menu) { Serial.println(F("ok (already in the setup menu)")); return true; }
    Serial.println(F("ok: restarting into the setup menu"));
    app_request_menu((uint8_t)MenuNext::Setup);
  } else if (!strcmp(line, "pair")) {
    MenuNext what;
    if (!strcmp(arg, "cam")) what = MenuNext::PairCam;
    else if (!strcmp(arg, "boards")) what = MenuNext::PairBoards;
    else { refuse("pair boards|cam"); return true; }
    const bool hh = s.role != Role::Camera;
    if (what == MenuNext::PairCam && hh && s.mode == Mode::Relay) {
      refuse("a relay handheld does not pair a camera: pair the camera board");
      return true;
    }
    if (what == MenuNext::PairBoards && hh && s.mode == Mode::Direct) {
      refuse("a direct handheld has no board to pair: 'mode relay' first");
      return true;
    }
    if (what == MenuNext::PairCam)
      Serial.println(F("ok: camera: Bluetooth on, open Pairing Registration; approve on the camera"));
    else
      Serial.println(F("ok: start 'pair boards' on the other board too; check both show the same "
                       "6-digit code, then hold PRG on each board to say yes"));
    if (g_in_menu) g_menu_req = (uint8_t)what;
    else app_request_menu((uint8_t)what);
  } else {
    return false;
  }
  return true;
}

void handle(Settings& s, char* line) {
  while (*line == ' ') line++;
  char* arg = strchr(line, ' ');
  if (arg) {
    *arg++ = 0;
    while (*arg == ' ') arg++;
  } else {
    arg = line + strlen(line);
  }
  if (!*line) return;
  if (*line == '#') return;  // comment lines in a pasted profile

  // 'set k v' is the same as 'k v' (machine-friendly spelling).
  if (!strcmp(line, "set")) {
    char* v = split_arg(arg);
    if (!*arg) { refuse("set <key> <value>"); return; }
    if (!strcmp(arg, "iv") || !strcmp(arg, "modes") || !strcmp(arg, "press") || !strcmp(arg, "afmax") ||
        !strcmp(arg, "aflock") || !strcmp(arg, "timer") || !strcmp(arg, "burst") || !strcmp(arg, "bulb") ||
        !strcmp(arg, "holdcap") || !strcmp(arg, "wake") || !strcmp(arg, "rstgest") || !strcmp(arg, "aux") ||
        !strcmp(arg, "led") || !strcmp(arg, "focus") || !strcmp(arg, "sf") || !strcmp(arg, "geotag")) {
      if (handle_ob(s, arg, v)) return;
      line = arg;
      arg = v;
    } else {
      refuse("set: unknown or console-only key (role, mode and gain have their own commands)");
      return;
    }
  }
  if (handle_ob(s, line, arg)) return;

  if (!strcmp(line, "help")) help();
  else if (!strcmp(line, "stat")) print_stat(s);
  else if (!strcmp(line, "dump")) rlog_dump_start();  // streamed from serial_poll, in pieces
  else if (!strcmp(line, "clear")) { rlog_clear(); Serial.println(F("ok")); }
  else if (!strcmp(line, "notify")) print_notify();
  else if (!strcmp(line, "forget-cam")) {
    app_fuji_start();
    if (fuji.forgetAndWait(1000, false)) Serial.println(F("ok: camera record and bond deleted"));
    else Serial.println(F("FAILED: BLE worker busy, bond may remain; try again"));
  } else if (!strcmp(line, "forget-pair")) {
    store_clear_pair();
    Serial.println(F("ok: board pairing deleted (reboot to apply)"));
  } else if (!strcmp(line, "role")) {
    if (!strcmp(arg, "h")) s.role = Role::Handheld;
    else if (!strcmp(arg, "c")) s.role = Role::Camera;
    else { Serial.println(F("usage: role <h|c>")); return; }
    // C8: a camera-unit tally may sit on a pin the handheld uses for a button.
    if (s.role == Role::Handheld && s.tally_pin &&
        (s.aux_pin == s.tally_pin || (s.two_stage && (s.tally_pin == 4 || s.tally_pin == 6 || s.tally_pin == 34)))) {
      s.tally_pin = 0;
      Serial.println(F("note: tally off (its pin is a button on the handheld)"));
    }
    saved(s, true);
  } else if (!strcmp(line, "mode")) {
    if (!strcmp(arg, "direct")) s.mode = Mode::Direct;
    else if (!strcmp(arg, "relay")) s.mode = Mode::Relay;
    else { Serial.println(F("usage: mode <direct|relay>")); return; }
    saved(s, true);
  } else if (!strcmp(line, "sf")) {
    long v = 0;
    if (!strcmp(arg, "auto")) s.adaptive_sf = true;
    else if (parse_long(arg, v) && v >= radiocfg::SF_MIN && v <= radiocfg::SF_MAX) {
      s.adaptive_sf = false;
      s.fixed_sf = (uint8_t)v;
      // Both boards start on last_sf after a reset. Without this a board kept the SF it last
      // used (seen on the bench: the camera board came back on SF12 long after 'sf 7').
      s.last_sf = (uint8_t)v;
      saved(s, true);  // 'sf N' on both boards, then 'reboot' both
      return;
    } else { Serial.println(F("usage: sf <auto|7..12>")); return; }
    saved(s, false);
  } else if (!strcmp(line, "focus")) {
    long v = 0;
    if (!parse_long(arg, v) || v < 0 || v > 5000) {
      Serial.println(F("usage: focus <0..5000 ms>"));
      return;
    }
    s.focus_ms = (uint16_t)v;
    saved(s, false);
  } else if (!strcmp(line, "gain")) {
    // A lower stated gain raises the TX power the legal cap allows: it needs "force".
    char* opt = strchr(arg, ' ');
    if (opt) {
      *opt++ = 0;
      while (*opt == ' ') opt++;
    }
    const bool force = opt && !strcmp(opt, "force");
    long v = 0;
    if (!parse_long(arg, v) || v < 0 || v > 15 || (opt && *opt && !force)) {
      Serial.println(F("usage: gain <0..15 dBi> [force]"));
      return;
    }
    if (v < s.ant_gain_dbi && !force) {
      Serial.printf("gain %ld is below the stored %d dBi and would raise TX power: "
                    "repeat as 'gain %ld force' if the antenna really is %ld dBi\n",
                    v, (int)s.ant_gain_dbi, v, v);
      return;
    }
    s.ant_gain_dbi = (int8_t)v;
    saved(s, true);
  } else if (!strcmp(line, "geotag")) {
    bool on;
    if (!parse_onoff(arg, on)) { Serial.println(F("usage: geotag <on|off>")); return; }
    s.geotag = on;
    saved(s, false);
  } else if (!strcmp(line, "reset")) {
    Serial.println(F("factory reset, rebooting"));
    app_fuji_start();
    fuji.forgetAndWait(1000, true);  // + delete every NimBLE bond before the restart
    store_factory_reset();
    duty_checkpoint(true);  // airtime already used still counts after a reset
    radio_sleep();          // ESP.restart() does not reset the SX1262
    Serial.flush();
    ESP.restart();
  } else if (!strcmp(line, "reboot")) {
    duty_checkpoint(true);
    radio_sleep();
    Serial.flush();
    ESP.restart();
  } else {
    Serial.println(F("unknown command; try help"));
  }
}
}  // namespace

const char* app_dev_name() {
  if (!g_name[0]) {
    const uint64_t mac = ESP.getEfuseMac();  // byte 0 = first MAC byte
    const uint32_t tail = (uint32_t)((((mac >> 24) & 0xFF) << 16) | (((mac >> 32) & 0xFF) << 8) |
                                     ((mac >> 40) & 0xFF));
    snprintf(g_name, sizeof g_name, "SLR-%05lX", (unsigned long)(tail & 0xFFFFF));
  }
  return g_name;
}

void app_fuji_start() {
  if (g_fuji_started) return;
  fuji.begin(app_dev_name());
  g_fuji_started = true;
}

bool app_fuji_started() { return g_fuji_started; }

void serial_poll(Settings& s, bool in_menu) {
  g_in_menu = in_menu;
  if (rlog_dump_active()) rlog_dump_step(1024);  // a 'dump' in progress: one piece per pass
  for (uint8_t budget = 0; budget < 64 && Serial.available(); budget++) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      g_line[g_len] = 0;
      // A console session keeps the handheld awake (it sleeps 60 s after the last line).
      app_handheld_note_activity();
      if (!g_overflow) handle(s, g_line);
      else Serial.println(F("line too long"));
      g_len = 0;
      g_overflow = false;
    } else if (g_len < sizeof g_line - 1) {
      g_line[g_len++] = (char)c;
    } else {
      g_overflow = true;
    }
  }
}

void app_request_menu(uint8_t what) {
  if (!app_handheld_request_menu(what)) app_camera_request_menu(what);
}

uint8_t serial_take_menu_request() {
  const uint8_t r = g_menu_req;
  g_menu_req = 0;
  return r;
}
