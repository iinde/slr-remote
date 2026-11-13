// SLR Remote (Super Long Range Remote), formerly XE5 Remote. One firmware, two roles. See docs/specs/2026-11-07-xe5-remote-design.md
// Fujifilm BLE protocol ported from gkoh/furble (MIT licence, (c) 2020 Guo-Rong Koh).
#include <Arduino.h>
#include "app.h"
#include "app_internal.h"
#include "bootgest.h"
#include "config.h"
#include "duty.h"
#include "hw.h"
#include "store.h"
#include "ui.h"

static Settings g_settings;
// The role this boot runs. The console may change g_settings.role (saved for the next boot),
// but loop() must never switch to the other role's app, which was never set up.
static Role g_active_role = Role::Unset;

void setup() {
  setCpuFrequencyMhz(80);  // 80 MHz is plenty here and the lowest BLE allows: saves battery
  Serial.begin(115200);
  hw_init();
  store_begin();
  store_load(g_settings);
  // A timer wake is a stuck-PRG sleep (hw_deep_sleep): straight back to sleep, nothing else.
  hw_stuck_wake_check(app_wake_extra(g_settings));
  // R1 reset taps and "menu next boot", before the screen lights (the cue for a second tap).
  const MenuNext next = bootgest_begin(g_settings);
  duty_begin();  // before anything can transmit (board pairing in the menu included)
  ui_begin();
  rlog_begin();

  // Hold PRG at power-on (not on a deep-sleep wake), RST tapped twice, asked for by the app or
  // the console, or no role yet -> setup menu.
  pinMode(pins::BTN_PRG, INPUT_PULLUP);
  delay(30);
  bool prg_held = !hw_woke_from_sleep() && digitalRead(pins::BTN_PRG) == LOW;
  if (prg_held || next != MenuNext::None || g_settings.role == Role::Unset) {
    menu_run(g_settings, (uint8_t)next);  // saves settings; returns when user exits
  }

#ifndef XE5_FW_VERSION
#define XE5_FW_VERSION "dev"
#endif
  if (!hw_woke_from_sleep()) ui_splash(g_settings.role == Role::Camera, "v" XE5_FW_VERSION);   // cold boot only
  g_active_role = g_settings.role == Role::Camera ? Role::Camera : Role::Handheld;
  if (g_active_role == Role::Camera) app_camera_setup(g_settings);
  else app_handheld_setup(g_settings);
}

void loop() {
  bootgest_tick();
  serial_poll(g_settings);
  if (g_active_role == Role::Camera) app_camera_loop();
  else app_handheld_loop();
  delay(1);  // let the idle task run, so the CPU can rest between passes
}
