// Radio region: the band, bandwidth, power rule and airtime limit a board runs under.
// Both boards must be set to the same region or they cannot hear each other.
#pragma once
#include <stdint.h>

// Stored in NVS by value: never renumber.
enum class Region : uint8_t { Unset = 0, EU868 = 1, US915 = 2 };

// How a region states its transmit power limit.
enum class PowerRule : uint8_t {
  Erp,        // limit is radiated power against a dipole (EU): antenna gain always counts
  Conducted,  // limit is power into the antenna (US): gain counts only above full_gain_dbi
};

struct RegionCfg {
  float freq_mhz;
  float bw_khz;
  float duty_limit;       // share of a rolling hour the governor allows (1.0 = no legal limit)
  PowerRule rule;
  int8_t limit_dbm;       // ERP or conducted, per rule
  int8_t full_gain_dbi;   // Conducted: antenna gain allowed at the full limit
  const char* about;      // channel as shown on the About screen
};

// EU868: 869.40 to 869.65 MHz, 500 mW ERP (27 dBm), 10 % duty cycle, 125 kHz.
// US915: one fixed channel at 918.0 MHz run as a digital modulation system (FCC 15.247):
// 500 kHz is the least bandwidth that rule accepts, and it has no dwell or duty limit. The
// rule allows 30 dBm conducted, but its spectral density limit (8 dBm in 3 kHz) is reached
// near 28.5 dBm for 500 kHz LoRa, so the cap here is 25 dBm, which is also what this
// hardware can reach. Antenna gain above 6 dBi comes off dB for dB.
// Unset has no row of its own: it borrows the EU figures (the strictest) so airtime and
// timing maths stay finite, and region_can_tx() keeps the radio silent.
const RegionCfg& region_cfg(Region r);

bool region_valid(uint8_t v);              // a stored value that names a region (Unset included)
inline bool region_can_tx(Region r) { return r == Region::EU868 || r == Region::US915; }

// The region a board starts with. A stored key wins. No key: a board that already has a role
// was set up by a build from before regions existed, which was EU only, so it stays EU and
// its owner sees no change. A fresh board (no role) starts Unset and cannot transmit until
// the user picks one.
Region region_on_load(bool has_key, uint8_t stored, bool role_set);

// Saving. The region key is written whenever the region changed, and on every save until the
// key exists: a region that was only worked out from the role must be pinned before the role
// can change, and a fresh board given a role before a region must come back Unset, not EU.
// The save writes region first, and writes the role only once the region key is in flash, so
// a save that stops part way can never leave a role with no region key.
inline bool region_must_write(bool changed, bool key_stored) { return changed || !key_stored; }
inline bool region_role_may_write(bool key_stored) { return key_stored; }

// Menu step: EU 868 -> US 915 -> EU 868. Unset goes to EU 868 and is never returned to.
Region region_next(Region r);

const char* region_label(Region r);        // menu: "Choose", "EU 868", "US 915"
const char* region_key(Region r);          // console: "unset", "eu", "us"
bool region_from_key(const char* k, Region& out);  // "eu" / "us" only

// The governor's full hourly budget for the region (duty_limit of 3600 s).
uint32_t region_budget_ms(Region r);

// Highest SX1262 setting that keeps the region's power limit with this antenna behind a
// front end of fem_gain_db, floored, then held between sx_min and sx_max (the hardware).
//   Erp:       limit + 2.15 - gain - fem
//   Conducted: limit - max(0, gain - full_gain) - fem
int8_t region_legal_sx_dbm(Region r, int8_t ant_gain_dbi, int8_t fem_gain_db, int8_t sx_max,
                           int8_t sx_min);
