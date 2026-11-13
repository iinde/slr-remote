// Duty-cycle governor persistence. See duty.h.
#include "duty.h"
#include "utc.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <string.h>
#include <sys/time.h>

#include "config.h"
#include "dutycycle.h"
#include "store.h"

namespace {

constexpr uint32_t kMagic = 0x58445432;  // "XDT2": 61-bucket layout
constexpr uint32_t kCheckpointMs = 5 * 60 * 1000;
constexpr uint32_t kFailRetryMs = 30000;  // a failing NVS is not retried on every transmit
constexpr const char* kBlob = "duty2";     // DutySnapshot (61 buckets)
constexpr const char* kLegacyBlob = "duty";  // the old 60-bucket layout
constexpr const char* kDirtyKey = "dutyd";
constexpr const char* kStampKey = "dutyt";  // UTC (s) when the snapshot was written, 0 = unknown   // 1 = airtime recorded after the saved snapshot
// Airtime booked since the last NVS snapshot is lost on a power cut. A snapshot is due at
// kUnsavedMaxMs and is written from the app loop at an idle moment (duty_idle), never inside
// a press (an NVS page GC can take ~45-400 ms). If no idle moment comes, it is forced
// synchronously at kUnsavedHardMs. A cold start books kUnsavedBoundMs (that plus one longest
// frame, SF12 ~2.2 s) on top of the saved snapshot, but only when the dirty flag says
// something was recorded after it.
constexpr uint32_t kUnsavedMaxMs = 18000;
constexpr uint32_t kUnsavedHardMs = 24000;
constexpr uint32_t kUnsavedBoundMs = kUnsavedHardMs + 2500;

struct LegacySnapshot {
  uint32_t bucket_ms[60];
  uint32_t cur_minute;
  uint8_t init;
};

struct RtcDuty {
  uint32_t magic;
  DutySnapshot snap;
  uint32_t sum;
};

RTC_NOINIT_ATTR RtcDuty s_rtc;

DutyCycle g_duty{radiocfg::DUTY_LIMIT};
bool g_begun = false;
bool g_dirty = false;         // recorded since the last NVS snapshot
bool g_flag_nvs = false;      // the NVS dirty flag is (known to be) set
bool g_due = false;           // a snapshot is due at the next idle moment
bool g_store_fail = false;    // the last snapshot (or flag) write failed
uint32_t g_last_checkpoint = 0;
uint32_t g_last_fail = 0;
uint32_t g_unsaved_ms = 0;    // airtime recorded since the last NVS snapshot
uint32_t g_saved_sum = 0;     // sum of the snapshot in NVS (idle roll-off is persisted too)

uint32_t checksum(const DutySnapshot& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  uint32_t h = 2166136261u;  // FNV-1a
  for (size_t i = 0; i < sizeof s; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

void to_rtc() {
  g_duty.save(s_rtc.snap);
  s_rtc.sum = checksum(s_rtc.snap);
  s_rtc.magic = kMagic;
}

bool rtc_valid(uint32_t now) {
  if (s_rtc.magic != kMagic || s_rtc.sum != checksum(s_rtc.snap)) return false;
  // A clock that went backwards (power-on) would stop the buckets ever rolling off.
  return !s_rtc.snap.init || (int32_t)(now - s_rtc.snap.cur_minute) >= 0;
}

bool put_flag(bool on) {
  const uint8_t v = on ? 1 : 0;
  return store_put_blob(kDirtyKey, &v, 1);
}

bool get_flag() {
  uint8_t v = 0;
  return store_get_blob(kDirtyKey, &v, 1) && v != 0;
}

// Set the NVS dirty flag before the first airtime after a snapshot (one 1-byte write per
// snapshot cycle). A cold start adds the unsaved bound only while it is set.
void arm_flag() {
  if (g_flag_nvs || (g_store_fail && millis() - g_last_fail < kFailRetryMs)) return;
  if (put_flag(true)) {
    g_flag_nvs = true;
  } else {
    g_store_fail = true;
    g_last_fail = millis();
  }
}

// Writes the snapshot, then clears the dirty flag. Returns false if the snapshot failed.
bool write_snapshot() {
  const uint32_t stamp = utc_now();
  store_put_blob(kStampKey, &stamp, sizeof stamp);
  DutySnapshot snap;
  const uint32_t sum = g_duty.usedMs(duty_now_ms());  // roll first so stale minutes go
  g_duty.save(snap);
  g_last_checkpoint = millis();
  if (!store_put_blob(kBlob, &snap, sizeof snap)) {
    g_store_fail = true;
    g_last_fail = millis();
    Serial.println("[duty] snapshot write FAILED");
    return false;
  }
  g_saved_sum = sum;
  g_dirty = false;
  g_due = false;
  g_unsaved_ms = 0;
  g_store_fail = false;
  // Snapshot first, flag second: a failure in between only leaves the flag set (a cold start
  // then books one bound too many, never one too few).
  if (g_flag_nvs || get_flag()) g_flag_nvs = !put_flag(false);
  return true;
}

}  // namespace

uint32_t duty_now_ms() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint32_t)((uint64_t)tv.tv_sec * 1000u + tv.tv_usec / 1000);
}

DutySnapshot g_boot_snap{};
uint32_t g_boot_ms = 0, g_boot_stamp = 0;
bool g_reage_pending = false;

void duty_begin() {
  if (g_begun) return;
  g_begun = true;
  const uint32_t now = duty_now_ms();
  const bool flag = get_flag();
  g_flag_nvs = flag;
  DutySnapshot saved{};
  const bool have_saved = store_get_blob(kBlob, &saved, sizeof saved);
  g_saved_sum = have_saved ? duty_snapshot_sum(saved) : 0;
  if (rtc_valid(now)) {
    g_duty.restore(s_rtc.snap);
    Serial.printf("[duty] restored %lu ms used\n", (unsigned long)g_duty.usedMs(now));
    // A reset mid-session (panic, WDT) keeps RTC memory but loses the count of airtime not
    // yet in NVS. The flag says whether there is any: save it now (boot path, not a loop).
    to_rtc();
    if (flag) write_snapshot();
  } else {
    // History lost (power-on/brownout): the saved buckets keep their ages, as if the power
    // cut took no time (never an under-count, and they still roll off). The unsaved bound is
    // added only if airtime was recorded after that snapshot, so repeated power cuts never
    // stack it up. The old 60-bucket blob (previous firmware) is booked whole, just now.
    uint32_t sum = g_saved_sum;
    bool legacy = false;
    if (!have_saved) {
      LegacySnapshot old;
      if (store_get_blob(kLegacyBlob, &old, sizeof old)) {
        for (uint32_t b : old.bucket_ms) sum += b;
        const uint32_t cap = g_duty.budgetMs();
        if (sum > cap) sum = cap;
        if (sum) g_duty.precharge(sum, now);
        legacy = sum != 0;
      }
    }
    uint32_t extra = 0;
    if (legacy) {
      extra = duty_cold_precharge_ms(sum, kUnsavedBoundMs, g_duty.budgetMs());
      if (extra) g_duty.precharge(extra, now);
    } else {
      extra = duty_cold_start(g_duty, have_saved ? &saved : nullptr, flag, kUnsavedBoundMs, now);
      uint32_t st = 0;
      if (have_saved && store_get_blob(kStampKey, &st, sizeof st) && st) {   // age it once UTC is known
        g_boot_snap = saved;
        g_boot_ms = now;
        g_boot_stamp = st;
        g_reage_pending = true;
      }
    }
    Serial.printf("[duty] cold start, %lu ms saved + %lu ms unsaved bound\n",
                  (unsigned long)sum, (unsigned long)extra);
    to_rtc();
    // Make the bound known airtime (booked once, then aging like the rest) and clear the flag.
    // If this write fails the flag stays set and the next cold start books the same bound on
    // the same snapshot again: no ratchet either way.
    if (flag || legacy) {
      if (write_snapshot() && legacy) store_erase(kLegacyBlob);
    }
  }
  g_last_checkpoint = millis();
}

bool duty_allows(uint32_t airtime_ms, bool priority) {
  // NVS is failing and the unsaved airtime has reached what a cold start can account for:
  // nothing more goes out until a snapshot is written (10 % in any rolling hour is a hard rule).
  if (g_store_fail && g_unsaved_ms >= kUnsavedHardMs) return false;
  return g_duty.canSend(airtime_ms, duty_now_ms(), priority);
}

bool duty_can_send(uint32_t airtime_ms, bool priority) {
  if (!duty_allows(airtime_ms, priority)) return false;
  arm_flag();  // before the airtime exists: a power cut mid-transmit is covered too
  return true;
}

void duty_record(uint32_t airtime_ms) {
  g_duty.record(airtime_ms, duty_now_ms());
  to_rtc();
  g_dirty = true;
  arm_flag();  // normally already set by duty_can_send
  g_unsaved_ms += airtime_ms;
  if (g_unsaved_ms >= kUnsavedMaxMs) g_due = true;  // written at the next idle moment
  // No idle moment came: write it now (documented exception to the 50 ms loop rule), but
  // never retry a failing NVS on every transmit.
  if (g_unsaved_ms >= kUnsavedHardMs && (!g_store_fail || millis() - g_last_fail >= kFailRetryMs))
    write_snapshot();
}

uint32_t duty_used_ms() { return g_duty.usedMs(duty_now_ms()); }

uint16_t duty_permille() { return g_duty.usedPermille(duty_now_ms()); }

bool duty_store_failed() { return g_store_fail; }

void duty_utc_tick() {
  if (!g_reage_pending || !utc_valid()) return;
  g_reage_pending = false;
  const uint32_t now = duty_now_ms();
  const uint32_t boot_utc = utc_now() - (now - g_boot_ms) / 1000;
  if (boot_utc <= g_boot_stamp) return;                       // clock went backwards: keep it safe
  const uint32_t gap_s = boot_utc - g_boot_stamp;              // snapshot written -> this boot
  const uint32_t before = g_duty.usedMs(now);
  g_duty.reageSnapshot(g_boot_snap, g_boot_ms, gap_s > 7200 ? 7200000u : gap_s * 1000u, now);
  Serial.printf("[duty] real time known: saved airtime aged %lu min, %lu -> %lu ms used\n",
                (unsigned long)(gap_s / 60), (unsigned long)before, (unsigned long)g_duty.usedMs(now));
  write_snapshot();
}

void duty_idle() {
  if (g_store_fail && millis() - g_last_fail < kFailRetryMs) return;
  if (g_due || (g_store_fail && g_dirty)) {
    write_snapshot();
    return;
  }
  if (millis() - g_last_checkpoint < kCheckpointMs) return;
  g_last_checkpoint = millis();
  // Periodic: what was recorded, and also idle roll-off (so a long quiet session lowers the
  // saved figure, and the next cold start does not book airtime that has already aged out).
  if (g_dirty || g_duty.usedMs(duty_now_ms()) < g_saved_sum) write_snapshot();
}

void duty_checkpoint(bool force) {
  if (force) {
    if (g_dirty || g_flag_nvs || g_duty.usedMs(duty_now_ms()) != g_saved_sum) write_snapshot();
    return;
  }
  duty_idle();
}
