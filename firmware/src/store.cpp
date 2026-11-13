// Persistent settings (NVS "xe5" via Preferences) and the range log (/range.csv on LittleFS).
#include "store.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <string.h>

#include <initializer_list>

#include "rangelog_fmt.h"

namespace {

Preferences g_prefs;
bool g_prefs_open = false;

// NVS keys (max 15 chars).
constexpr const char* kRole = "role";
constexpr const char* kRegion = "region";
constexpr const char* kMode = "mode";
constexpr const char* kTwoStage = "two";
constexpr const char* kFocus = "focus";
constexpr const char* kAdaptive = "asf";
constexpr const char* kFixedSf = "fsf";
constexpr const char* kAntGain = "ant";
constexpr const char* kGeotag = "geo";
constexpr const char* kHhGps = "hhgps";
constexpr const char* kLastSf = "lsf";
constexpr const char* kPress = "press";
constexpr const char* kAfMax = "afmax";
constexpr const char* kAfLock = "aflock";
constexpr const char* kTimer = "timer";
constexpr const char* kIvEvery = "ivev";
constexpr const char* kIvCount = "ivn";
constexpr const char* kBurst = "burst";
constexpr const char* kBulb = "bulb";
constexpr const char* kHoldCap = "hcap";
constexpr const char* kModes = "modes";
constexpr const char* kWake = "wake";
constexpr const char* kRstGest = "rstg";
constexpr const char* kAux = "aux";
constexpr const char* kLed = "led";
constexpr const char* kRlog = "rlog";
constexpr const char* kLedB = "ledb";
constexpr const char* kTally = "tally";
constexpr const char* kObMode = "obmode";
// Legacy single-record pairing (read once, then superseded by the slots below).
constexpr const char* kPairValid = "pvalid";
constexpr const char* kPairKey = "pkey";
constexpr const char* kPairNet = "pnet";
constexpr const char* kPeerCtr = "pctr";
// Two pairing slots: a new pairing is written whole into the inactive slot, then the one
// byte kPairSlot is flipped as the only commit write. A failed write leaves the old slot.
constexpr const char* kPairSlot = "pslot";
constexpr const char* kPairBlob[2] = {"pair0", "pair1"};
constexpr const char* kSlotCtr[2] = {"pctr0", "pctr1"};
constexpr uint8_t kSlotLegacy = 0xFF;  // no slot written yet: the legacy keys hold the record

struct PairBlob {
  uint8_t key[32];
  uint16_t net;
};
constexpr const char* kCtrCeiling = "ctrc";

constexpr uint32_t kCtrBlock = 1024;
constexpr uint32_t kPeerCtrStride = 256;

// Own TX counter state.
bool g_ctr_loaded = false;
uint32_t g_ctr_next = 0;
uint32_t g_ctr_ceiling = 0;

// Last peer counter written to NVS.
bool g_peer_loaded = false;
uint32_t g_peer_saved = 0;
bool g_slot_loaded = false;
uint8_t g_slot = kSlotLegacy;  // active pairing slot

uint8_t active_slot();
const char* peer_ctr_key() {
  const uint8_t s = active_slot();
  return s < 2 ? kSlotCtr[s] : kPeerCtr;
}

constexpr const char* kLogPath = "/range.csv";
constexpr const char* kOldLogPath = "/range.old.csv";
constexpr size_t kLogCap = 256 * 1024;
bool g_fs_ok = false;
bool g_rows_known = false;
uint32_t g_rows = 0;

bool sf_ok(uint8_t sf) { return sf >= radiocfg::SF_MIN && sf <= radiocfg::SF_MAX; }

bool ensure_prefs() {
  if (!g_prefs_open) g_prefs_open = g_prefs.begin("xe5", false);
  return g_prefs_open;
}

uint8_t active_slot() {
  if (!g_slot_loaded && ensure_prefs()) {
    const uint8_t v = g_prefs.getUChar(kPairSlot, kSlotLegacy);
    g_slot = v < 2 ? v : kSlotLegacy;
    g_slot_loaded = true;
  }
  return g_slot;
}

bool write_log_header(File& f) {
  char hdr[256];
  size_t n = range_csv_header(hdr, sizeof(hdr));
  return n > 0 && f.write(reinterpret_cast<const uint8_t*>(hdr), n) == n;
}

void dump_abort();

bool create_log() {
  File f = LittleFS.open(kLogPath, FILE_WRITE);
  if (!f) return false;
  bool ok = write_log_header(f);
  f.close();
  g_rows = 0;
  g_rows_known = true;
  return ok;
}

uint32_t count_log_rows() {
  File f = LittleFS.open(kLogPath, FILE_READ);
  if (!f) return 0;
  uint8_t buf[256];
  uint32_t lines = 0;
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    for (size_t i = 0; i < n; ++i) lines += buf[i] == '\n';
  }
  f.close();
  return lines > 0 ? lines - 1 : 0;  // minus the header
}

}  // namespace

void store_begin() {
  if (!ensure_prefs()) Serial.println("[store] NVS open failed");
}

// Only keys whose value changed are written (store_save runs on every SF change and every
// mode-card pick; NVS wear and write time stay as before the one-button settings).
namespace {
Settings g_saved;
bool g_saved_valid = false;
// The region key is in NVS. Until it is, every save writes it: a region that was only worked
// out from the role (region_on_load) must not change when the role does, and a fresh board
// that is given a role before a region must stay Unset, not load as EU next boot.
bool g_region_stored = false;
}  // namespace
// (Declared above store_load so a load can seed it: C5.)

void store_load(Settings& s) {
  s = Settings{};
  if (!ensure_prefs()) return;
  uint8_t role = g_prefs.getUChar(kRole, static_cast<uint8_t>(Role::Unset));
  if (role <= static_cast<uint8_t>(Role::Camera)) s.role = static_cast<Role>(role);
  const bool has_region = g_prefs.isKey(kRegion);
  s.region = region_on_load(has_region, has_region ? g_prefs.getUChar(kRegion, 0) : 0,
                            s.role != Role::Unset);
  uint8_t mode = g_prefs.getUChar(kMode, static_cast<uint8_t>(Mode::Direct));
  if (mode <= static_cast<uint8_t>(Mode::Relay)) s.mode = static_cast<Mode>(mode);
  s.two_stage = g_prefs.getBool(kTwoStage, s.two_stage);
  uint16_t focus = g_prefs.getUShort(kFocus, s.focus_ms);
  if (focus <= 5000) s.focus_ms = focus;
  s.adaptive_sf = g_prefs.getBool(kAdaptive, s.adaptive_sf);
  uint8_t fsf = g_prefs.getUChar(kFixedSf, s.fixed_sf);
  if (sf_ok(fsf)) s.fixed_sf = fsf;
  int8_t ant = g_prefs.getChar(kAntGain, s.ant_gain_dbi);
  if (ant >= -10 && ant <= 20) s.ant_gain_dbi = ant;
  s.geotag = g_prefs.getBool(kGeotag, s.geotag);
  s.hh_gps = g_prefs.getBool(kHhGps, s.hh_gps);
  uint8_t lsf = g_prefs.getUChar(kLastSf, s.last_sf);
  if (sf_ok(lsf)) s.last_sf = lsf;
  // One button. Out-of-range values keep the default (the engine clamps again anyway).
  const uint8_t press = g_prefs.getUChar(kPress, s.press);
  if (press <= 3) s.press = press;
  s.af_limit_s = g_prefs.getUChar(kAfMax, s.af_limit_s);
  s.aflock_s = g_prefs.getUChar(kAfLock, s.aflock_s);
  s.timer_s = g_prefs.getUChar(kTimer, s.timer_s);
  s.iv_every_s = g_prefs.getUShort(kIvEvery, s.iv_every_s);
  s.iv_count = g_prefs.getUShort(kIvCount, s.iv_count);
  s.burst_ms = g_prefs.getUShort(kBurst, s.burst_ms);
  s.bulb_s = g_prefs.getUShort(kBulb, s.bulb_s);
  s.hold_cap_s = g_prefs.getUShort(kHoldCap, s.hold_cap_s);
  uint8_t mb[9];
  if (g_prefs.getBytesLength(kModes) == sizeof mb && g_prefs.getBytes(kModes, mb, sizeof mb) == sizeof mb &&
      mb[0] >= 1 && mb[0] <= 8) {
    s.n_modes = mb[0];
    memcpy(s.modes, mb + 1, sizeof s.modes);
  }
  s.wake_act = g_prefs.getBool(kWake, s.wake_act);
  s.rst_gest = g_prefs.getBool(kRstGest, s.rst_gest);
  const uint8_t aux = g_prefs.getUChar(kAux, s.aux_pin);
  if (aux == 0 || aux == pins::BTN_HALF) s.aux_pin = aux;
  if (s.aux_pin && s.two_stage) s.aux_pin = 0;  // both want GPIO6: the switch wins
  s.led_cues = g_prefs.getBool(kLed, s.led_cues);
  s.rlog_always = g_prefs.getBool(kRlog, s.rlog_always);
  s.led_bright = g_prefs.getUChar(kLedB, s.led_bright);
  s.tally_pin = g_prefs.getUChar(kTally, s.tally_pin);
  if (s.tally_pin != 3 && s.tally_pin != 4 && s.tally_pin != 6 && s.tally_pin != 34) s.tally_pin = 0;
  // C8: on a handheld a tally on a button pin kills the LED and the pin's wake: the button wins.
  const uint8_t tally_stored = s.tally_pin;
  if (s.role != Role::Camera && s.tally_pin &&
      (s.aux_pin == s.tally_pin || (s.two_stage && (s.tally_pin == 4 || s.tally_pin == 6 || s.tally_pin == 34))))
    s.tally_pin = 0;
  const uint8_t om = g_prefs.getUChar(kObMode, s.ob_mode);
  s.ob_mode = om == 1 ? 1 : 0;  // only SHOT and AF LOCK survive a power-on
  // C5: seed the change cache so the first save after boot writes only what changed.
  // Known side effect: a key never stored stays absent until its value changes, so such a
  // setting follows the default of whatever firmware is running (the first save used to pin it).
  g_saved = s;
  g_saved.tally_pin = tally_stored;  // a tally cleared above is written back by the next save
  g_region_stored = has_region;
  g_saved_valid = true;
}

uint8_t store_get_u8(const char* key, uint8_t def, bool* ok) {
  const bool open = ensure_prefs();
  if (ok) *ok = open;
  if (!open) return def;
  return g_prefs.getUChar(key, def);
}

bool store_put_u8(const char* key, uint8_t v) {
  if (!ensure_prefs()) return false;
  if (g_prefs.isKey(key) && g_prefs.getUChar(key, (uint8_t)~v) == v) return true;  // no wear
  return g_prefs.putUChar(key, v) == 1;
}

bool store_save(const Settings& s) {
  if (!ensure_prefs()) return false;
  bool ok = true;  // C5: every put returns 0 when the NVS write or commit failed
  const bool all = !g_saved_valid;
  const Settings& o = g_saved;
#define CH(f) (all || s.f != o.f)
  // Region goes to flash before role. A role with no region key reads as a board from before
  // regions (EU 868) on the next boot, so a save cut short or failed between the two must
  // never leave that pair behind: the role is written only once the region key is in place.
  if (region_must_write(CH(region), g_region_stored)) {
    g_region_stored = g_prefs.putUChar(kRegion, static_cast<uint8_t>(s.region)) != 0;
    ok &= g_region_stored;
  }
  if (CH(role)) {
    if (region_role_may_write(g_region_stored)) ok &= g_prefs.putUChar(kRole, static_cast<uint8_t>(s.role)) != 0;
    else ok = false;
  }
  if (CH(mode)) ok &= g_prefs.putUChar(kMode, static_cast<uint8_t>(s.mode)) != 0;
  if (CH(two_stage)) ok &= g_prefs.putBool(kTwoStage, s.two_stage) != 0;
  if (CH(focus_ms)) ok &= g_prefs.putUShort(kFocus, s.focus_ms) != 0;
  if (CH(adaptive_sf)) ok &= g_prefs.putBool(kAdaptive, s.adaptive_sf) != 0;
  if (CH(fixed_sf)) ok &= g_prefs.putUChar(kFixedSf, s.fixed_sf) != 0;
  if (CH(ant_gain_dbi)) ok &= g_prefs.putChar(kAntGain, s.ant_gain_dbi) != 0;
  if (CH(geotag)) ok &= g_prefs.putBool(kGeotag, s.geotag) != 0;
  if (CH(hh_gps)) ok &= g_prefs.putBool(kHhGps, s.hh_gps) != 0;
  if (CH(last_sf)) ok &= g_prefs.putUChar(kLastSf, s.last_sf) != 0;
  if (CH(press)) ok &= g_prefs.putUChar(kPress, s.press) != 0;
  if (CH(af_limit_s)) ok &= g_prefs.putUChar(kAfMax, s.af_limit_s) != 0;
  if (CH(aflock_s)) ok &= g_prefs.putUChar(kAfLock, s.aflock_s) != 0;
  if (CH(timer_s)) ok &= g_prefs.putUChar(kTimer, s.timer_s) != 0;
  if (CH(iv_every_s)) ok &= g_prefs.putUShort(kIvEvery, s.iv_every_s) != 0;
  if (CH(iv_count)) ok &= g_prefs.putUShort(kIvCount, s.iv_count) != 0;
  if (CH(burst_ms)) ok &= g_prefs.putUShort(kBurst, s.burst_ms) != 0;
  if (CH(bulb_s)) ok &= g_prefs.putUShort(kBulb, s.bulb_s) != 0;
  if (CH(hold_cap_s)) ok &= g_prefs.putUShort(kHoldCap, s.hold_cap_s) != 0;
  if (all || s.n_modes != o.n_modes || memcmp(s.modes, o.modes, sizeof s.modes)) {
    uint8_t mb[9];
    mb[0] = s.n_modes;
    memcpy(mb + 1, s.modes, sizeof s.modes);
    ok &= g_prefs.putBytes(kModes, mb, sizeof mb) == sizeof mb;
  }
  if (CH(wake_act)) ok &= g_prefs.putBool(kWake, s.wake_act) != 0;
  if (CH(rst_gest)) ok &= g_prefs.putBool(kRstGest, s.rst_gest) != 0;
  if (CH(aux_pin)) ok &= g_prefs.putUChar(kAux, s.aux_pin) != 0;
  if (CH(led_cues)) ok &= g_prefs.putBool(kLed, s.led_cues) != 0;
  if (CH(rlog_always)) ok &= g_prefs.putBool(kRlog, s.rlog_always) != 0;
  if (CH(led_bright)) ok &= g_prefs.putUChar(kLedB, s.led_bright) != 0;
  if (CH(tally_pin)) ok &= g_prefs.putUChar(kTally, s.tally_pin) != 0;
  if (CH(ob_mode)) ok &= g_prefs.putUChar(kObMode, s.ob_mode) != 0;
#undef CH
  // C5: cache only what is really stored. After a failed write the next save rewrites every key.
  if (ok) g_saved = s;
  g_saved_valid = ok;
  if (!ok) Serial.println("[store] settings save FAILED");
  return ok;
}

void store_load_pair(PairRecord& p) {
  p = PairRecord{};
  memset(p.key, 0, sizeof(p.key));
  if (!ensure_prefs()) return;
  const uint8_t slot = active_slot();
  bool valid = false;
  if (slot < 2) {
    PairBlob b;
    valid = g_prefs.isKey(kPairBlob[slot]) &&
            g_prefs.getBytesLength(kPairBlob[slot]) == sizeof b &&
            g_prefs.getBytes(kPairBlob[slot], &b, sizeof b) == sizeof b;
    if (valid) {
      memcpy(p.key, b.key, sizeof p.key);
      p.net = b.net;
    }
  } else {
    valid = g_prefs.getBool(kPairValid, false) && g_prefs.isKey(kPairKey) &&
            g_prefs.getBytesLength(kPairKey) == sizeof(p.key) &&
            g_prefs.getBytes(kPairKey, p.key, sizeof(p.key)) == sizeof(p.key);
    if (valid) p.net = g_prefs.getUShort(kPairNet, 0);
  }
  if (!valid) {
    memset(p.key, 0, sizeof(p.key));
    p.net = 0;
    return;
  }
  p.valid = true;
  p.peer_ctr = g_prefs.getUInt(peer_ctr_key(), 0);
  g_peer_saved = p.peer_ctr;
  g_peer_loaded = true;
}

bool store_save_pair(const PairRecord& p) {
  if (!ensure_prefs()) return false;
  // Atomic: everything goes into the slot not in use, then one byte switches to it. Until
  // that last write lands, the old pairing (if any) is untouched and still the one loaded.
  const uint8_t cur = active_slot();
  const uint8_t next = cur == 0 ? 1 : 0;
  PairBlob b{};
  memcpy(b.key, p.key, sizeof b.key);
  b.net = p.net;
  bool ok = p.valid;
  ok = ok && g_prefs.putBytes(kPairBlob[next], &b, sizeof b) == sizeof b;
  ok = ok && g_prefs.putUInt(kSlotCtr[next], p.peer_ctr) == 4;
  ok = ok && g_prefs.putUChar(kPairSlot, next) == 1;
  memset(&b, 0, sizeof b);
  if (!ok) {
    Serial.println("[store] pairing save FAILED (old pairing kept)");
    return false;
  }
  g_slot = next;
  g_slot_loaded = true;
  g_peer_saved = p.peer_ctr;
  g_peer_loaded = true;
  // Tidy up: the previous record is no longer reachable (best effort, never needed).
  if (cur < 2) {
    g_prefs.remove(kPairBlob[cur]);
    g_prefs.remove(kSlotCtr[cur]);
  } else {
    for (const char* k : {kPairValid, kPairKey, kPairNet, kPeerCtr})
      if (g_prefs.isKey(k)) g_prefs.remove(k);
  }
  return true;
}

bool store_put_peer_ctr(uint32_t ctr) {
  if (!ensure_prefs()) return false;
  if (!g_peer_loaded) {
    g_peer_saved = g_prefs.getUInt(peer_ctr_key(), 0);
    g_peer_loaded = true;
  }
  if (ctr <= g_peer_saved) return true;
  if (g_prefs.putUInt(peer_ctr_key(), ctr) != 4) return false;
  g_peer_saved = ctr;
  return true;
}

void store_save_peer_ctr(uint32_t ctr) {
  if (!ensure_prefs()) return;
  if (!g_peer_loaded) {
    g_peer_saved = g_prefs.getUInt(peer_ctr_key(), 0);
    g_peer_loaded = true;
  }
  if (ctr <= g_peer_saved || ctr - g_peer_saved < kPeerCtrStride) return;
  if (g_prefs.putUInt(peer_ctr_key(), ctr) == 4) g_peer_saved = ctr;
}

bool store_reserve_peer_ctr(uint32_t ctr) {
  if (!ensure_prefs()) return false;
  if (!g_peer_loaded) {
    g_peer_saved = g_prefs.getUInt(peer_ctr_key(), 0);
    g_peer_loaded = true;
  }
  if (ctr < g_peer_saved) return true;
  const uint32_t hwm = ctr + kPeerCtrStride;
  if (g_prefs.putUInt(peer_ctr_key(), hwm) != 4) {
    Serial.println("[store] counter reservation write FAILED");
    return false;  // g_peer_saved unchanged: the next frame tries again
  }
  g_peer_saved = hwm;
  return true;
}

void store_clear_pair() {
  if (!ensure_prefs()) return;
  // The slot byte first: with it gone nothing below is loaded, whatever else fails.
  for (const char* k : {kPairSlot, kPairBlob[0], kPairBlob[1], kSlotCtr[0], kSlotCtr[1],
                        kPairValid, kPairKey, kPairNet, kPeerCtr}) {
    if (g_prefs.isKey(k)) g_prefs.remove(k);
  }
  g_slot = kSlotLegacy;
  g_slot_loaded = true;
  g_peer_saved = 0;
  g_peer_loaded = true;
}

uint32_t store_next_ctr() {
  if (!g_ctr_loaded) {
    uint32_t ceiling = ensure_prefs() ? g_prefs.getUInt(kCtrCeiling, 0) : 0;
    // Counter 0 is never accepted by the peer (strictly greater than 0 required). The
    // ceiling is above every counter ever sent, and a peer that reserved counters (camera
    // board, store_reserve_peer_ctr) holds at most last_accepted + kPeerCtrStride: starting
    // a stride above the ceiling clears that reservation on every boot, so the first frame
    // after our reboot is never refused by a peer that rebooted too.
    g_ctr_next = ceiling == 0 ? 1 : ceiling + kPeerCtrStride;
    g_ctr_ceiling = ceiling;  // forces the block below to be persisted before first use
    g_ctr_loaded = true;
  }
  if (g_ctr_next >= g_ctr_ceiling) {
    // Persist the new ceiling before handing out any counter from the new block. If that
    // write fails, hand out nothing: after a reboot the old ceiling would be reused and
    // counters already on air would go out again under the same key.
    const uint32_t ceiling = g_ctr_next + kCtrBlock;
    if (!ensure_prefs() || g_prefs.putUInt(kCtrCeiling, ceiling) != 4) {
      Serial.println("[store] counter ceiling write FAILED");
      return 0;
    }
    g_ctr_ceiling = ceiling;
  }
  return g_ctr_next++;
}

void store_skip_ctr(uint32_t n) {
  // Load the block state. The next store_next_ctr() persists a new ceiling if the jump
  // crosses it, and hands out nothing (0) if that write fails.
  if (!g_ctr_loaded) store_next_ctr();
  g_ctr_next += n;
}

bool store_get_blob(const char* key, void* buf, size_t len) {
  if (!ensure_prefs() || key == nullptr || buf == nullptr) return false;
  if (!g_prefs.isKey(key) || g_prefs.getBytesLength(key) != len) return false;
  return g_prefs.getBytes(key, buf, len) == len;
}

bool store_put_blob(const char* key, const void* buf, size_t len) {
  if (!ensure_prefs() || key == nullptr || buf == nullptr) return false;
  return g_prefs.putBytes(key, buf, len) == len;
}

bool store_erase(const char* key) {
  if (!ensure_prefs() || key == nullptr) return false;
  if (!g_prefs.isKey(key)) return true;
  return g_prefs.remove(key);
}

void store_factory_reset() {
  if (ensure_prefs()) g_prefs.clear();
  g_saved_valid = false;
  g_region_stored = false;
  // Pairing is gone too, so restarting the own counter is safe.
  g_ctr_loaded = false;
  g_peer_saved = 0;
  g_peer_loaded = true;
  g_slot = kSlotLegacy;
  g_slot_loaded = true;
  dump_abort();  // never unlink or truncate a file the dump still has open
  if (g_fs_ok) {
    LittleFS.remove(kLogPath);
    LittleFS.remove(kOldLogPath);
    create_log();
  }
}

bool rlog_begin() {
  g_fs_ok = LittleFS.begin(true);  // formats on mount failure
  if (!g_fs_ok) {
    Serial.println("[rlog] LittleFS mount failed");
    return false;
  }
  if (!LittleFS.exists(kLogPath)) return create_log();
  File f = LittleFS.open(kLogPath, FILE_READ);
  size_t size = f ? f.size() : 0;
  if (f) f.close();
  if (size == 0) return create_log();
  g_rows_known = false;  // counted lazily (rlog_rows), never in the boot/wake path
  return true;
}

bool rlog_append(const char* line) {
  if (!g_fs_ok || line == nullptr) return false;
  size_t n = strlen(line);
  if (n == 0) return false;
  File f = LittleFS.open(kLogPath, FILE_APPEND);
  if (!f) return false;
  size_t w = f.write(reinterpret_cast<const uint8_t*>(line), n);
  const size_t size = f.size();
  f.close();
  if (w != n) return false;
  if (line[n - 1] == '\n' && g_rows_known) ++g_rows;
  // Rotate: keep the previous 256 KB as /range.old.csv. Not while a dump is reading (a rename
  // over an open file is unsafe in littlefs): the next append after it ends rotates.
  if (size > kLogCap && !rlog_dump_active()) {
    if (LittleFS.exists(kOldLogPath) && !LittleFS.remove(kOldLogPath)) {
      Serial.println("[rlog] rotate: old log not removed, still appending");
      return true;  // the row is written; the live log just grows past the cap for now
    }
    if (!LittleFS.rename(kLogPath, kOldLogPath)) {
      // Never truncate the only copy: keep appending to it, and say so.
      Serial.println("[rlog] rotate: rename FAILED, still appending");
      return false;
    }
    if (!create_log()) return false;
  }
  return true;
}

uint32_t rlog_rows() {
  if (!g_rows_known && g_fs_ok) {
    g_rows = count_log_rows();
    g_rows_known = true;
  }
  return g_rows;
}

bool rlog_rows_known() { return g_rows_known; }

size_t rlog_size() {
  if (!g_fs_ok) return 0;
  File f = LittleFS.open(kLogPath, FILE_READ);
  const size_t n = f ? f.size() : 0;
  if (f) f.close();
  return n;
}

// Incremental dump state: the rotated part first (it has its own header), then the live log.
static File g_dump;
static uint8_t g_dump_part = 0;  // 0 idle, 1 old file, 2 live file
static uint32_t g_dump_progress_ms = 0;

static bool dump_open_next() {
  while (g_dump_part && g_dump_part <= 2) {
    const char* path = g_dump_part == 1 ? kOldLogPath : kLogPath;
    if (LittleFS.exists(path)) {
      g_dump = LittleFS.open(path, FILE_READ);
      if (g_dump) return true;
    }
    if (g_dump_part == 2) Serial.println("[rlog] no log");
    g_dump_part++;
  }
  g_dump_part = 0;
  return false;
}

bool rlog_dump_start() {
  if (!g_fs_ok) {
    Serial.println("[rlog] no filesystem");
    return false;
  }
  if (g_dump) g_dump.close();
  g_dump_part = 1;
  g_dump_progress_ms = millis();
  return dump_open_next();
}

bool rlog_dump_step(size_t max_bytes) {
  if (!g_dump_part) return false;
  // Only what the USB/serial buffer takes now: never block loop() on a slow host.
  size_t room = (size_t)Serial.availableForWrite();
  if (room > max_bytes) room = max_bytes;
  if (room == 0) {
    if (millis() - g_dump_progress_ms > 10000) {  // host stopped reading: give up
      if (g_dump) g_dump.close();
      g_dump_part = 0;
      return false;
    }
    return true;
  }
  g_dump_progress_ms = millis();
  uint8_t buf[256];
  while (room > 0) {
    if (!g_dump || !g_dump.available()) {
      if (g_dump) g_dump.close();
      g_dump_part++;
      if (g_dump_part > 2 || !dump_open_next()) {
        g_dump_part = 0;
        return false;
      }
      continue;
    }
    const size_t want = room < sizeof buf ? room : sizeof buf;
    const size_t n = g_dump.read(buf, want);
    if (n == 0) {
      g_dump.close();
      continue;
    }
    Serial.write(buf, n);
    room -= n;
  }
  return true;
}

bool rlog_dump_active() { return g_dump_part != 0; }

namespace {
void dump_abort() {
  if (g_dump) g_dump.close();
  if (g_dump_part) Serial.println("\n[rlog] dump stopped: log cleared");
  g_dump_part = 0;
}
}  // namespace

void rlog_clear() {
  if (!g_fs_ok) return;
  dump_abort();  // close the reader before unlinking (an open file cannot be removed)
  LittleFS.remove(kLogPath);
  LittleFS.remove(kOldLogPath);
  create_log();
}
