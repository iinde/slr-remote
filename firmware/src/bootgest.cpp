#include "bootgest.h"

#include <Arduino.h>
#include <esp_system.h>

#include "duty.h"
#include "hw.h"
#include "radio.h"
#include "rstgest.h"

namespace {
bool g_armed = false;
bool g_by_taps = false;  // X23: this boot's menu was opened by the RST taps alone

// X23: a mark in RTC memory, which keeps its contents through an RST tap (power stays on) but
// not through real power loss. kMarkTap = the tap flag in NVS was set by the boot just before
// this one. A flag found in NVS without it was left by a power-on that lost power inside the
// 3 s window, so it is stale and must not open the menu.
// Whether RTC memory really lasts through RST is learned per board, not assumed: only once a
// power-on boot has found the mark intact (saved in NVS) is a missing mark taken as proof of
// power loss. Until then the gesture works exactly as before.
constexpr uint32_t kMarkIdle = 0x52535430;  // booted, no tap waiting
constexpr uint32_t kMarkTap = 0x52535431;   // first tap seen, window open
constexpr const char* kNvsRtcKept = "rstrtc";  // 1 = the mark has survived a power-on reset here
RTC_NOINIT_ATTR uint32_t s_rtc_mark;
}

MenuNext bootgest_begin(const Settings& s) {
  // Menu asked for by the running app before its restart (card SETUP, serial setup/pair).
  bool ok = false;
  const uint8_t next = store_get_u8(NVS_MENU_NEXT, 0, &ok);
  MenuNext asked = MenuNext::None;
  if (ok && next >= 1 && next <= 3) {
    store_put_u8(NVS_MENU_NEXT, 0);
    asked = (MenuNext)next;
  }

  // R1. Only a power-on (battery, USB plug, RST = CHIP_PU) can be a tap.
  const esp_reset_reason_t r = esp_reset_reason();
  const BootKind kind = hw_woke_from_sleep() || r == ESP_RST_DEEPSLEEP ? BootKind::DeepSleep
                        : r == ESP_RST_POWERON                         ? BootKind::PowerOn
                                                                        : BootKind::Other;
  bool read_ok = false;
  uint8_t stored = store_get_u8(NVS_RST_TAPS, 0, &read_ok);
  // X23: drop a tap flag that the last power-on left behind when it lost power (see above).
  // This boot then counts as a first tap again, so a real double tap still works.
  const uint32_t mark = s_rtc_mark;
  if (kind == BootKind::PowerOn && s.rst_gest) {
    bool kept = store_get_u8(kNvsRtcKept, 0) == 1;
    if (!kept && (mark == kMarkIdle || mark == kMarkTap)) kept = store_put_u8(kNvsRtcKept, 1);
    if (read_ok && stored == 1 && kept && mark != kMarkTap) stored = 0;
  }
  const RstDecision d = rstgest_boot(kind, s.rst_gest, read_ok, stored);
  bool wrote = true;
  if (d.write) wrote = store_put_u8(NVS_RST_TAPS, d.value);
  g_armed = d.armed && wrote;  // a failed write: a plain boot, no hint
  s_rtc_mark = g_armed ? kMarkTap : kMarkIdle;  // X23
  if (asked != MenuNext::None) return asked;
  g_by_taps = d.open_setup;  // X23: may still be a stale flag on a board that has not learned
  return d.open_setup ? MenuNext::Setup : MenuNext::None;
}

bool bootgest_hint() { return g_armed; }

bool bootgest_by_taps() { return g_by_taps; }

void bootgest_tick() {
  if (!rstgest_settle_due(g_armed, millis())) return;
  bootgest_settle();
}

void bootgest_settle() {
  if (!g_armed) return;
  store_put_u8(NVS_RST_TAPS, 0);
  g_armed = false;
  s_rtc_mark = kMarkIdle;  // X23: the window is over
}

void bootgest_restart_into_menu(MenuNext what) {
  bootgest_settle();
  store_put_u8(NVS_MENU_NEXT, (uint8_t)what);
  duty_checkpoint(true);
  radio_sleep();  // ESP.restart() does not reset the SX1262
  Serial.flush();
  delay(100);
  ESP.restart();
  for (;;) {
  }
}
