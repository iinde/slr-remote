#include "seqplan.h"

#include "airtime.h"
#include "linkmgr.h"

namespace {

uint32_t air(uint8_t sf, uint8_t len) { return lora_airtime_ms(sf, 125.0f, 5, 8, len); }

uint32_t count_eff(uint16_t every_s, uint16_t count) {
  if (count) return count;
  return PLAN_MAX_RUN_S / (every_s ? every_s : 1) + 1;
}

uint32_t permille_up(uint64_t ms) {
  const uint64_t p = (ms * 1000 + PLAN_BUDGET_MS - 1) / PLAN_BUDGET_MS;
  return p > 65535 ? 65535 : (uint32_t)p;
}

struct Need {
  uint32_t hh, cam;  // per mille
};

Need need_for(uint8_t sf, uint16_t every_s, uint32_t shots_total) {
  const uint32_t dur_s = shots_total ? (shots_total - 1) * (uint32_t)every_s : 0;
  const uint32_t window_ms = (dur_s < 3600 ? dur_s : 3600) * 1000;
  uint32_t per_hour = 3600 / (every_s ? every_s : 1) + 1;
  const uint32_t shots = shots_total < per_hour ? shots_total : per_hour;
  const uint32_t ping = air(sf, PLAN_PING_FRAME), ack = air(sf, PLAN_ACK_FRAME);
  const uint32_t iv = ping_interval_ms(ping, ack, 2000);
  const uint32_t pings = iv ? window_ms / iv : 0;
  // x1.5 retry margin on the shots (integer: *3/2).
  const uint64_t hh = (uint64_t)shots * plan_shot_cost_ms(sf, false) * 3 / 2 + (uint64_t)pings * ping;
  const uint64_t cam = (uint64_t)shots * plan_shot_cost_ms(sf, true) * 3 / 2 + (uint64_t)pings * ack;
  return Need{permille_up(hh), permille_up(cam)};
}

bool fits(const PlanIn& in, uint16_t every_s, uint32_t shots, uint16_t* need, uint16_t* have) {
  const Need n = need_for(in.sf, every_s, shots);
  const uint32_t have_hh = in.hh_permille >= 1000 ? 0 : 1000 - in.hh_permille;
  const uint32_t have_cam = in.cam_permille >= 1000 ? 0 : 1000 - in.cam_permille;
  // Too fast to finish one round trip between shots: never fits.
  const uint32_t trip = plan_shot_cost_ms(in.sf, false) + plan_shot_cost_ms(in.sf, true);
  const bool fast_ok = (uint32_t)every_s * 1000 > trip + 500;
  const bool ok = fast_ok && n.hh <= have_hh && n.cam <= have_cam;
  if (need && have) {
    // Report the board with the least room to spare.
    const int32_t spare_hh = (int32_t)have_hh - (int32_t)n.hh;
    const int32_t spare_cam = (int32_t)have_cam - (int32_t)n.cam;
    const bool cam_worse = spare_cam <= spare_hh;
    *need = (uint16_t)(cam_worse ? n.cam : n.hh);
    *have = (uint16_t)(cam_worse ? have_cam : have_hh);
  }
  return ok;
}

}  // namespace

uint32_t plan_shot_cost_ms(uint8_t sf, bool camera_side) {
  return camera_side ? 2 * air(sf, PLAN_ACK_FRAME) : air(sf, PLAN_SHOT_FRAME);
}

PlanOut plan_interval(const PlanIn& in) {
  PlanOut o;
  const uint16_t every = in.every_s ? in.every_s : 1;
  const uint32_t shots = count_eff(every, in.count);
  o.duration_s = (shots - 1) * (uint32_t)every;
  o.fits = fits(in, every, shots, &o.need_permille, &o.have_permille);
  if (o.fits) {
    o.fit_every_s = every;
    o.fit_count = in.count;
    return o;
  }
  static const uint16_t kEvery[] = {2, 3, 5, 10, 15, 20, 25, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1800, 3600};
  for (uint16_t e : kEvery) {
    if (e <= every) continue;
    if (fits(in, e, count_eff(e, in.count), nullptr, nullptr)) {
      o.fit_every_s = e;
      break;
    }
  }
  // Need grows with the shot count: binary search the largest count that fits.
  uint32_t lo = 0, hi = shots - 1;
  while (lo < hi) {
    const uint32_t mid = (lo + hi + 1) / 2;
    if (fits(in, every, mid, nullptr, nullptr)) lo = mid;
    else hi = mid - 1;
  }
  o.fit_count = (uint16_t)(lo > 65535 ? 65535 : lo);
  return o;
}

bool plan_shot_fits(uint8_t sf, uint16_t hh_permille, uint16_t cam_permille) {
  const uint64_t hh_left = hh_permille >= 1000 ? 0 : (uint64_t)PLAN_BUDGET_MS * (1000 - hh_permille) / 1000;
  const uint64_t cam_left = cam_permille >= 1000 ? 0 : (uint64_t)PLAN_BUDGET_MS * (1000 - cam_permille) / 1000;
  return hh_left >= plan_shot_cost_ms(sf, false) && cam_left >= plan_shot_cost_ms(sf, true);
}
