#include "dutycycle.h"

#include <string.h>

// bucket_ms_[0] is the current minute, bucket_ms_[N - 1] the oldest (it started 60 minutes
// before the current one, so part of it can still be inside the last 3600 s). cur_minute_ holds
// the millis() at which bucket 0 started; only differences are used, so a millis()
// wrap is harmless.
namespace {
constexpr uint32_t MINUTE_MS = 60000;
constexpr uint32_t HOUR_MS = 3600000;
constexpr int N = DUTY_BUCKETS;
}  // namespace

void DutyCycle::roll(uint32_t now_ms) {
  if (!init_) {
    init_ = true;
    cur_minute_ = now_ms;
    memset(bucket_ms_, 0, sizeof bucket_ms_);
    return;
  }
  const uint32_t elapsed = now_ms - cur_minute_;
  if ((int32_t)elapsed < 0) return;  // stale timestamp: keep the current bucket
  const uint32_t steps = elapsed / MINUTE_MS;
  if (steps == 0) return;
  if (steps >= (uint32_t)N) {
    memset(bucket_ms_, 0, sizeof bucket_ms_);
    cur_minute_ = now_ms;
    return;
  }
  memmove(bucket_ms_ + steps, bucket_ms_, (N - steps) * sizeof bucket_ms_[0]);
  memset(bucket_ms_, 0, steps * sizeof bucket_ms_[0]);
  cur_minute_ += steps * MINUTE_MS;
}

static uint32_t budget_ms(float limit) {
  if (!(limit > 0.f)) return 0;
  return (uint32_t)((double)limit * HOUR_MS + 0.5);
}

uint32_t DutyCycle::usedMs(uint32_t now_ms) {
  roll(now_ms);
  uint32_t sum = 0;
  for (int i = 0; i < N; i++) sum += bucket_ms_[i];
  return sum;
}

bool DutyCycle::canSend(uint32_t airtime_ms, uint32_t now_ms, bool priority) {
  const uint64_t budget = budget_ms(limit_);
  const uint64_t allowed = priority ? budget : budget * 8 / 10;
  return (uint64_t)usedMs(now_ms) + airtime_ms <= allowed;
}

void DutyCycle::record(uint32_t airtime_ms, uint32_t now_ms) {
  roll(now_ms);
  bucket_ms_[0] += airtime_ms;
}

void DutyCycle::save(DutySnapshot& out) const {
  memcpy(out.bucket_ms, bucket_ms_, sizeof bucket_ms_);
  out.cur_minute = cur_minute_;
  out.init = init_ ? 1 : 0;
}

void DutyCycle::restore(const DutySnapshot& in) {
  memcpy(bucket_ms_, in.bucket_ms, sizeof bucket_ms_);
  cur_minute_ = in.cur_minute;
  init_ = in.init != 0;
}

void DutyCycle::reageSnapshot(const DutySnapshot& snap, uint32_t boot_ms, uint32_t extra_ms, uint32_t now_ms) {
  roll(now_ms);
  const uint32_t since = (cur_minute_ - boot_ms) / MINUTE_MS;   // minutes the snapshot has rolled already
  const uint32_t more = extra_ms / MINUTE_MS;                    // whole minutes only: never over-ages
  if (more == 0) return;
  for (int i = 0; i < N; i++) {
    const uint32_t at = (uint32_t)i + since;                     // where snapshot bucket i sits now
    if (at >= (uint32_t)N) break;
    const uint32_t v = snap.bucket_ms[i];
    bucket_ms_[at] = bucket_ms_[at] > v ? bucket_ms_[at] - v : 0;
    const uint32_t to = at + more;
    if (to < (uint32_t)N) bucket_ms_[to] += v;                   // older than the window: dropped
  }
}

void DutyCycle::restoreShifted(const DutySnapshot& in, uint32_t now_ms) {
  memcpy(bucket_ms_, in.bucket_ms, sizeof bucket_ms_);
  cur_minute_ = now_ms;  // the saved current minute starts again now: never earlier
  init_ = true;
}

uint32_t duty_snapshot_sum(const DutySnapshot& s) {
  uint64_t v = 0;
  for (int i = 0; i < N; i++) v += s.bucket_ms[i];
  return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)v;
}

uint16_t DutyCycle::usedPermille(uint32_t now_ms) {
  const uint64_t budget = budget_ms(limit_);
  const uint64_t used = usedMs(now_ms);
  if (budget == 0) return used ? 1000 : 0;
  const uint64_t pm = used * 1000 / budget;
  return (uint16_t)(pm > 1000 ? 1000 : pm);
}

uint32_t DutyCycle::budgetMs() const { return budget_ms(limit_); }

uint32_t duty_cold_precharge_ms(uint32_t saved_sum, uint32_t unsaved_bound, uint32_t budget) {
  if (saved_sum >= budget) return 0;
  return unsaved_bound > budget - saved_sum ? budget - saved_sum : unsaved_bound;
}

uint32_t duty_cold_start(DutyCycle& d, const DutySnapshot* saved, bool dirty,
                         uint32_t unsaved_bound, uint32_t now_ms) {
  uint32_t sum = 0;
  if (saved) {
    d.restoreShifted(*saved, now_ms);
    sum = duty_snapshot_sum(*saved);
  }
  const uint32_t extra = dirty ? duty_cold_precharge_ms(sum, unsaved_bound, d.budgetMs()) : 0;
  if (extra) d.precharge(extra, now_ms);
  return extra;
}
