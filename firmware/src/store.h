// Persistent settings (NVS via Preferences, namespace "xe5") and the range log
// (LittleFS file /range.csv).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "region.h"

struct Settings {
  Role role = Role::Unset;
  // Radio region. Unset = not chosen yet: the radio never transmits (see region_on_load for
  // what a board from before this setting loads as).
  Region region = Region::Unset;
  Mode mode = Mode::Direct;
  bool two_stage = false;        // external two-stage switch fitted (PRG becomes page/menu)
  uint16_t focus_ms = 500;       // one-press focus wait
  bool adaptive_sf = true;
  uint8_t fixed_sf = 7;
  int8_t ant_gain_dbi = 3;
  bool geotag = false;           // camera board: push GPS to camera
  bool hh_gps = false;           // handheld: GNSS on while awake
  uint8_t last_sf = 7;           // last agreed SF (both roles)
  // One button (docs/specs/2026-11-08-one-button-ux.md). Missing keys load as these.
  uint8_t press = 1;             // PressStyle: 0 classic (bench v0.1), 1 A, 2 B, 3 C
  uint8_t af_limit_s = 4;        // A: AF lets go after this, nothing fires (2..15)
  uint8_t aflock_s = 30;         // AF LOCK auto release (5..120)
  uint8_t timer_s = 10;          // TIMER (1..60)
  uint16_t iv_every_s = 10;      // INTERVAL every (2..3600)
  uint16_t iv_count = 120;       // INTERVAL shots, 0 = until stopped (4 h cap)
  uint16_t burst_ms = 1000;      // BURST tap (200..10000)
  uint16_t bulb_s = 120;         // BULB (1..1800)
  uint16_t hold_cap_s = 1800;    // direct BULB/BURST hard cap (bench-tested: the X-E5 lets go by itself)
  uint8_t n_modes = 4;           // modes on the card, in order (ObMode values)
  uint8_t modes[8] = {0, 1, 2, 3, 0, 0, 0, 0};
  bool wake_act = true;          // wake tap in SHOT takes the photo
  bool rst_gest = true;          // R1: RST twice = setup menu
  uint8_t aux_pin = 0;           // R2/R2b second button: 0 off, 6 = GPIO6
  bool led_cues = true;          // LED blinks for AF, fired, stopped
  uint8_t led_bright = 2;        // 1 dim, 2 bright (when led_cues)
  uint8_t tally_pin = 0;         // external tally LED: 0 off, GPIO 3/4/6, or 34 = LED across GPIO3 (+) and GPIO4 (0 V)
  bool rlog_always = false;      // range log: every shot, not only on the RANGE page
  uint8_t ob_mode = 0;           // SHOT or AF LOCK, kept across power-on (others are not)
};
void store_begin();
void store_load(Settings& s);       // defaults for missing keys
// False if NVS would not open or any write failed (the setting then lasts only until reboot).
bool store_save(const Settings& s);
// Small NVS flags outside Settings (R1 reset taps, menu-next-boot, run flag). get returns def
// if missing; ok (optional) is false if NVS could not be opened.
uint8_t store_get_u8(const char* key, uint8_t def, bool* ok = nullptr);
bool store_put_u8(const char* key, uint8_t v);  // false if not written
constexpr const char* NVS_RST_TAPS = "rstc";     // R1: 1 = a power-on less than 3 s ago
constexpr const char* NVS_MENU_NEXT = "menunext";  // 1 setup, 2 setup + pair cam, 3 + pair boards
constexpr const char* NVS_RUN_FLAG = "runf";     // 1 = an INTERVAL / BULB was running

struct PairRecord {
  bool valid = false;
  uint8_t key[32];
  uint16_t net = 0;
  uint32_t peer_ctr = 0;           // highest counter accepted from the peer
};
void store_load_pair(PairRecord& p);
// Atomic (two slots + one commit byte): false if any NVS write failed, and then the previous
// pairing is still the stored one.
bool store_save_pair(const PairRecord& p);
void store_save_peer_ctr(uint32_t ctr);   // throttled: writes at most every 256 increments
// Writes ctr exactly if it is above the stored value (handheld, before deep sleep). False on
// an NVS failure.
bool store_put_peer_ctr(uint32_t ctr);
// Reservation scheme (camera board). Call BEFORE acting on an accepted frame. Keeps the
// stored value strictly above every counter ever accepted: when ctr reaches it, writes
// ctr + 256 (one NVS write per 256 frames). Seed the ReplayGuard from the stored value at
// boot and no frame accepted before a reset can be accepted again. Returns false if the
// reservation covering ctr could not be persisted (NVS full/failed): the caller must then
// not act on the frame (a reset could let it, or a retry of it, be accepted again).
// The peer must skip its counter past the reservation when it stops being answered
// (store_skip_ctr), or its next <= 256 frames after our reboot are refused.
bool store_reserve_peer_ctr(uint32_t ctr);
void store_clear_pair();

// Own TX counter, never reused across reboots: reserves blocks of 1024 in NVS, and starts
// each boot 256 above the stored ceiling (past any counter reservation a peer holds).
// Returns 0 ("no counter": send nothing) if the new block could not be persisted.
uint32_t store_next_ctr();
// Jumps the own counter forward by n (handheld: past a rebooted camera's reservation).
void store_skip_ctr(uint32_t n);

// Opaque blobs (camera record etc.)
bool store_get_blob(const char* key, void* buf, size_t len);  // false if missing/size mismatch
bool store_put_blob(const char* key, const void* buf, size_t len);  // false if not written
bool store_erase(const char* key);  // true if the key is gone (or never existed)
void store_factory_reset();         // wipe namespace + range log

bool rlog_begin();                  // mount LittleFS (format on fail), create file with header
// line includes '\n'. false if not written (no FS, full). Rotates /range.csv to
// /range.old.csv once it passes 256 KB (the rotation costs a rename + header write).
bool rlog_append(const char* line);
uint32_t rlog_rows();               // rows in /range.csv; counted on first use, not at boot
                                    // (a full read: never call it from a loop path)
bool rlog_rows_known();             // rlog_rows() is a cached figure (no file scan)
size_t rlog_size();                 // bytes in /range.csv (one stat, no read)
// Streams /range.old.csv then /range.csv to Serial in pieces, never blocking loop():
// start, then call step() from loop() until it returns false.
bool rlog_dump_start();
bool rlog_dump_step(size_t max_bytes);
bool rlog_dump_active();
void rlog_clear();                  // truncate to header
