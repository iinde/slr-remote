// Blocking setup menu (hold PRG at power-on). PRG click = next item, hold = select/toggle.
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "app_internal.h"
#include "bootgest.h"
#include "buttons.h"
#include "config.h"
#include "duty.h"
#include "fuji.h"
#include "hw.h"
#include "pairing.h"
#include "radio.h"
#include "ui.h"

#ifndef XE5_FW_VERSION
#define XE5_FW_VERSION "dev"
#endif

namespace {

enum class Item : uint8_t {
  Role, Mode, PairCam, PairBoards, Shutter, Press, Focus, AfLimit, Modes, WakePress, Sf, Gain,
  Geotag, HhGps, ResetTaps, Aux, Led, Tally, About, ForgetCam, Reset, Exit
};

constexpr uint8_t AF_LIMIT_OPTS[] = {2, 4, 8, 15};
constexpr uint8_t MODES_BASIC[] = {0, 1, 2, 3};           // SHOT, AF LOCK, TIMER, INTERVAL
constexpr uint8_t MODES_ALL[] = {0, 1, 2, 3, 4, 5, 6};    // + BURST, BULB, VIDEO

constexpr uint16_t FOCUS_OPTS[] = {300, 500, 800, 1200};
constexpr int8_t GAIN_OPTS[] = {2, 3, 5};
constexpr uint32_t CAM_PAIR_MS = 60000;
constexpr uint32_t CAM_CONNECT_BUDGET_MS = 30000 + 15000;  // fuji connect timeout + bond/handshake
constexpr uint32_t BOARD_PAIR_MS = 120000;
constexpr uint32_t IDLE_EXIT_MS = 300000;  // left in the bag: save and boot the role
// X23: opened by RST taps and PRG never touched: most likely a stale tap flag, not a user.
constexpr uint32_t TAPS_IDLE_EXIT_MS = 30000;
constexpr uint32_t TOAST_MS = 2500;
constexpr uint8_t MAX_ITEMS = UI_MENU_MAX;

UiModel g_ui;
ClickButton g_btn;
uint32_t g_last_render = 0, g_toast_until = 0, g_last_input = 0;
bool g_radio_up = false;
char g_vals[MAX_ITEMS][16];

BtnEvent poll_btn() {
  BtnEvent e = g_btn.update(digitalRead(pins::BTN_PRG) == LOW, millis());
  if (e != BtnEvent::None) g_last_input = millis();
  return e;
}

void render(bool force = false) {
  const uint32_t now = millis();
  if (g_ui.toast && (int32_t)(now - g_toast_until) >= 0) g_ui.toast = nullptr;
  if (!force && now - g_last_render < 100) return;
  g_last_render = now;
  ui_render(g_ui, now);
}

void toast(const char* t) {
  g_ui.toast = t;
  g_toast_until = millis() + TOAST_MS;
}

void big(const char* a, const char* b) {
  g_ui.menu.active = false;
  g_ui.big1 = a;
  g_ui.big2 = b;
  render(true);
}

void end_big() {
  g_ui.big1 = g_ui.big2 = nullptr;
  g_ui.menu.active = true;
}

// PRG held or stuck LOW for this long while the menu waits for it to go up: not a finger.
constexpr uint32_t kStuckReleaseMs = 15000;

void wait_release() {
  const uint32_t t0 = millis();
  while (digitalRead(pins::BTN_PRG) == LOW) {
    if (millis() - t0 >= kStuckReleaseMs) {
      // Never spin until the battery is flat: sleep with PRG out of the wake sources; a timer
      // checks it again (hw_stuck_wake_check) and arms PRG once it is up.
      ui_power(false);
      radio_sleep();
      duty_checkpoint(true);
      hw_deep_sleep(false, true);
    }
    delay(10);
  }
  delay(30);
}

// Yes/no screen. Hold = yes, click = no; 30 s = no.
bool confirm(const char* question) {
  big(question, "Hold=yes Click=no");
  const uint32_t t0 = millis();
  bool yes = false;
  while (millis() - t0 < 30000) {
    BtnEvent e = poll_btn();
    if (e == BtnEvent::Long) { yes = true; break; }
    if (e == BtnEvent::Click) break;
    delay(10);
  }
  end_big();
  return yes;
}

bool is_handheld(const Settings& s) { return s.role != Role::Camera; }

uint8_t build_items(const Settings& s, Item* out) {
  uint8_t n = 0;
  const bool hh = is_handheld(s), relay = s.mode == Mode::Relay;
  out[n++] = Item::Role;
  if (hh) out[n++] = Item::Mode;
  if (!hh || !relay) out[n++] = Item::PairCam;
  if (!hh || relay) out[n++] = Item::PairBoards;
  if (hh) {
    out[n++] = Item::Shutter;
    out[n++] = Item::Press;
    out[n++] = Item::Focus;
    out[n++] = Item::AfLimit;
    out[n++] = Item::Modes;
    out[n++] = Item::WakePress;
  }
  if (hh && relay) out[n++] = Item::Sf;
  if (!hh || relay) out[n++] = Item::Gain;
  if (!hh) out[n++] = Item::Geotag;
  if (hh) out[n++] = Item::HhGps;
  out[n++] = Item::ResetTaps;
  if (hh) out[n++] = Item::Aux;
  out[n++] = Item::Led;
  out[n++] = Item::Tally;
  out[n++] = Item::About;
  if ((!hh || !relay) && n < MAX_ITEMS - 2) out[n++] = Item::ForgetCam;
  out[n++] = Item::Reset;
  out[n++] = Item::Exit;
  return n;
}

const char* item_name(Item it) {
  switch (it) {
    case Item::Role: return "Role";
    case Item::Mode: return "Mode";
    case Item::PairCam: return "Pair camera";
    case Item::PairBoards: return "Pair boards";
    case Item::Shutter: return "Shutter";
    case Item::Press: return "Press";
    case Item::Focus: return "Focus wait";
    case Item::AfLimit: return "AF limit";
    case Item::Modes: return "Modes shown";
    case Item::WakePress: return "Wake press";
    case Item::ResetTaps: return "Reset taps";
    case Item::Aux: return "Aux button";
    case Item::Led: return "LED";
    case Item::Tally: return "Tally LED";
    case Item::About: return "About";
    case Item::Sf: return "SF";
    case Item::Gain: return "Antenna";
    case Item::Geotag: return "Geotag";
    case Item::HhGps: return "GPS";
    case Item::ForgetCam: return "Forget camera";
    case Item::Reset: return "Factory reset";
    case Item::Exit: return "Exit";
  }
  return "";
}

const char* item_value(const Settings& s, Item it, char* buf, size_t cap) {
  switch (it) {
    case Item::Role:
      return s.role == Role::Camera ? "Camera" : s.role == Role::Handheld ? "Handheld" : "--";
    case Item::Mode: return s.mode == Mode::Relay ? "Relay" : "Direct";
    case Item::PairBoards: {
      PairRecord p;
      store_load_pair(p);
      return p.valid ? "Paired" : "--";
    }
    case Item::PairCam:
      if (!app_fuji_started()) return nullptr;
      return fuji.hasSaved() ? "Saved" : "--";
    case Item::Shutter: return s.two_stage ? "Two-stage" : "PRG";
    case Item::Press: {
      static const char* const k[] = {"Classic", "A", "B", "C"};
      return s.press <= 3 ? k[s.press] : "?";
    }
    case Item::Focus: snprintf(buf, cap, "%u ms", (unsigned)s.focus_ms); return buf;
    case Item::AfLimit: snprintf(buf, cap, "%u s", (unsigned)s.af_limit_s); return buf;
    case Item::Modes:
      if (s.n_modes == sizeof MODES_BASIC && !memcmp(s.modes, MODES_BASIC, sizeof MODES_BASIC))
        return "Basic";
      if (s.n_modes == sizeof MODES_ALL && !memcmp(s.modes, MODES_ALL, sizeof MODES_ALL))
        return "All";
      return "Custom";
    case Item::WakePress: return s.wake_act ? "Act" : "Wake";
    case Item::ResetTaps: return s.rst_gest ? "On" : "Off";
    case Item::Aux: return s.aux_pin ? "GPIO6" : "Off";
    case Item::Led: return !s.led_cues ? "Off" : s.led_bright == 1 ? "Dim" : "Bright";
    case Item::Tally: return s.tally_pin == 0 ? "Off" : s.tally_pin == 3 ? "GPIO3" : s.tally_pin == 4 ? "GPIO4" : s.tally_pin == 34 ? "GPIO3+4" : "GPIO6";
    case Item::About: return "SLR " XE5_FW_VERSION;
    case Item::Sf:
      if (s.adaptive_sf) return "Auto";
      snprintf(buf, cap, "SF%u", (unsigned)s.fixed_sf);
      return buf;
    case Item::Gain: snprintf(buf, cap, "%d dBi", (int)s.ant_gain_dbi); return buf;
    case Item::Geotag: return s.geotag ? "On" : "Off";
    case Item::HhGps: return s.hh_gps ? "On" : "Off";
    default: return nullptr;
  }
}

void fill_menu(const Settings& s, const Item* items, uint8_t n, uint8_t sel) {
  g_ui.menu.active = true;
  g_ui.menu.title = "SETUP " XE5_FW_VERSION;
  g_ui.menu.n = n;
  g_ui.menu.sel = sel;
  for (uint8_t i = 0; i < n; i++) {
    g_ui.menu.items[i] = item_name(items[i]);
    g_ui.menu.values[i] = item_value(s, items[i], g_vals[i], sizeof g_vals[i]);
  }
}

template <typename T, size_t N>
T next_opt(const T (&opts)[N], T cur) {
  for (size_t i = 0; i < N; i++)
    if (opts[i] == cur) return opts[(i + 1) % N];
  return opts[0];
}

void pair_camera() {
  app_fuji_start();
  fuji.startPairing(CAM_PAIR_MS);
  const uint32_t t0 = millis();
  char l1[24], l2[24];
  bool ok = false, cancelled = false;
  uint32_t left_scan = 0;  // when the worker left PairScan (connect/bond/handshake began)
  uint32_t shown_key = 0;  // the passkey last echoed to the console
  for (;;) {
    const uint32_t now = millis();
    fuji.loop(now);
    if (poll_btn() == BtnEvent::Click) {
      fuji.cancelPairing();
      cancelled = true;
      break;
    }
    if (fuji.ready()) { ok = true; break; }
    const FujiState st = fuji.state();
    if (now - t0 > 2000 && (st == FujiState::Idle || st == FujiState::Failed)) break;
    const bool scanning = st == FujiState::PairScan || st == FujiState::Idle;
    if (!scanning && !left_scan) left_scan = now;
    // Scan phase: CAM_PAIR_MS + 2 s. Once found, bonding/handshake gets its own budget
    // (connect timeout + 15 s) instead of being cut off by the scan deadline.
    if ((scanning && now - t0 > CAM_PAIR_MS + 2000) ||
        (left_scan && now - left_scan > CAM_CONNECT_BUDGET_MS)) {
      fuji.cancelPairing();
      break;
    }
    const uint32_t left = (CAM_PAIR_MS - min(CAM_PAIR_MS, now - t0)) / 1000;
    snprintf(l1, sizeof l1, "PAIR CAM %lus", (unsigned long)left);
    if (fuji.passkey() && fuji.passkey() != shown_key) {
      // A host on the serial console can follow pairing (pair cam): the code goes there too.
      shown_key = fuji.passkey();
      Serial.printf("pair: camera code %06lu\n", (unsigned long)shown_key);
    }
    if (fuji.passkey()) snprintf(l2, sizeof l2, "CODE %06lu", (unsigned long)fuji.passkey());
    else snprintf(l2, sizeof l2, "%s", fuji.stateText());
    g_ui.toast = st == FujiState::PairScan ? "Cam: BT > Pairing reg" : "Click = cancel";
    g_ui.menu.active = false;
    g_ui.big1 = l1;
    g_ui.big2 = l2;
    render();
    delay(20);
  }
  end_big();
  Serial.printf("pair: %s\n", ok ? "camera paired" : cancelled ? "cancelled" : "camera pair failed");
  toast(ok ? "Camera paired" : cancelled ? "Cancelled" : "Camera pair failed");
}

void pair_show(const char* a, const char* b) {
  big(a, b);
  // The console shows the same words, so pairing can be followed from a host (the yes is
  // still a hold on each board: the code is there for a person to compare two screens).
  static char last[48];
  char now_s[48];
  snprintf(now_s, sizeof now_s, "%s | %s", a ? a : "", b ? b : "");
  if (strcmp(now_s, last)) {
    Serial.printf("pair: %s\n", now_s);
    snprintf(last, sizeof last, "%s", now_s);
  }
}

int pair_input() {
  BtnEvent e = poll_btn();
  render();  // keep toasts/timeouts ticking
  if (e == BtnEvent::Long) return 1;
  if (e == BtnEvent::Click) return -1;
  return 0;
}

void pair_boards(Settings& s) {
  if (!g_radio_up) g_radio_up = radio_begin(s.last_sf, s.ant_gain_dbi);
  if (!g_radio_up) {
    toast("Radio failed");
    return;
  }
  PairRecord rec;
  const PairResult r = pair_run(is_handheld(s), pair_show, pair_input, rec, BOARD_PAIR_MS);
  end_big();
  switch (r) {
    case PairResult::Ok:
      s.last_sf = radiocfg::SF_MIN;  // both ends start the new link at SF7
      toast("Boards paired");
      break;
    case PairResult::Timeout: toast("Pair timed out"); break;
    case PairResult::Failed:
      toast("Failed, nothing saved");
      break;
    case PairResult::Rejected: toast("Pair cancelled"); break;
    case PairResult::Unsure: toast("UNSURE: PAIR AGAIN"); break;
    case PairResult::StoreFailed: toast("PAIR FAILED: storage"); break;
    default: toast("Pair failed"); break;
  }
}

[[noreturn]] void sleep_unset() {
  big("NO ROLE", "Sleeping: PRG wakes");
  delay(1500);
  // C2: PRG held or stuck LOW here (in a bag) would wake us straight back into this menu for
  // another 5 min, over and over. wait_release() sleeps on the stuck-PRG timer instead.
  wait_release();
  ui_power(false);
  radio_sleep();  // also if never started: the SX1262 otherwise idles in standby
  duty_checkpoint(true);
  hw_deep_sleep(false);
}

[[noreturn]] void save_and_reboot(const Settings& s, const char* msg) {
  if (!store_save(s)) msg = "SAVE FAILED";  // C5: do not claim a save that did not reach flash
  big(msg, "Release PRG");
  wait_release();
  big(msg, "Rebooting");
  // ESP.restart() does not reset the SX1262: left in RX it would draw ~5 mA from now on
  // if the next role never starts the radio.
  radio_sleep();
  duty_checkpoint(true);
  delay(200);
  ESP.restart();
  for (;;) {}
}

// Returns true if the menu should exit.
bool activate(Settings& s, Item it) {
  switch (it) {
    case Item::Role: {
      // C8: a tally cleared by a mistaken Role toggle comes back when Role is toggled back.
      static uint8_t tally_cleared = 0;
      s.role = s.role == Role::Handheld ? Role::Camera : Role::Handheld;
      // C8: a camera-unit tally may sit on a pin the handheld uses for a button.
      if (s.role == Role::Handheld && s.tally_pin &&
          (s.aux_pin == s.tally_pin || (s.two_stage && (s.tally_pin == 4 || s.tally_pin == 6 || s.tally_pin == 34)))) {
        tally_cleared = s.tally_pin;
        s.tally_pin = 0;
        toast("Tally off: button pin");
      } else if (s.role == Role::Camera) {
        if (tally_cleared && !s.tally_pin) s.tally_pin = tally_cleared;
        tally_cleared = 0;
      }
      break;
    }
    case Item::Mode: s.mode = s.mode == Mode::Relay ? Mode::Direct : Mode::Relay; break;
    case Item::PairCam: pair_camera(); break;
    case Item::PairBoards: pair_boards(s); break;
    case Item::Shutter: {
      s.two_stage = !s.two_stage;
      const bool aux_off = s.two_stage && s.aux_pin;
      if (aux_off) {
        s.aux_pin = 0;  // both use GPIO6
        toast("Aux off: GPIO6");
      }
      // C8: the switch takes GPIO4 and GPIO6, so a handheld tally on them has to go too.
      if (s.two_stage && s.role != Role::Camera && (s.tally_pin == 4 || s.tally_pin == 6 || s.tally_pin == 34)) {
        s.tally_pin = 0;
        // C8: one toast for both, or this one would replace "Aux off" before it was ever shown.
        toast(aux_off ? "Aux + tally off" : "Tally off: GPIO4/6");
      }
      break;
    }
    case Item::Press: s.press = (uint8_t)((s.press + 1) % 4); break;
    case Item::Focus: s.focus_ms = next_opt(FOCUS_OPTS, s.focus_ms); break;
    case Item::AfLimit: s.af_limit_s = next_opt(AF_LIMIT_OPTS, s.af_limit_s); break;
    case Item::Modes: {
      const bool basic = s.n_modes == sizeof MODES_BASIC && !memcmp(s.modes, MODES_BASIC, sizeof MODES_BASIC);
      const uint8_t* src = basic ? MODES_ALL : MODES_BASIC;
      s.n_modes = basic ? sizeof MODES_ALL : sizeof MODES_BASIC;
      memset(s.modes, 0, sizeof s.modes);
      memcpy(s.modes, src, s.n_modes);
      break;
    }
    case Item::WakePress: s.wake_act = !s.wake_act; break;
    case Item::ResetTaps: s.rst_gest = !s.rst_gest; break;
    case Item::Aux:
      if (!s.aux_pin && s.two_stage) {
        toast("Two-stage uses GPIO6");
        break;
      }
      s.aux_pin = s.aux_pin ? 0 : pins::BTN_HALF;
      // C8: the aux button takes GPIO6, so a handheld tally there has to go.
      if (s.aux_pin && s.role != Role::Camera && s.tally_pin == s.aux_pin) {
        s.tally_pin = 0;
        toast("Tally off: GPIO6");
      }
      break;
    case Item::Led:   // Off -> Dim -> Bright -> Off
      if (!s.led_cues) { s.led_cues = true; s.led_bright = 1; }
      else if (s.led_bright == 1) s.led_bright = 2;
      else s.led_cues = false;
      break;
    case Item::Tally: {   // Off -> GPIO6 -> GPIO4 -> GPIO3 -> Off, skipping pins this board's buttons use
      static const uint8_t opts[] = {0, 34, 6, 4, 3};
      uint8_t i = 0;
      while (i < 5 && opts[i] != s.tally_pin) i++;
      for (uint8_t k = 1; k <= 5; k++) {
        const uint8_t p = opts[(i + k) % 5];
        const bool hh_ = s.role != Role::Camera;
        if (p && hh_ && ((s.two_stage && (p == 4 || p == 6 || p == 34)) || s.aux_pin == p)) continue;
        s.tally_pin = p;
        break;
      }
      break;
    }
    case Item::About: {
      char l[5][28];
      const uint64_t mac = ESP.getEfuseMac();
      snprintf(l[0], sizeof l[0], "Super Long Range Remote");
      snprintf(l[1], sizeof l[1], "fw %s  %s", XE5_FW_VERSION, s.role == Role::Camera ? "camera unit" : "handheld");
      snprintf(l[2], sizeof l[2], "id %02X%02X%02X  %s", (unsigned)(mac >> 24 & 0xFF), (unsigned)(mac >> 32 & 0xFF),
               (unsigned)(mac >> 40 & 0xFF), __DATE__);
      snprintf(l[3], sizeof l[3], "Heltec V4  LoRa 869.5");
      snprintf(l[4], sizeof l[4], "case: Chem_Kid   click");
      const char* lines[5] = {l[0], l[1], l[2], l[3], l[4]};
      ui_about(lines, 5);
      wait_release();
      while (poll_btn() == BtnEvent::None) delay(10);
      wait_release();
      return false;
    }
    case Item::Sf:
      if (s.adaptive_sf) {
        s.adaptive_sf = false;
        s.fixed_sf = radiocfg::SF_MIN;
      } else if (s.fixed_sf >= radiocfg::SF_MAX) {
        s.adaptive_sf = true;
      } else {
        s.fixed_sf++;
      }
      break;
    case Item::Gain: s.ant_gain_dbi = next_opt(GAIN_OPTS, s.ant_gain_dbi); break;
    case Item::Geotag: s.geotag = !s.geotag; break;
    case Item::HhGps: s.hh_gps = !s.hh_gps; break;
    case Item::ForgetCam:
      if (confirm("Forget camera?")) {
        app_fuji_start();
        toast(fuji.forgetAndWait(1000, false) ? "Camera forgotten" : "Forget failed: retry");
      }
      break;
    case Item::Reset:
      if (confirm("Factory reset?")) {
        app_fuji_start();
        fuji.forgetAndWait(1000, true);  // + delete every NimBLE bond before the restart
        store_factory_reset();
        Settings fresh;
        save_and_reboot(fresh, "RESET DONE");
      }
      break;
    case Item::Exit:
      if (s.role == Role::Unset) {
        toast("Pick a role first");
        break;
      }
      return true;
  }
  return false;
}

}  // namespace

void menu_run(Settings& s, uint8_t start) {
  g_ui = UiModel{};
  g_ui.role = s.role;
  g_ui.mode = s.mode;
  // X23: only the taps opened this menu (PRG not held at boot): leave soon unless PRG is used.
  bool taps_only = bootgest_by_taps() && digitalRead(pins::BTN_PRG) == HIGH;
  wait_release();  // the boot hold must not count as a press
  g_last_input = millis();
  Serial.println(F("setup menu: PRG click = next, hold 1 s = select"));
  // Asked for from the console ('pair cam' / 'pair boards'): start that flow at once.
  if (start == (uint8_t)MenuNext::PairCam && (!is_handheld(s) || s.mode == Mode::Direct)) {
    Serial.println(F("pair: camera pairing started (camera: Bluetooth > Pairing Registration)"));
    pair_camera();
  } else if (start == (uint8_t)MenuNext::PairBoards && (!is_handheld(s) || s.mode == Mode::Relay)) {
    Serial.println(F("pair: board pairing started; start it on the other board too"));
    pair_boards(s);
  }

  Item items[MAX_ITEMS + 3];
  uint8_t sel = 0;
  for (;;) {
    const uint8_t n = build_items(s, items);
    if (sel >= n) sel = 0;
    fill_menu(s, items, n, sel);
    render();

    const BtnEvent e = poll_btn();
    if (e != BtnEvent::None) taps_only = false;  // X23: someone is here
    if (e == BtnEvent::Click) {
      sel = (uint8_t)((sel + 1) % n);
    } else if (e == BtnEvent::Long) {
      const Item it = items[sel];
      if (activate(s, it)) save_and_reboot(s, "SAVED");
      // The list depends on role/mode: keep the cursor on the same item.
      const uint8_t m = build_items(s, items);
      for (uint8_t i = 0; i < m; i++)
        if (items[i] == it) sel = i;
    }
    bootgest_tick();
    const uint8_t req = serial_take_menu_request();  // 'pair cam' / 'pair boards' typed here
    if (req) taps_only = false;  // X23: the console is in use
    if (req == (uint8_t)MenuNext::PairCam) pair_camera();
    else if (req == (uint8_t)MenuNext::PairBoards) pair_boards(s);
    // The USB console works here too: 'role h|c' picks a role and boots it.
    const Role before = s.role;
    serial_poll(s, true);
    if (before == Role::Unset && s.role != Role::Unset) save_and_reboot(s, "SAVED");
    // X23: a stale tap flag opened the menu on a unit nobody is holding: boot the role after
    // 30 s, not 5 min (saved first, like the long idle exit below).
    if (taps_only && s.role != Role::Unset && millis() - g_last_input > TAPS_IDLE_EXIT_MS)
      save_and_reboot(s, "NO INPUT");
    if (millis() - g_last_input > IDLE_EXIT_MS) {
      if (s.role != Role::Unset) save_and_reboot(s, "SAVED");
      // No role (fresh or factory reset) and left alone: sleep instead of draining the
      // battery with the screen on. PRG wakes the board straight back into this menu.
      sleep_unset();
    }
    delay(10);
  }
}
