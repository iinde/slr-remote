// Glue shared by the role apps, the setup menu and the serial console (not a public contract).
#pragma once
#include <stdint.h>

#include "hw.h"
#include "proto.h"
#include "store.h"

const char* app_dev_name();    // "XE5-xxxxx": 5 hex digits of the chip MAC
void app_fuji_start();         // fuji.begin(app_dev_name()) once per boot
bool app_fuji_started();

void app_handheld_print_stat();  // serial 'stat', handheld role
void app_camera_print_stat();    // serial 'stat', camera role

inline int8_t app_clamp8(float v) {
  if (!(v == v)) return 0;  // NAN
  if (v > 127.f) return 127;
  if (v < -128.f) return -128;
  return (int8_t)(v < 0 ? v - 0.5f : v + 0.5f);
}

inline const char* app_camlink_text(CamLink c) {
  switch (c) {
    case CamLink::NotPaired: return "NO CAM";
    case CamLink::Scanning: return "SCAN";
    case CamLink::Connecting: return "CONN";
    case CamLink::Ready: return "READY";
    default: return "--";
  }
}

// Extra deep-sleep wake pins for this board's settings (handheld only).
inline uint8_t app_wake_extra(const Settings& s) {
  if (s.role == Role::Camera) return 0;
  if (s.two_stage) return HW_WAKE_TWO_STAGE;
  return s.aux_pin ? HW_WAKE_AUX : 0;
}

// Restart into the setup menu (what: MenuNext), releasing anything held first. Never returns
// if a role app runs. The handheld one returns false if the handheld app is not running.
void app_request_menu(uint8_t what);
bool app_handheld_request_menu(uint8_t what);
void app_camera_request_menu(uint8_t what);
// 'pair ...' typed while the setup menu runs: the menu picks it up (0 = none).
uint8_t serial_take_menu_request();

// Handheld: settings were changed from the serial console (applies what can apply live).
void app_handheld_settings_changed();
// Handheld: a console line arrived. Counts as activity for the idle sleep (no-op otherwise).
void app_handheld_note_activity();
// Handheld one-button state as one JSON object (serial 'jstat'); camera: a short one.
void app_handheld_print_json_status();
