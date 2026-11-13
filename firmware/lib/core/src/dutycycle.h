// Rolling one-hour transmit duty-cycle governor (one-minute buckets).
#pragma once
#include <stdint.h>

// 61 buckets, not 60: bucket k covers a whole minute, and airtime sent at its end is still
// inside the true 3600 s window until a full hour after that minute ENDS. Dropping it after
// 60 would forget up to 59 s of airtime early (up to ~10.8 % in a sliding hour).
constexpr int DUTY_BUCKETS = 61;

// Plain copy of the governor's state, so it can live in RTC memory / NVS across resets.
struct DutySnapshot {
  uint32_t bucket_ms[DUTY_BUCKETS];
  uint32_t cur_minute;
  uint8_t init;
};

class DutyCycle {
 public:
  explicit DutyCycle(float limit_fraction = 0.10f) : limit_(limit_fraction) {}
  // priority (shutter commands, acks): allowed while used+airtime <= 100 % of budget.
  // non-priority (pings): allowed while used+airtime <= 80 % of budget.
  bool canSend(uint32_t airtime_ms, uint32_t now_ms, bool priority);
  void record(uint32_t airtime_ms, uint32_t now_ms);
  uint32_t usedMs(uint32_t now_ms);          // airtime in the last 3600 s
  uint16_t usedPermille(uint32_t now_ms);    // usedMs / (3600 s * limit) * 1000, capped 1000
  void save(DutySnapshot& out) const;
  void restore(const DutySnapshot& in);
  // Cold start (power-on: the clock restarted, so in.cur_minute means nothing now). Restores
  // the saved buckets with their relative ages, as if the power cut took no time at all: real
  // time only moves them further into the past, so this never under-counts, and the history
  // still rolls off as it ages (it is not re-booked as fresh airtime on every boot).
  void restoreShifted(const DutySnapshot& in, uint32_t now_ms);
  // Conservative start when the real history is unknown: book ms as used just now.
  void precharge(uint32_t ms, uint32_t now_ms) { record(ms, now_ms); }
  uint32_t budgetMs() const;                 // the full hourly budget (limit * 3600 s)
  // Real time became known after a cold start: the saved snapshot `snap` (restored at boot_ms
  // with restoreShifted) is really `extra_ms` older than assumed. Moves only its share of the
  // buckets back by whole minutes; airtime booked since boot (incl. the precharge) is untouched.
  void reageSnapshot(const DutySnapshot& snap, uint32_t boot_ms, uint32_t extra_ms, uint32_t now_ms);
 private:
  void roll(uint32_t now_ms);
  float limit_;
  uint32_t bucket_ms_[DUTY_BUCKETS] = {0};
  uint32_t cur_minute_ = 0;
  bool init_ = false;
};

// Cold start (history lost): the airtime that may have gone out after the last snapshot and
// must be booked on top of it. unsaved_bound only if the store says something was recorded
// after that snapshot (dirty), else 0; never more than what keeps saved_sum + it <= budget.
uint32_t duty_cold_precharge_ms(uint32_t saved_sum, uint32_t unsaved_bound, uint32_t budget);
uint32_t duty_snapshot_sum(const DutySnapshot& s);
// The whole cold-start policy: restore saved (if any) with its ages kept, then book the
// unsaved bound at now only if dirty (airtime was recorded after that snapshot was written).
// Returns the extra booked. A caller that then saves the result and clears dirty books the
// bound once; N further cold starts with nothing recorded add nothing.
uint32_t duty_cold_start(DutyCycle& d, const DutySnapshot* saved, bool dirty,
                         uint32_t unsaved_bound, uint32_t now_ms);
