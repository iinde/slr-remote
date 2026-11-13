#include "region.h"

#include <math.h>
#include <string.h>

namespace {

constexpr RegionCfg kEu{869.525f, 125.0f, 0.10f, PowerRule::Erp, 27, 0, "869.5"};
constexpr RegionCfg kUs{918.0f, 500.0f, 1.0f, PowerRule::Conducted, 25, 6, "918.0"};
constexpr RegionCfg kUnset{869.525f, 125.0f, 0.10f, PowerRule::Erp, 27, 0, "--"};

}  // namespace

const RegionCfg& region_cfg(Region r) {
  switch (r) {
    case Region::EU868: return kEu;
    case Region::US915: return kUs;
    default: return kUnset;
  }
}

bool region_valid(uint8_t v) { return v <= static_cast<uint8_t>(Region::US915); }

Region region_on_load(bool has_key, uint8_t stored, bool role_set) {
  if (has_key) return region_valid(stored) ? static_cast<Region>(stored) : Region::Unset;
  return role_set ? Region::EU868 : Region::Unset;
}

Region region_next(Region r) { return r == Region::EU868 ? Region::US915 : Region::EU868; }

const char* region_label(Region r) {
  switch (r) {
    case Region::EU868: return "EU 868";
    case Region::US915: return "US 915";
    default: return "Choose";
  }
}

const char* region_key(Region r) {
  switch (r) {
    case Region::EU868: return "eu";
    case Region::US915: return "us";
    default: return "unset";
  }
}

bool region_from_key(const char* k, Region& out) {
  if (!k) return false;
  if (!strcmp(k, "eu")) out = Region::EU868;
  else if (!strcmp(k, "us")) out = Region::US915;
  else return false;
  return true;
}

uint32_t region_budget_ms(Region r) {
  return (uint32_t)((double)region_cfg(r).duty_limit * 3600000.0 + 0.5);
}

int8_t region_legal_sx_dbm(Region r, int8_t ant_gain_dbi, int8_t fem_gain_db, int8_t sx_max,
                           int8_t sx_min) {
  const RegionCfg& c = region_cfg(r);
  float limit;
  if (c.rule == PowerRule::Erp) {
    limit = c.limit_dbm + 2.15f - ant_gain_dbi - fem_gain_db;
  } else {
    const int over = ant_gain_dbi > c.full_gain_dbi ? ant_gain_dbi - c.full_gain_dbi : 0;
    limit = (float)(c.limit_dbm - over - fem_gain_db);
  }
  int lim = static_cast<int>(floorf(limit));
  if (lim > sx_max) lim = sx_max;
  if (lim < sx_min) lim = sx_min;
  return static_cast<int8_t>(lim);
}
