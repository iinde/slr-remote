// Boot-time ways into the setup menu that do not need PRG held through a reset:
//  - R1 reset taps: RST twice (spec 3.2, logic in lib/core/rstgest.h);
//  - "menu next boot": the mode card's SETUP, or 'setup' / 'pair' on the serial console.
// Also the INTERVAL/BULB run flag, so a reset mid-run is reported on the next boot.
#pragma once
#include <stdint.h>

#include "store.h"

enum class MenuNext : uint8_t { None = 0, Setup = 1, PairCam = 2, PairBoards = 3 };

// Call once, very early in setup() (after store_load, before the screen lights: the screen
// lighting is the cue for the second tap). Returns what the menu should do (None = no menu).
MenuNext bootgest_begin(const Settings& s);
// The first 3 s after a single RST tap: the boot screen says "RST AGAIN NOW = SETUP".
bool bootgest_hint();
// X23: the menu this boot was opened by the RST taps alone (not asked for by the app or the
// console). The flag behind it can be stale, so the menu leaves early if nobody presses PRG.
bool bootgest_by_taps();
// Clears the tap flag once the window has passed. Call from every loop (role apps, menu).
void bootgest_tick();
// Clears it now (before deep sleep or a restart inside the window).
void bootgest_settle();
// Restart into the setup menu (optionally starting a pairing). Never returns. The caller has
// already released anything held on the camera.
[[noreturn]] void bootgest_restart_into_menu(MenuNext what);
