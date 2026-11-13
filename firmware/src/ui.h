// OLED UI (U8g2, SSD1306 128x64 I2C). Renders a UiModel; owns no app state.
#pragma once
#include <stdint.h>
#include "config.h"
#include "modes.h"
#include "proto.h"

enum class Page : uint8_t { Main = 0, Link, Hunt, Gps, Range, Camera, Count };

struct UiShot {
  ShotStage stage = ShotStage::Idle;
  uint32_t at_ms = 0;      // when the stage last changed
  uint16_t latency_ms = 0; // press -> Delivered (relay) / press -> Accepted (direct)
  uint8_t err = 0;
  uint16_t count = 0;      // shots this session
  bool focus_only = false; // a half-press (Focus) track: AF outcome, not a shot
  bool late = false;       // proof of delivery arrived after the press was reported failed
  // A press accepted locally but not yet started/on air (direct: waiting for the BLE link;
  // relay: queued, not transmitted yet). pending_until: when it expires (0 = no countdown).
  bool pending = false;
  uint32_t pending_until = 0;
};

constexpr uint8_t UI_MENU_MAX = 20;
struct UiMenu {
  bool active = false;
  const char* title = "";
  const char* items[UI_MENU_MAX] = {};
  const char* values[UI_MENU_MAX] = {};  // right-aligned value text, may be nullptr
  uint8_t n = 0, sel = 0;
};

// One-button press styles (A, B, C): the engine's screen plus the label bar.
struct UiOb {
  bool active = false;
  ObScreen scr;
  bool alert = false;        // RELEASE NOT CONFIRMED: the shutter may still be held
  const char* alert_line = "";
  char info[28] = {0};       // Home: small status row (#count, latency, AIR)
  bool view_bar = true;      // Home on a view page: draw the label bar over rows 53..63
};

struct UiModel {
  Role role = Role::Unset;
  Mode mode = Mode::Direct;
  Page page = Page::Main;
  // camera link (BLE). In relay mode on the handheld these come from Acks.
  CamLink cam = CamLink::None;
  const char* cam_text = "";   // FujiCam::stateText() or relayed equivalent
  const char* cam_name = "";
  int8_t ble_rssi = 0;
  // LoRa
  bool lora_ok = false;        // got an Ack recently (relay)
  bool link_checking = false;  // relay: gate open, first reply not due yet (never "no link")
  bool link_unchecked = false; // relay power-on: nothing sent yet (antenna gate), never asked
  uint8_t bars = 0;            // 0..4
  float margin_db = 0, rssi_up = 0, snr_up = 0, rssi_dn = 0, snr_dn = 0;
  uint8_t sf = 7;
  uint16_t bw_khz = 125;       // the active region's bandwidth
  int8_t tx_out_dbm = 0;
  float success_pct = 0;
  uint16_t rtt_ms = 0;
  bool rescue = false;
  bool adaptive = true;
  uint16_t duty_permille = 0;
  // batteries (255 = unknown/USB)
  uint8_t hh_batt = 255, cam_batt = 255;
  // shot
  UiShot shot;
  // GPS
  bool gps_on = false, hh_fix = false, cam_fix = false;
  uint8_t sats = 0;
  float hdop = 0;
  double dist_m = -1;          // <0 unknown
  double bearing_deg = 0;
  double own_lat = 0, own_lon = 0;  // this board's fix (valid when hh_fix)
  // hunt
  int16_t hunt_rssi = 0, hunt_best = -200;
  // range test
  bool range_on = false;
  uint16_t range_n = 0;
  float range_ok_pct = 0;
  // pairing / toasts
  const char* big1 = nullptr;  // if set: full-screen message (pair codes, errors)
  const char* big2 = nullptr;
  const char* toast = nullptr; // bottom line overlay
  UiMenu menu;
  UiOb ob;
};

void ui_begin();               // Vext must already be on; resets OLED via OLED_RST (~30 ms)
void ui_power(bool on);        // display on/off (U8g2 power save)
void ui_contrast(uint8_t c);    // 255 normal; lower = dimmed (a long run on a quiet screen)
void ui_render(const UiModel& m, uint32_t now);  // full redraw; call <= 10 Hz
// SLR Remote polish
void ui_splash(bool camera, const char* ver);   // blocking ~1.6 s boot animation (cold boot only)
bool ui_animating(uint32_t now);                // an animation is running: render at ~30 Hz
void ui_about(const char* const* lines, uint8_t n);   // full-screen About card (caller waits for a press)
