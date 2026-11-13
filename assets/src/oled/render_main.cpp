// Renders the SLR Remote OLED screens on a PC by running the firmware's own drawing code
// (firmware/src/ui.cpp) against the real U8g2 library with a dummy display bus.
// Each screen is written as a 128x64 PGM; make_screens.py turns those into the PNGs.
// The numbers in each model (battery, signal, counts) are example values.
#include <stdio.h>
#include <string.h>
#include <string>
#include <math.h>
#include "U8g2lib.h"
#include "ui.h"
#include "apperr.h"

static uint32_t g_now = 0;
static std::string g_out, g_name;
uint32_t millis() { return g_now; }
void delay(uint32_t ms) { g_now += ms; }

void oled_on_send(u8g2_t* u) {
  if (g_name.empty()) return;
  const uint8_t* b = u8g2_GetBufferPtr(u);
  const std::string p = g_out + "/" + g_name + ".pgm";
  FILE* f = fopen(p.c_str(), "wb");
  if (!f) { perror(p.c_str()); return; }
  fprintf(f, "P5\n128 64\n255\n");
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 128; x++) fputc((b[(y / 8) * 128 + x] >> (y % 8)) & 1 ? 255 : 0, f);
  fclose(f);
}

static void cp(char* d, size_t cap, const char* s) { snprintf(d, cap, "%s", s); }
#define CP(d, s) cp(d, sizeof d, s)

static void shoot(const char* name, const UiModel& m, uint32_t now) {
  g_now = now;
  g_name = name;
  ui_render(m, now);
  g_name.clear();
}

// A paired relay handheld on its home screen, camera ready.
static UiModel handheld() {
  UiModel m;
  m.role = Role::Handheld;
  m.mode = Mode::Relay;
  m.page = Page::Main;
  m.cam = CamLink::Ready;
  m.cam_text = "READY";
  m.lora_ok = true;
  m.bars = 4;
  m.sf = 7;
  m.margin_db = 14.5f;
  m.rssi_up = -84; m.snr_up = 9.5f; m.rssi_dn = -86; m.snr_dn = 9.0f;
  m.tx_out_dbm = 22;
  m.success_pct = 100;
  m.rtt_ms = 205;
  m.ble_rssi = -58;
  m.duty_permille = 12;
  m.hh_batt = 86;
  m.cam_batt = 71;
  m.shot.count = 12;
  m.ob.active = true;
  CP(m.ob.scr.title, "SHOT");
  CP(m.ob.scr.bar, "TAP SHOT  HOLD AF");
  CP(m.ob.info, "#12 0.2s  AIR 98%");
  return m;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: render_oled <out dir>\n"); return 2; }
  g_out = argv[1];
  ui_begin();
  const uint32_t T = 20000;   // 20000/250 is even: progress dots drawn steady

  // Splash: keep the last frame the boot animation sends.
  g_now = 0; g_name = "splash";
  ui_splash(false, "v0.3.0");
  g_name.clear();

  { UiModel m = handheld(); shoot("ready", m, T); }

  { UiModel m = handheld();            // power-on, first ping out, no reply due yet
    m.lora_ok = false; m.link_checking = true; m.bars = 0; m.cam = CamLink::None; m.cam_text = "NO LINK";
    m.cam_batt = 255; m.shot.count = 0;
    CP(m.ob.info, "#0  AIR 100%");
    shoot("checking", m, T + 620); }

  { UiModel m = handheld();            // press transmitted, waiting for the camera unit
    m.shot.stage = ShotStage::Sent; m.shot.at_ms = T - 100; m.shot.count = 13;
    CP(m.ob.info, "#13 0.2s  AIR 98%");
    shoot("sending", m, T + 250); }

  { UiModel m = handheld();            // camera unit has it
    m.shot.stage = ShotStage::Delivered; m.shot.at_ms = T - 100; m.shot.count = 13; m.shot.latency_ms = 205;
    CP(m.ob.info, "#13 0.2s  AIR 98%");
    shoot("at-cam", m, T + 250); }

  { UiModel m = handheld();            // camera accepted the press
    m.shot.stage = ShotStage::Accepted; m.shot.at_ms = T - 1000; m.shot.count = 13; m.shot.latency_ms = 205;
    CP(m.ob.info, "#13 0.2s  AIR 98%");
    shoot("pressed", m, T); }

  { UiModel m = handheld();            // long-range setting
    m.sf = 11; m.bars = 2; m.margin_db = 4.5f;
    CP(m.ob.info, "#12 1.9s  AIR 91%");
    shoot("slow-link", m, T); }

  { UiModel m = handheld();            // button held: half-press
    ObScreen& o = m.ob.scr;
    o.kind = ObKind::Text;
    CP(o.big, "AF HELD"); CP(o.right, "2.1s"); o.progress = 525;
    CP(o.l1, "past 4s: AF off,"); CP(o.l2, "no shot, mode card");
    CP(o.bar, "LET GO: SHOOT");
    shoot("af-hold", m, T); }

  { UiModel m = handheld();            // AF LOCK mode: focus locked by a tap, counting down
    ObScreen& o = m.ob.scr;
    o.kind = ObKind::Text;
    CP(o.title, "AF LOCK"); CP(o.big, "AF LOCKED"); CP(o.right, "22s"); o.progress = 733;
    CP(o.l1, "tap fires at once"); CP(o.l2, "auto release at 0");
    CP(o.bar, "TAP: FIRE HOLD: UNLOCK");
    shoot("af-lock", m, T); }

  { UiModel m = handheld();            // self-timer counting down (timer set to 5 s)
    ObScreen& o = m.ob.scr;
    o.kind = ObKind::Count;
    CP(o.title, "TIMER 5s"); CP(o.big, "3"); CP(o.l1, "AF ON");
    CP(o.bar, "ANY PRESS: STOP");
    shoot("timer", m, T); }

  { UiModel m = handheld();            // interval run
    ObScreen& o = m.ob.scr;
    o.kind = ObKind::Text;
    CP(o.title, "INTERVAL"); CP(o.big, "37/120"); CP(o.right, "next 6s"); o.progress = 308;
    CP(o.l1, "ok 37 fail 0 skip 0"); CP(o.l2, "AIR 74% left");
    CP(o.bar, "ANY PRESS: STOP");
    shoot("interval", m, T); }

  { UiModel m = handheld();            // VIDEO mode home screen: a tap starts recording
    CP(m.ob.scr.title, "VIDEO");
    CP(m.ob.scr.bar, "TAP: REC  HOLD: MODES");
    shoot("video", m, T); }

  { UiModel m = handheld();            // mode card
    ObScreen& o = m.ob.scr;
    o.kind = ObKind::List;
    CP(o.head, "MODE  5");
    const char* it[] = {"SHOT", "AF LOCK", "TIMER", "INTERVAL", "VIEWS", "SETUP", "OFF"};
    const char* va[] = {"", "30s", "10s", "10sx120", "", "", ""};
    o.n = 7; o.sel = 2;
    for (int i = 0; i < 7; i++) { CP(o.item[i], it[i]); CP(o.val[i], va[i]); }
    CP(o.bar, "TAP NEXT     HOLD PICK");
    shoot("mode-card", m, T); }

  { UiModel m = handheld();            // LINK view page (label bar hidden)
    m.page = Page::Link; m.ob.view_bar = false;
    m.margin_db = 9.5f; m.bars = 3;
    shoot("link", m, T); }

  { UiModel m = handheld();            // GPS view page: distance and direction to the camera
    m.page = Page::Gps; m.ob.view_bar = false;
    m.gps_on = true; m.hh_fix = true; m.cam_fix = true; m.sats = 9; m.hdop = 1.1f;
    m.dist_m = 90; m.bearing_deg = 40;
    shoot("gps", m, T); }

  { UiModel m = handheld();            // delivered, but no outcome came back
    m.shot.stage = ShotStage::Failed; m.shot.err = apperr::NO_RESULT; m.shot.at_ms = T - 6000; m.shot.count = 13;
    CP(m.ob.info, "#13 0.2s  AIR 98%");
    shoot("unknown", m, T); }

  { UiModel m;                         // setup menu, relay handheld
    m.role = Role::Handheld; m.mode = Mode::Relay;
    static const char* it[] = {"Role", "Mode", "Pair boards", "Shutter", "Press", "Focus wait", "AF limit",
                               "Modes shown", "Wake press", "SF", "Antenna", "GPS", "Reset taps",
                               "Aux button", "LED", "Tally LED", "About", "Factory reset", "Exit"};
    static const char* va[] = {"Handheld", "Relay", "Paired", "PRG", "A", "500 ms", "4 s", "Basic", "Act",
                               "Auto", "3 dBi", "Off", "On", "Off", "Bright", "Off", "SLR 0.3.0", nullptr, nullptr};
    m.menu.active = true; m.menu.title = "SETUP 0.3.0"; m.menu.n = 19; m.menu.sel = 4;
    for (int i = 0; i < 19; i++) { m.menu.items[i] = it[i]; m.menu.values[i] = va[i]; }
    shoot("menu", m, T); }

  { UiModel m;                         // camera unit status page
    m.role = Role::Camera; m.page = Page::Camera;
    m.cam = CamLink::Ready; m.cam_text = "READY"; m.cam_name = "X-E5";
    m.lora_ok = true; m.bars = 4; m.sf = 7; m.ble_rssi = -58; m.rssi_up = -86; m.snr_up = 9.0f;
    m.gps_on = true; m.hh_fix = true; m.hh_batt = 86; m.cam_batt = 71;
    shoot("camera-unit", m, T); }

  return 0;
}
