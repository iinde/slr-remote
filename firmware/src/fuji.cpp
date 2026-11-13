// Fujifilm X-series BLE remote (central).
//
// Ported from Furble by Guo-Rong Koh (gkoh/furble), MIT licence:
//   lib/furble/Fujifilm.cpp, FujifilmBasic.cpp, FujifilmSecure.cpp, Camera.cpp, Device.cpp
// Copyright (c) 2020 Guo-Rong Koh. Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software ... (full MIT text in Furble's LICENSE; credited in README).
//
// Furble's flows are kept step for step. Differences are structural (one worker task instead of
// Furble's Control task, one long-lived NimBLEClient) and are listed in the module report.
//
// Threading rules (NimBLE host task on core 0, worker on core 0 at lower priority, loop() on
// core 1):
//  - Every GATT operation, scan start/stop and scan-result clear runs on the worker only.
//    loop()-side calls just set flags, post a command, or kick the link (disconnect /
//    cancelConnect, which NimBLE allows from any task).
//  - The worker is woken through a binary semaphore, never its task notification: every
//    blocking NimBLE-Arduino call waits on that notification and would return early.
//  - Host-task callbacks never block (no mutex waits) so the worker cannot run while the
//    host holds a scan-result pointer.

#include "fuji.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdarg>
#include <cstring>
#include <string>

#include "fujievt.h"
#include "store.h"

FujiCam fuji;

namespace {

// ---------------------------------------------------------------- protocol constants (Furble)
constexpr uint16_t COMPANY_ID = 0x04d8;
constexpr uint8_t TYPE_TOKEN = 0x02;
constexpr size_t TOKEN_LEN = 4;   // Basic
constexpr size_t SERIAL_LEN = 5;  // Secure
constexpr size_t ADV_BASIC_LEN = 2 + 1 + TOKEN_LEN;
constexpr size_t ADV_SECURE_LEN = 2 + 1 + SERIAL_LEN;
constexpr uint16_t GEOTAG_SYNC_INTERVAL = 10;

// Fujifilm.h
const NimBLEUUID SVC_PAIR_UUID{0x91f1de68, 0xdff6, 0x466e, 0x8b65ff13b0f16fb8};
const NimBLEUUID CHR_PAIR_UUID{0xaba356eb, 0x9633, 0x4e60, 0xb73ff52516dbd671};
const NimBLEUUID CHR_IDEN_UUID{0x85b9163e, 0x62d1, 0x49ff, 0xa6f5054b4630d4a1};
const NimBLEUUID SVC_CONF_UUID{0x4c0020fe, 0xf3b6, 0x40de, 0xacc977d129067b14};
const NimBLEUUID CHR_IND1_UUID{0xa68e3f66, 0x0fcc, 0x4395, 0x8d4caa980b5877fa};
const NimBLEUUID CHR_IND2_UUID{0xbd17ba04, 0xb76b, 0x4892, 0xa545b73ba1f74dae};
const NimBLEUUID CHR_NOT1_UUID{0xf9150137, 0x5d40, 0x4801, 0xa8dcf7fc5b01da50};
const NimBLEUUID CHR_IND3_UUID{0x049ec406, 0xef75, 0x4205, 0xa39008fe209c51f0};
const NimBLEUUID CHR_SHUTTER_UUID{0x7fcf49c6, 0x4ff0, 0x4777, 0xa03d1a79166af7a8};
const NimBLEUUID GEOTAG_UPDATE{0xad06c7b7, 0xf41a, 0x46f4, 0xa29a712055319122};
const NimBLEUUID SVC_GEOTAG_UUID{0x3b46ec2b, 0x48ba, 0x41fd, 0xb1b8ed860b60d22b};
const NimBLEUUID CHR_GEOTAG_UUID{0x0f36ec14, 0x29e5, 0x411a, 0xa1b664ee8383f090};
const NimBLEUUID SVC_SHUTTER_UUID{0x6514eb81, 0x4e8f, 0x458d, 0xaa2ae691336cdfac};

// FujifilmBasic.cpp
const NimBLEUUID CR_SVC_UUID{0xaf854c2e, 0xb214, 0x458e, 0x97e2912c4ecf2cb8};
const NimBLEUUID XAPP_SVC_UUID{0x117c4142, 0xedd4, 0x4c77, 0x8696dd18eebb770a};

// FujifilmSecure.h/.cpp
const NimBLEUUID SERVICE_UUID{0xa9d2b304, 0xe8d6, 0x4902, 0x8336352b772d7597};
const NimBLEUUID PAIR_SVC_UUID{0x123d8f06, 0x62a1, 0x4935, 0x9322833c531ee225};
const NimBLEUUID STATUS_CHR_UUID{0xf557d96b, 0x8284, 0x4667, 0x8793b971c1deca2a};
const NimBLEUUID IDENT_CHR_UUID{0x85b9163e, 0x62d1, 0x49ff, 0xa6f5054b4630d4a1};
const NimBLEUUID NOT6_CHR_UUID{0xe6692c5c, 0xb7cd, 0x44f4, 0x95fceda07ce32560};
const NimBLEUUID NOTX_SVC_UUID{0x4e941240, 0xd01d, 0x46b9, 0xa5ea67636806830b};
const NimBLEUUID NOT4_CHR_UUID{0xbf6dc9cf, 0x3606, 0x4ec9, 0xa4c8d77576e93ea4};
const NimBLEUUID NOT5_CHR_UUID{0x75823784, 0xfbb7, 0x4b71, 0xabaecd9a34072e3c};
const NimBLEUUID NOT7_CHR_UUID{0xaab609c4, 0x94dd, 0x4d89, 0xbc60665d5090b828};
const NimBLEUUID NOT8_CHR_UUID{0x2a125640, 0x706d, 0x4dd1, 0xb420c0f4ab93c361};
const NimBLEUUID NOT9_CHR_UUID{0x82a9f452, 0xc5ce, 0x4ef5, 0x82033fc9a47f8171};
const NimBLEUUID NOT10_CHR_UUID{0xdeef7187, 0x3f43, 0x4364, 0x9e2211a8c8a15951};
const NimBLEUUID GEOTAG_SYNC_INTERVAL_UUID{0xc95d91ae, 0xb247, 0x4d6d, 0x86617dd5d6a0f85b};

const uint8_t SHUTTER_RELEASE[2] = {0x00, 0x00};
const uint8_t SHUTTER_CMD[2] = {0x01, 0x00};
const uint8_t SHUTTER_PRESS[2] = {0x02, 0x00};
const uint8_t SHUTTER_FOCUS[2] = {0x03, 0x00};

struct __attribute__((packed)) FujiTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
};
struct __attribute__((packed)) Geotag {  // Fujifilm.h geotag_t
  int32_t latitude;
  int32_t longitude;
  int32_t altitude;
  uint8_t pad[4];
  FujiTime gps_time;
};
static_assert(sizeof(Geotag) == 23, "Furble's packed geotag_t is 23 bytes");

// ---------------------------------------------------------------- our tuning
constexpr uint32_t PAIR_PICK_WINDOW_MS = 3000;  // strongest candidate after first match
constexpr uint32_t SECURE_SCAN_MS = 60000;      // FujifilmSecure::SCAN_TIME_MS
constexpr uint32_t CONNECT_TIMEOUT_MS = 30000;  // FurbleControl TIMEOUT_DEFAULT_MS
constexpr uint16_t SCAN_ITVL_MS = 6553;         // Furble Scan: interval == window
constexpr uint32_t BACKOFF_MS[] = {2000, 5000, 10000, 30000};
// After this many scans in a row that never saw the saved camera (about 5 min) it is taken
// to be switched off: later scans listen 10 % of the time instead of all of it, so the BLE
// receiver is not on for 40 min of every hour. A command or a connect brings full duty back.
constexpr uint8_t EASE_AFTER_MISSES = 4;
constexpr uint16_t EASY_SCAN_ITVL_MS = 3000;
constexpr uint16_t EASY_SCAN_WINDOW_MS = 300;
constexpr uint32_t CMD_WAIT_MS = 2000;  // caller's bound for CMD + action writes on the worker
constexpr uint32_t WAIT_SLICE_MS = 5;   // wait-hook cadence while a command waits
constexpr uint32_t GEO_NO_REQ_MS = 30000;  // linked this long with no location request
constexpr uint32_t GEO_REQ_STALE_MS = 25000;  // a request pending this long went unanswered
// The worker must START a command by this long after it was posted, or never: a command it
// starts then still has ~1.5 s to finish before the caller gives up, so a press can never
// land after the caller has already reported Timeout.
constexpr uint32_t CMD_START_BY_MS = 500;
constexpr uint32_t DISCONNECT_WAIT_MS = 6000;  // > supervision timeout (2 x initial = 5.12 s)
constexpr uint32_t TASK_STACK = 10240;
constexpr size_t RING = 32;
constexpr size_t MAX_CANDS = 6;
constexpr size_t NAME_LEN = 32;

enum class Flow : uint8_t { None = 0, Basic = 1, Secure = 2 };

constexpr uint8_t REC_VER = 1;
struct Rec {  // persisted as blob "fuji"
  uint8_t ver;
  uint8_t flow;
  uint8_t addr_type;
  uint8_t secret_len;
  uint64_t addr;
  uint8_t secret[8];  // Basic: 4-byte token. Secure: 5-byte serial.
  char name[NAME_LEN];
};

struct Cand {
  Rec rec;
  int8_t best_rssi;
};

struct Gps {
  double lat, lon, alt;
  int y, mo, d, h, mi, s;
  bool valid;
};

// ---------------------------------------------------------------- shared state
struct State {
  std::atomic<bool> started{false};
  std::atomic<FujiState> st{FujiState::Idle};
  std::atomic<bool> saved{false};
  std::atomic<bool> autoConnect{true};
  std::atomic<bool> pairReq{false};
  std::atomic<bool> pairCancel{false};
  std::atomic<bool> forgetReq{false};
  std::atomic<uint32_t> pairTimeout{0};
  std::atomic<bool> ready{false};
  std::atomic<bool> linkUp{false};
  std::atomic<bool> configured{false};   // NOT1 == 02 00
  std::atomic<bool> geoRequested{false}; // GEOTAG_UPDATE == 01 00
  std::atomic<bool> geotagOn{false};
  std::atomic<bool> gpsFresh{false};
  std::atomic<uint32_t> passkey{0};
  std::atomic<uint32_t> lastNotify{0};
  std::atomic<uint32_t> readyAt{0};         // millis() when ready last became true (0 = never)
  std::atomic<uint32_t> shutterAt{0};       // millis() when the last shutter write began (0 = never)
  std::atomic<uint32_t> geoReqAt{0};        // millis() when the pending geotag request came
  std::atomic<bool> forgetOk{false};        // the last forget really erased the record
  std::atomic<uint32_t> forgetGen{0};       // forget() requests made
  std::atomic<uint32_t> forgetDoneGen{0};   // the request number doForget() last completed
  void (*waitHook)() = nullptr;             // loop task only (shutterCommand)
  std::atomic<uint32_t> cmdStarted{0};      // id of the last command the worker began
  std::atomic<uint8_t> geoStatus{0};        // GeoStatus
  std::atomic<bool> geoChar{false};         // a writable geotag characteristic was found
  std::atomic<bool> scanEased{false};       // worker: the next saved-camera scan is low duty
  std::atomic<bool> fastReq{false};         // a command found no link: scan at full duty again

  SemaphoreHandle_t mtx = nullptr;   // rec, gps, name (never taken on the host task)
  SemaphoreHandle_t gatt = nullptr;  // one GATT user at a time (all on the worker)
  SemaphoreHandle_t wake = nullptr;  // binary: wakes the worker
  portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;
  portMUX_TYPE candMux = portMUX_INITIALIZER_UNLOCKED;  // cands, firstMatchMs, findSerial
  QueueHandle_t logQ = nullptr;
  QueueHandle_t cmdQ = nullptr;      // shutter commands for the worker
  QueueHandle_t resQ = nullptr;      // their results
  TaskHandle_t task = nullptr;
  uint32_t cmdId = 0;                // loop() only

  NimBLEClient* client = nullptr;
  NimBLERemoteCharacteristic* shutter = nullptr;  // valid only while ready (worker only)
  NimBLERemoteCharacteristic* geo = nullptr;      // geotag chr, may be null (worker only)
  std::string devName;

  Rec rec{};
  char name[NAME_LEN] = "";
  Gps gps{};

  Cand cands[MAX_CANDS];
  size_t nCands = 0;
  uint32_t firstMatchMs = 0;
  uint8_t findSerial[SERIAL_LEN] = {};
  std::atomic<bool> secureSeen{false};
  uint64_t seenAddr = 0;  // address the saved camera is advertising from now (candMux)
  uint8_t seenType = 0;   // the X-E5 rotates a private address, so the paired one goes stale

  FujiNotify ring[RING];
  uint8_t ringHead = 0;   // next write slot
  uint8_t ringCount = 0;
};
State g;

struct Lock {
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }
  SemaphoreHandle_t m_;
};

// ---------------------------------------------------------------- logging (never blocks callers)
struct LogLine { char s[112]; };

void flog(const char* fmt, ...) {
  if (!g.logQ) return;
  LogLine l;
  int n = snprintf(l.s, sizeof(l.s), "[fuji] ");
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(l.s + n, sizeof(l.s) - n, fmt, ap);
  va_end(ap);
  xQueueSend(g.logQ, &l, 0);  // drop if full rather than block BLE host or loop()
}

void drainLog() {
  LogLine l;
  while (xQueueReceive(g.logQ, &l, 0) == pdTRUE) {
    // Text and newline in one write: loop() on the other core may be printing a reply.
    char line[sizeof l.s + 2];
    const int n = snprintf(line, sizeof line, "%s\r\n", l.s);
    if (n > 0) Serial.write((const uint8_t*)line, (size_t)(n < (int)sizeof line ? n : (int)sizeof line - 1));
  }
}

void setState(FujiState s) { g.st.store(s); }

uint32_t uuid32(const NimBLEUUID& u) {
  const uint8_t* v = u.getValue();
  if (u.bitSize() == 128) {  // little-endian: first group lives in bytes 12..15
    return (uint32_t)v[12] | (uint32_t)v[13] << 8 | (uint32_t)v[14] << 16 | (uint32_t)v[15] << 24;
  }
  if (u.bitSize() == 32) return (uint32_t)v[0] | v[1] << 8 | v[2] << 16 | (uint32_t)v[3] << 24;
  if (u.bitSize() == 16) return (uint32_t)v[0] | v[1] << 8;
  return 0;
}

bool waitWake(uint32_t ms) { return xSemaphoreTake(g.wake, pdMS_TO_TICKS(ms)) == pdTRUE; }
void wakeWorker() {
  if (g.wake) xSemaphoreGive(g.wake);
}

struct CmdReq {
  uint32_t id;
  uint32_t deadline;  // millis(); not started by then = never sent (no late shutter press)
  uint8_t action[2];
};
struct CmdRes {
  uint32_t id;
  FujiErr err;
};

// Notifications that can follow a shutter press. GEOTAG_UPDATE and the sync-interval
// characteristic fire every ~10 s on their own, and NOT4..NOT10 are status chatter, so
// counting them would let a periodic geotag request inside the 2 s window read as CAM EVT.
// Narrow until the bench test pins down which UUID really means "exposed".
bool isShotEvidence(const NimBLEUUID& u) {
  return u == CHR_NOT1_UUID || u == CHR_IND1_UUID || u == CHR_IND2_UUID || u == CHR_IND3_UUID;
}

// ---------------------------------------------------------------- notifications (Fujifilm::notify)
void onNotify(NimBLERemoteCharacteristic* pChr, uint8_t* data, size_t len, bool isNotify) {
  uint32_t now = millis();
  const NimBLEUUID& u = pChr->getUUID();
  FujiNotify n{};
  n.at_ms = now;
  n.uuid32 = uuid32(u);
  n.len = len > 255 ? 255 : (uint8_t)len;
  memcpy(n.data, data, len < sizeof(n.data) ? len : sizeof(n.data));

  portENTER_CRITICAL(&g.ringMux);
  g.ring[g.ringHead] = n;
  g.ringHead = (g.ringHead + 1) % RING;
  if (g.ringCount < RING) g.ringCount++;
  portEXIT_CRITICAL(&g.ringMux);
  // Not evidence: NOT1 = 02 00 ("configured"), anything during the handshake, and the
  // camera's own status traffic in the first FUJI_READY_SETTLE_MS after the link is ready.
  const uint32_t readyAt = g.ready.load() ? g.readyAt.load() : 0;
  if (fuji_notify_is_shot_evidence(isShotEvidence(u), u == CHR_NOT1_UUID, data, len, now,
                                   readyAt, g.shutterAt.load()))
    g.lastNotify.store(now ? now : 1);

  if (u == CHR_NOT1_UUID) {
    if (len >= 2 && data[0] == 0x02 && data[1] == 0x00) g.configured = true;
  } else if (u == GEOTAG_UPDATE) {
    if (len >= 2 && data[0] == 0x01 && data[1] == 0x00) {
      if (!g.geoRequested.exchange(true)) g.geoReqAt = now ? now : 1;
      wakeWorker();
    }
  }
  flog("%s %08lx len=%u %02x %02x", isNotify ? "NOT" : "IND", (unsigned long)n.uuid32,
       (unsigned)len, len > 0 ? data[0] : 0, len > 1 ? data[1] : 0);
}

// Fujifilm::subscribe
bool subscribe(const NimBLEUUID& svc, const NimBLEUUID& chr, bool notification) {
  NimBLERemoteService* pSvc = g.client->getService(svc);
  if (!pSvc) return false;
  NimBLERemoteCharacteristic* pChr = pSvc->getCharacteristic(chr);
  if (!pChr) return false;
  return pChr->subscribe(notification, onNotify, true);
}

// ---------------------------------------------------------------- client callbacks (Camera.cpp)
void applyTxPower() {
  NimBLEDevice::setPowerLevel(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
  NimBLEDevice::setPowerLevel(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
  NimBLEDevice::setPowerLevel(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_SCAN);
}

class ClientCb : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient*) override {
    applyTxPower();  // Furble re-applies TX power once connected
    g.linkUp = true;
  }
  void onDisconnect(NimBLEClient*, int reason) override {
    g.linkUp = false;
    g.ready = false;
    g.configured = false;
    g.geoRequested = false;
    flog("disconnected reason=%d", reason);
    wakeWorker();
  }
  void onConfirmPasskey(NimBLEConnInfo& info, uint32_t pin) override {
    // Furble leaves NimBLE's default (accept). We accept too, but keep the value for the UI.
    g.passkey = pin;
    NimBLEDevice::injectConfirmPasskey(info, true);
  }
  uint32_t onPassKeyDisplay(NimBLEConnInfo&) override {
    uint32_t pk = NimBLEDevice::getSecurityPasskey();
    g.passkey = pk;
    return pk;
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    flog("auth enc=%d bonded=%d auth=%d", info.isEncrypted(), info.isBonded(),
         info.isAuthenticated());
  }
};
ClientCb clientCb;

// ---------------------------------------------------------------- scanning
enum class ScanMode : uint8_t { Pair, FindSecure };

bool isFuji(const NimBLEAdvertisedDevice* d, std::string& mfr) {
  if (!d->haveManufacturerData()) return false;
  mfr = d->getManufacturerData();
  if (mfr.size() < 3) return false;
  uint16_t cid = (uint8_t)mfr[0] | (uint16_t)(uint8_t)mfr[1] << 8;
  return cid == COMPANY_ID;
}

// FujifilmBasic::matches / FujifilmSecure::matches. Fills rec on match.
Flow classify(const NimBLEAdvertisedDevice* d, const std::string& mfr, Rec& rec) {
  Flow f = Flow::None;
  if (mfr.size() == ADV_BASIC_LEN && (uint8_t)mfr[2] == TYPE_TOKEN &&
      (d->isAdvertisingService(CR_SVC_UUID) || d->isAdvertisingService(XAPP_SVC_UUID))) {
    f = Flow::Basic;
    rec.secret_len = TOKEN_LEN;
  } else if (mfr.size() == ADV_SECURE_LEN && d->isAdvertisingService(SERVICE_UUID)) {
    f = Flow::Secure;
    rec.secret_len = SERIAL_LEN;
  }
  if (f == Flow::None) return f;
  rec.ver = REC_VER;
  rec.flow = (uint8_t)f;
  const NimBLEAddress& a = d->getAddress();
  rec.addr = (uint64_t)a;
  rec.addr_type = a.getType();
  memset(rec.secret, 0, sizeof(rec.secret));
  memcpy(rec.secret, mfr.data() + 3, rec.secret_len);
  strncpy(rec.name, d->getName().c_str(), NAME_LEN - 1);
  rec.name[NAME_LEN - 1] = '\0';
  return f;
}

// Host task: copies only, under a spinlock, never blocks.
void addCandidate(const Rec& r, int8_t rssi) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&g.candMux);
  bool found = false;
  for (size_t i = 0; i < g.nCands && !found; i++) {
    if (g.cands[i].rec.addr != r.addr) continue;
    found = true;
    if (rssi > g.cands[i].best_rssi) g.cands[i].best_rssi = rssi;
    if (r.name[0]) memcpy(g.cands[i].rec.name, r.name, NAME_LEN);
  }
  if (!found && g.nCands < MAX_CANDS) {
    if (g.nCands == 0) g.firstMatchMs = now;
    g.cands[g.nCands].rec = r;
    g.cands[g.nCands].best_rssi = rssi;
    g.nCands++;
  }
  portEXIT_CRITICAL(&g.candMux);
}

class ScanCb : public NimBLEScanCallbacks {
 public:
  ScanMode mode = ScanMode::Pair;
  void onResult(const NimBLEAdvertisedDevice* d) override {
    std::string mfr;
    if (!isFuji(d, mfr)) return;
    if (mode == ScanMode::Pair) {
      Rec r{};
      if (classify(d, mfr, r) != Flow::None) addCandidate(r, d->getRSSI());
      return;
    }
    // FujifilmSecure::onResult: saved camera ready when it advertises PAIR_SVC + our serial.
    if (mfr.size() != ADV_SECURE_LEN || !d->isAdvertisingService(PAIR_SVC_UUID)) return;
    portENTER_CRITICAL(&g.candMux);
    const bool match = memcmp(mfr.data() + 3, g.findSerial, SERIAL_LEN) == 0;
    if (match && !g.secureSeen) {
      const NimBLEAddress& a = d->getAddress();
      g.seenAddr = (uint64_t)a;
      g.seenType = a.getType();
    }
    portEXIT_CRITICAL(&g.candMux);
    if (match) g.secureSeen = true;
  }
};
ScanCb scanCb;

// Worker only. maxResults stays at NimBLE's default (0xFF) as in Furble: with 0, stop()
// frees every NimBLEAdvertisedDevice, possibly under a host-task callback. Results are
// cleared here instead, once the scan is over.
void clearScanResults() {
  vTaskDelay(pdMS_TO_TICKS(20));  // let the host task finish any queued scan event
  NimBLEDevice::getScan()->clearResults();
}

void startScan(ScanMode mode, uint32_t duration_ms, bool dups, bool eased = false) {
  NimBLEScan* s = NimBLEDevice::getScan();
  if (!s->isScanning()) s->clearResults();
  scanCb.mode = mode;
  s->setActiveScan(true);
  // eased: the camera has been away a while, so listen in short windows to save battery.
  s->setInterval(eased ? EASY_SCAN_ITVL_MS : SCAN_ITVL_MS);
  s->setWindow(eased ? EASY_SCAN_WINDOW_MS : SCAN_ITVL_MS);
  s->setScanCallbacks(&scanCb, dups);
  s->start(duration_ms, false, true);
}

void stopScan() {  // worker only
  NimBLEScan* s = NimBLEDevice::getScan();
  if (s->isScanning()) s->stop();
  clearScanResults();
}

// Any task: interrupt a connect / bond / GATT procedure the worker is blocked in. Scans are
// not touched here; the worker's scan loops poll the abort flags every 50 ms and stop them.
void kickLink() {
  if (!g.started) return;
  if (g.client->isConnected()) {
    g.client->disconnect();
  } else if (g.st.load() == FujiState::Connecting) {
    g.client->cancelConnect();
  }
}

// Worker only.
void abortLink() {
  stopScan();
  kickLink();
}

// ---------------------------------------------------------------- connect flows
void applySecurity() {  // Camera::connect
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
}

bool connectTo(const Rec& r) {
  setState(FujiState::Connecting);
  NimBLEAddress addr(r.addr, r.addr_type);
  flog("connecting %s", addr.toString().c_str());
  return g.client->connect(addr, true, false, true);
}

bool writeChr(NimBLERemoteService* svc, const NimBLEUUID& chr, const uint8_t* d, size_t n) {
  if (!svc) return false;
  NimBLERemoteCharacteristic* c = svc->getCharacteristic(chr);
  if (!c || !c->canWrite()) return false;
  return c->writeValue(d, n, true);
}

FujiErr fail(FujiErr e, const char* step) {
  flog("handshake failed at %s (err %d)", step, (int)e);
  return e;
}

// Discovered during the handshake (lock already held) so a geotag write never pays for
// service discovery while a shutter command waits. Optional: geotag just stays off.
void discoverGeotag() {
  NimBLERemoteService* svc = g.client->getService(SVC_GEOTAG_UUID);
  NimBLERemoteCharacteristic* c = svc ? svc->getCharacteristic(CHR_GEOTAG_UUID) : nullptr;
  g.geo = (c && c->canWrite()) ? c : nullptr;
  g.geoChar = g.geo != nullptr;
  g.geoStatus = (uint8_t)GeoStatus::Waiting;
  if (!g.geo) flog("no geotag characteristic");
}

// FujifilmBasic::_connect
FujiErr basicConnect(const Rec& r) {
  if (!connectTo(r)) return fail(FujiErr::NotFound, "connect");
  setState(FujiState::Handshake);
  NimBLERemoteService* pair = g.client->getService(SVC_PAIR_UUID);
  if (!pair) return fail(FujiErr::HandshakeFailed, "pair svc");
  if (!writeChr(pair, CHR_PAIR_UUID, r.secret, TOKEN_LEN))
    return fail(FujiErr::HandshakeFailed, "token");
  if (!writeChr(pair, CHR_IDEN_UUID, (const uint8_t*)g.devName.data(), g.devName.size()))
    return fail(FujiErr::HandshakeFailed, "ident");
  if (!subscribe(SVC_CONF_UUID, CHR_IND1_UUID, false)) return fail(FujiErr::HandshakeFailed, "IND1");
  if (!subscribe(SVC_CONF_UUID, CHR_IND2_UUID, false)) return fail(FujiErr::HandshakeFailed, "IND2");
  if (!subscribe(SVC_CONF_UUID, CHR_NOT1_UUID, true)) return fail(FujiErr::HandshakeFailed, "NOT1");
  if (!subscribe(SVC_CONF_UUID, GEOTAG_UPDATE, true)) return fail(FujiErr::HandshakeFailed, "GEO");
  if (!subscribe(SVC_CONF_UUID, CHR_IND3_UUID, false)) return fail(FujiErr::HandshakeFailed, "IND3");
  NimBLERemoteService* sh = g.client->getService(SVC_SHUTTER_UUID);
  g.shutter = sh ? sh->getCharacteristic(CHR_SHUTTER_UUID) : nullptr;
  if (!g.shutter) return fail(FujiErr::HandshakeFailed, "shutter");
  discoverGeotag();
  return FujiErr::None;
}

// FujifilmSecure::_connect scan step: wait for the saved camera to advertise PAIR_SVC + serial.
FujiErr secureFindSaved(const Rec& r, bool (*aborted)()) {
  setState(FujiState::Scanning);
  g.secureSeen = false;
  portENTER_CRITICAL(&g.candMux);
  memcpy(g.findSerial, r.secret, SERIAL_LEN);
  portEXIT_CRITICAL(&g.candMux);
  // Duplicates allowed: the controller's filter would drop the reconnect (PAIR_SVC) advert if
  // any other advert from the same address came first. onResult rejects non-Fuji cheaply.
  // An eased scan ends early when a command asks for the camera: the worker then starts
  // again at full duty.
  const bool eased = g.scanEased.load();
  startScan(ScanMode::FindSecure, SECURE_SCAN_MS, true, eased);
  while (NimBLEDevice::getScan()->isScanning() && !g.secureSeen && !aborted() &&
         !(eased && g.fastReq.load())) {
    drainLog();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  stopScan();
  if (!g.secureSeen) return fail(FujiErr::NotFound, "scan for saved camera");
  return FujiErr::None;
}

struct Sub {
  const char* name;
  const NimBLEUUID& svc;
  const NimBLEUUID& chr;
  bool notification;
};

FujiErr secureConnect(const Rec& r, bool saved, bool (*aborted)()) {
  if (saved) {
    FujiErr e = secureFindSaved(r, aborted);
    if (e != FujiErr::None) return e;
  }
  if (aborted()) return FujiErr::NotReady;
  Rec target = r;
  if (saved) {  // connect where the camera is advertising now, not where it was at pairing
    portENTER_CRITICAL(&g.candMux);
    target.addr = g.seenAddr;
    target.addr_type = g.seenType;
    portEXIT_CRITICAL(&g.candMux);
    if (target.addr != r.addr) flog("camera moved address since pairing; using the advertised one");
  }
  if (!connectTo(target)) return fail(FujiErr::NotFound, "connect");

  setState(FujiState::Securing);
  if (!g.client->secureConnection()) return fail(FujiErr::SecureFailed, "secureConnection");

  setState(FujiState::Handshake);
  NimBLEAttValue status = g.client->getValue(PAIR_SVC_UUID, STATUS_CHR_UUID);
  if (status.size() != 4) return fail(FujiErr::HandshakeFailed, "status read");
  const uint8_t ackb[4] = {status[0], status[1], status[2], 0x20};
  if (!g.client->setValue(PAIR_SVC_UUID, STATUS_CHR_UUID, NimBLEAttValue(ackb, 4), true))
    return fail(FujiErr::HandshakeFailed, "status ack");

  NimBLEAttValue name((const uint8_t*)g.devName.data(), (uint16_t)g.devName.size());
  if (!g.client->setValue(PAIR_SVC_UUID, IDENT_CHR_UUID, name, true))
    return fail(FujiErr::HandshakeFailed, "ident");

  const Sub subs0[] = {
      {"indication 1", SVC_CONF_UUID, CHR_IND1_UUID, false},
      {"indication 2", SVC_CONF_UUID, CHR_IND2_UUID, false},
      {"notification 1", SVC_CONF_UUID, CHR_NOT1_UUID, true},
      {"notification 2", SVC_CONF_UUID, GEOTAG_UPDATE, true},
      {"notification 4", NOTX_SVC_UUID, NOT4_CHR_UUID, true},
      {"notification 5", NOTX_SVC_UUID, NOT5_CHR_UUID, true},
  };
  for (const Sub& s : subs0) {
    if (!subscribe(s.svc, s.chr, s.notification)) return fail(FujiErr::HandshakeFailed, s.name);
  }
  const Sub subs1[] = {
      {"notification 6", SVC_CONF_UUID, NOT6_CHR_UUID, true},
      {"notification 7", NOTX_SVC_UUID, NOT7_CHR_UUID, true},
      {"notification 8", NOTX_SVC_UUID, NOT8_CHR_UUID, true},
      {"notification 9", NOTX_SVC_UUID, NOT9_CHR_UUID, true},
      {"notification 10", NOTX_SVC_UUID, NOT10_CHR_UUID, true},
      {"notification 11", NOTX_SVC_UUID, GEOTAG_SYNC_INTERVAL_UUID, true},
  };
  for (const Sub& s : subs1) {
    if (subscribe(s.svc, s.chr, s.notification)) continue;
    flog("optional subscribe failed: %s", s.name);
    if (&s.chr == &GEOTAG_SYNC_INTERVAL_UUID) return fail(FujiErr::HandshakeFailed, s.name);
  }

  const uint8_t ivl[2] = {(uint8_t)(GEOTAG_SYNC_INTERVAL & 0xff), (uint8_t)(GEOTAG_SYNC_INTERVAL >> 8)};
  if (!g.client->setValue(NOTX_SVC_UUID, GEOTAG_SYNC_INTERVAL_UUID, NimBLEAttValue(ivl, 2), true))
    return fail(FujiErr::HandshakeFailed, "geotag sync interval");

  NimBLERemoteService* sh = g.client->getService(SVC_SHUTTER_UUID);
  if (!sh) return fail(FujiErr::HandshakeFailed, "shutter svc");
  g.shutter = sh->getCharacteristic(CHR_SHUTTER_UUID);
  if (!g.shutter) return fail(FujiErr::HandshakeFailed, "shutter chr");
  discoverGeotag();
  return FujiErr::None;
}

void markReady() {
  const uint32_t t = millis();
  g.readyAt = t ? t : 1;
  g.ready = true;
}

// One full attempt. Holds the GATT lock throughout so commands see NotReady, never a half link.
// mark_ready false (pairing): the caller marks the link ready once the record is saved, so
// nothing reports "paired" before it is.
FujiErr runConnect(const Rec& r, bool saved, bool (*aborted)(), bool mark_ready = true) {
  Lock gl(g.gatt);
  g.shutter = nullptr;
  g.geo = nullptr;
  g.geoChar = false;
  g.ready = false;
  g.configured = false;
  g.geoRequested = false;
  g.passkey = 0;
  applySecurity();
  uint32_t t0 = millis();
  FujiErr e = (Flow)r.flow == Flow::Secure ? secureConnect(r, saved, aborted) : basicConnect(r);
  if (e == FujiErr::None && !g.client->isConnected()) e = FujiErr::HandshakeFailed;
  if (e != FujiErr::None) {
    g.shutter = nullptr;
    g.geo = nullptr;
    if (g.client->isConnected()) g.client->disconnect();
    return e;
  }
  if (mark_ready) markReady();
  flog("connected (%s flow) in %lu ms", (Flow)r.flow == Flow::Secure ? "secure" : "basic",
       (unsigned long)(millis() - t0));
  return e;
}

// ---------------------------------------------------------------- persistence
// false if NVS refused the write: then nothing is marked saved.
bool saveRecord(const Rec& r) {
  if (!store_put_blob("fuji", &r, sizeof(r))) return false;
  {
    Lock l(g.mtx);
    g.rec = r;
    memcpy(g.name, r.name, NAME_LEN);
  }
  g.saved = true;
  return true;
}

void loadRecord() {
  Rec r{};
  if (store_get_blob("fuji", &r, sizeof(r)) && r.ver == REC_VER &&
      ((Flow)r.flow == Flow::Basic || (Flow)r.flow == Flow::Secure)) {
    r.name[NAME_LEN - 1] = '\0';
    Lock l(g.mtx);
    g.rec = r;
    memcpy(g.name, r.name, NAME_LEN);
    g.saved = true;
  }
}

// ---------------------------------------------------------------- worker jobs
bool pairAborted() { return g.pairCancel.load() || g.forgetReq.load(); }
bool savedAborted() { return g.pairReq.load() || g.forgetReq.load() || !g.autoConnect.load(); }

bool pickCandidate(Rec& out) {
  Cand cands[MAX_CANDS];
  size_t n;
  portENTER_CRITICAL(&g.candMux);
  n = g.nCands;
  memcpy(cands, g.cands, n * sizeof(Cand));
  portEXIT_CRITICAL(&g.candMux);
  if (n == 0) return false;
  size_t best = 0;
  for (size_t i = 1; i < n; i++) {
    if (cands[i].best_rssi > cands[best].best_rssi) best = i;
  }
  out = cands[best].rec;
  flog("pair: %u candidate(s); chose '%s' %s rssi %d (%s)", (unsigned)n, out.name,
       NimBLEAddress(out.addr, out.addr_type).toString().c_str(), cands[best].best_rssi,
       (Flow)out.flow == Flow::Secure ? "secure" : "basic");
  for (size_t i = 0; i < n; i++) {
    if (i == best) continue;
    flog("pair: passed over '%s' rssi %d", cands[i].rec.name, cands[i].best_rssi);
  }
  return true;
}

// Scan until first match + PAIR_PICK_WINDOW_MS (or timeout). Returns false if nothing usable.
bool pairScan(uint32_t timeout_ms, Rec& chosen) {
  portENTER_CRITICAL(&g.candMux);
  g.nCands = 0;
  g.firstMatchMs = 0;
  portEXIT_CRITICAL(&g.candMux);
  setState(FujiState::PairScan);
  NimBLEDevice::getServer()->start();  // as Furble's Scan::start(callback)
  startScan(ScanMode::Pair, timeout_ms, true);
  while (NimBLEDevice::getScan()->isScanning() && !pairAborted()) {
    drainLog();
    portENTER_CRITICAL(&g.candMux);
    const uint32_t first = g.nCands ? g.firstMatchMs : 0;
    portEXIT_CRITICAL(&g.candMux);
    if (first && millis() - first >= PAIR_PICK_WINDOW_MS) break;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  stopScan();
  if (pairAborted()) return false;
  return pickCandidate(chosen);
}

void doPairing() {
  g.pairReq = false;
  g.pairCancel = false;
  abortLink();
  Rec chosen{};
  if (!pairScan(g.pairTimeout.load(), chosen)) {
    flog(pairAborted() ? "pair: cancelled" : "pair: no camera found");
    setState(pairAborted() ? FujiState::Idle : FujiState::Failed);
    return;
  }
  // Fresh pairing: drop any stale bond we hold for this camera (camera side was reset to pair).
  NimBLEAddress addr(chosen.addr, chosen.addr_type);
  if (NimBLEDevice::isBonded(addr)) NimBLEDevice::deleteBond(addr);
  FujiErr e = runConnect(chosen, false, pairAborted, false);
  if (e != FujiErr::None) {
    setState(pairAborted() ? FujiState::Idle : FujiState::Failed);
    return;
  }
  if (!saveRecord(chosen)) {
    // Never report "paired" for a record the next boot cannot find: drop the link and the
    // new bond (nothing points to it), and fail visibly.
    flog("pair: save failed");
    {
      Lock gl(g.gatt);
      g.shutter = nullptr;
      g.geo = nullptr;
      if (g.client->isConnected()) g.client->disconnect();
    }
    if (NimBLEDevice::isBonded(addr)) NimBLEDevice::deleteBond(addr);
    setState(FujiState::Failed);
    return;
  }
  markReady();
  setState(FujiState::Ready);
  flog("pair: saved '%s'", chosen.name);
}

void doForget() {
  g.forgetReq = false;
  const uint32_t gen = g.forgetGen.load();  // every request up to this one is served here
  Rec r;
  bool had;
  {
    Lock l(g.mtx);
    r = g.rec;
    had = g.rec.ver == REC_VER;
    memset(&g.rec, 0, sizeof(g.rec));
    g.name[0] = '\0';
  }
  {
    Lock gl(g.gatt);  // forget() already kicked the link; wait for any user to let go
    g.shutter = nullptr;
    g.geo = nullptr;
    g.ready = false;
    if (g.client->isConnected()) g.client->disconnect();
  }
  if (had) NimBLEDevice::deleteBond(NimBLEAddress(r.addr, r.addr_type));
  const bool erased = store_erase("fuji");
  g.forgetOk = erased;
  g.saved = false;
  setState(FujiState::Idle);
  g.forgetDoneGen = gen;  // last: forgetAndWait() reads forgetOk only after this
  flog(erased ? "forgot camera" : "forget: record erase FAILED");
}

// Geotag (Fujifilm::updateGeoData/sendGeoData): only when enabled, requested and fix valid.
void serviceGeotag() {
  if (!g.geotagOn || !g.geoRequested || !g.gpsFresh) return;
  Gps p;
  {
    Lock l(g.mtx);
    p = g.gps;
  }
  g.gpsFresh = false;
  if (!g.geo) return;
  if (!p.valid) {
    // The camera is asking and we have nothing to give: its photos go out untagged (or with
    // its stale last location). Say so instead of leaving "last write ok" up.
    g.geoStatus = (uint8_t)GeoStatus::NoFix;
    return;
  }
  // Shutter commands go first: never start a geotag write while one is waiting.
  if (uxQueueMessagesWaiting(g.cmdQ) > 0) {
    g.gpsFresh = true;
    return;
  }
  if (xSemaphoreTake(g.gatt, 0) != pdTRUE) {
    g.gpsFresh = true;
    return;
  }
  bool ok = false;
  if (g.ready && g.geo) {
    Geotag t{};
    t.latitude = (int32_t)(p.lat * 10000000);
    t.longitude = (int32_t)(p.lon * 10000000);
    t.altitude = (int32_t)p.alt;
    t.gps_time = {(uint16_t)p.y, (uint8_t)p.mo, (uint8_t)p.d, (uint8_t)p.h, (uint8_t)p.mi,
                  (uint8_t)p.s};
    ok = g.geo->writeValue((const uint8_t*)&t, sizeof(t), true);
  }
  xSemaphoreGive(g.gatt);
  if (ok) g.geoRequested = false;
  g.geoStatus = (uint8_t)(ok ? GeoStatus::LastOk : GeoStatus::LastFail);
  flog("geotag write %s", ok ? "ok" : "failed");
}

void disconnectQuietly() {
  Lock gl(g.gatt);
  g.shutter = nullptr;
  g.geo = nullptr;
  g.ready = false;
  if (g.client->isConnected()) g.client->disconnect();
}

// Fujifilm::sendShutterCommand: CMD then action, both with response. Never retried here: a
// retry could fire twice.
FujiErr execCommand(const uint8_t action[2]) {
  Lock gl(g.gatt);
  NimBLERemoteCharacteristic* c = g.shutter;
  if (!g.ready || !c || !g.client->isConnected() || !c->canWrite()) return FujiErr::NotReady;
  if (!c->writeValue(SHUTTER_CMD, sizeof(SHUTTER_CMD), true)) return FujiErr::WriteFailed;
  // The action write was sent; a missing response does not prove the camera ignored it.
  if (!c->writeValue(action, 2, true)) return FujiErr::Unsure;
  return FujiErr::None;
}

void serviceCommands() {
  CmdReq q;
  while (xQueueReceive(g.cmdQ, &q, 0) == pdTRUE) {
    CmdRes r{q.id, FujiErr::Timeout};
    if ((int32_t)(millis() - q.deadline) < 0) {
      g.cmdStarted = q.id;  // from here on the caller can no longer call it "not sent"
      const uint32_t t0 = millis();
      r.err = execCommand(q.action);
      flog("cmd %02x: %s in %lums", q.action[0], r.err == FujiErr::None ? "acked" : "FAILED",
           (unsigned long)(millis() - t0));
    } else {
      flog("cmd %02x: dropped, waited too long", q.action[0]);
    }
    xQueueSend(g.resQ, &r, 0);
  }
}

void worker(void*) {
  uint8_t backoff = 0;
  uint32_t nextTry = 0;
  bool wasReady = false;
  bool pendingDisc = false;
  uint32_t discSince = 0;
  uint8_t misses = 0;  // scans in a row that never saw the saved camera
  for (;;) {
    drainLog();
    serviceCommands();
    // A command came while scanning was eased: someone wants the camera, try hard again now.
    if (g.fastReq.exchange(false) && misses >= EASE_AFTER_MISSES) {
      misses = 0;
      backoff = 0;
      nextTry = millis();
      flog("command waiting: scanning at full duty again");
    }
    if (g.forgetReq) { doForget(); backoff = 0; misses = 0; nextTry = 0; continue; }
    if (g.pairReq) { doPairing(); backoff = 0; misses = 0; nextTry = millis(); wasReady = g.ready; continue; }

    bool linked = g.ready && g.client->isConnected();
    if (!g.autoConnect || !g.saved) {
      if (linked || g.client->isConnected()) disconnectQuietly();
      if (g.st.load() != FujiState::Failed || g.saved) setState(FujiState::Idle);
      wasReady = false;
      backoff = 0;
      misses = 0;
      nextTry = 0;
      waitWake(250);
      continue;
    }
    if (!linked) g.geoStatus = (uint8_t)GeoStatus::Off;
    if (linked) {
      wasReady = true;
      setState(FujiState::Ready);
      serviceGeotag();
      waitWake(100);
      continue;
    }
    // We asked for a disconnect (kickLink) and onDisconnect has not run yet: connect() would
    // be refused ("not disconnected"). Wait for it; onDisconnect wakes us. Not a failed try.
    if (g.linkUp && !g.client->isConnected()) {
      if (!pendingDisc) {
        pendingDisc = true;
        discSince = millis();
      }
      if (millis() - discSince < DISCONNECT_WAIT_MS) {
        waitWake(100);
        continue;
      }
      g.linkUp = false;  // no callback within the supervision timeout: assume it is down
    }
    pendingDisc = false;
    if (wasReady) {  // link just dropped: try again straight away, then back off
      wasReady = false;
      g.ready = false;
      misses = 0;  // it was here a moment ago: full-duty scans
      nextTry = millis();
      flog("link lost, reconnecting");
    }
    if ((int32_t)(millis() - nextTry) < 0) {
      waitWake(100);
      continue;
    }
    Rec r;
    {
      Lock l(g.mtx);
      r = g.rec;
    }
    g.scanEased = misses >= EASE_AFTER_MISSES;
    FujiErr e = runConnect(r, true, savedAborted);
    if (e == FujiErr::None) {
      backoff = 0;
      misses = 0;  // connected: the next loss starts with full-duty scans again
      wasReady = true;
      setState(FujiState::Ready);
      continue;
    }
    if (savedAborted()) continue;
    // An eased scan cut short by a command is not a failed try: the top of the loop
    // restarts it at full duty.
    if (g.scanEased && g.fastReq) continue;
    // Only a scan that never saw the camera counts towards easing off; a camera that is
    // there but fails the connect or handshake keeps full-duty scans.
    if ((Flow)r.flow == Flow::Secure && e == FujiErr::NotFound && !g.secureSeen) {
      if (misses < EASE_AFTER_MISSES && ++misses == EASE_AFTER_MISSES)
        flog("camera not seen in %u scans: easing off to low-duty scans", (unsigned)misses);
    } else {
      misses = 0;
    }
    uint32_t wait = BACKOFF_MS[backoff];
    if (backoff + 1 < sizeof(BACKOFF_MS) / sizeof(BACKOFF_MS[0])) backoff++;
    nextTry = millis() + wait;
    setState(FujiState::Failed);
    flog("retry in %lu ms", (unsigned long)wait);
  }
}

// ---------------------------------------------------------------- commands
// Caller side (loop task). The writes run on the worker, so loop() is bounded by CMD_WAIT_MS
// even when the camera stops answering ATT requests (whose own timeout is 30 s). On timeout
// the link is dropped so NimBLE abandons the stalled procedure; the result is Timeout.
FujiErr shutterCommand(const uint8_t action[2], const char* label) {
  if (!g.started) return FujiErr::NotReady;
  if (!g.ready) {
    // A command with no link: if reconnect scanning has eased off, bring it back to full
    // duty now (the worker ignores this when it has not eased off).
    g.fastReq = true;
    wakeWorker();
    return FujiErr::NotReady;
  }
  CmdRes r;
  while (xQueueReceive(g.resQ, &r, 0) == pdTRUE) {}  // results of commands we gave up on
  const uint32_t t0 = millis();
  CmdReq q{++g.cmdId, t0 + CMD_START_BY_MS, {action[0], action[1]}};
  if (xQueueSend(g.cmdQ, &q, 0) != pdTRUE) return FujiErr::NotReady;
  wakeWorker();
  for (;;) {
    const uint32_t spent = millis() - t0;
    // Wait in short slices so the hook (button sampling) keeps running: a quick press made
    // during this write must not be lost.
    bool got = false;
    if (spent < CMD_WAIT_MS) {
      const uint32_t slice = min<uint32_t>(WAIT_SLICE_MS, CMD_WAIT_MS - spent);
      TickType_t ticks = pdMS_TO_TICKS(slice);
      if (ticks == 0) ticks = 1;
      got = xQueueReceive(g.resQ, &r, ticks) == pdTRUE;
      if (!got) {
        if (g.waitHook) g.waitHook();
        if (millis() - t0 < CMD_WAIT_MS) continue;
      }
    }
    if (!got) {
      flog("cmd %s: no answer in %lu ms, dropping link", label, (unsigned long)CMD_WAIT_MS);
      // The link is known dead from here: mark it so the follow-up commands of this shot
      // fail at once instead of each waiting another 2 s for the disconnect event.
      g.ready = false;
      kickLink();
      // Started on the worker: its writes may have reached the camera. Not started: the
      // start-by deadline has passed, so it never will be.
      return g.cmdStarted.load() == q.id ? FujiErr::Unsure : FujiErr::Timeout;
    }
    if (r.id == q.id) return r.err;
  }
}

}  // namespace

// ---------------------------------------------------------------- public API
void FujiCam::begin(const char* devName) {
  if (g.started) return;
  g.mtx = xSemaphoreCreateMutex();
  g.gatt = xSemaphoreCreateMutex();
  g.wake = xSemaphoreCreateBinary();
  g.logQ = xQueueCreate(16, sizeof(LogLine));
  g.cmdQ = xQueueCreate(4, sizeof(CmdReq));
  g.resQ = xQueueCreate(4, sizeof(CmdRes));
  g.devName = devName ? devName : "SLR";

  // Device::init
  NimBLEDevice::init(g.devName);
  applyTxPower();
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT);
  applySecurity();
  NimBLEDevice::createServer();  // Furble's Scan creates one; started only when pairing

  // Camera::connect: one long-lived client instead of Furble's per-connect self-deleting one.
  g.client = NimBLEDevice::createClient();
  g.client->setClientCallbacks(&clientCb, false);
  g.client->setConnectTimeout(CONNECT_TIMEOUT_MS);
  // Fast link: 7.5..15 ms interval, no peripheral latency, 4 s supervision timeout (units: 1.25 ms, 10 ms).
  // The NimBLE default (30..50 ms, latency 1) made every write-with-response take about 200 ms, and a
  // shot needs two before the shutter fires. Extra idle cost is a few more radio events per second.
  g.client->setConnectionParams(6, 12, 0, 400);

  loadRecord();
  g.started = true;
  xTaskCreatePinnedToCore(worker, "fuji", TASK_STACK, nullptr, 1, &g.task, 0);
}

void FujiCam::startPairing(uint32_t timeout_ms) {
  if (!g.started) return;
  g.pairTimeout = timeout_ms ? timeout_ms : 30000;
  g.pairCancel = false;
  g.pairReq = true;
  kickLink();
  wakeWorker();
}

void FujiCam::cancelPairing() {
  if (!g.started) return;
  g.pairReq = false;
  g.pairCancel = true;  // pairScan sees it within 50 ms and stops the scan itself
  FujiState s = g.st.load();
  if (s == FujiState::Connecting || s == FujiState::Securing || s == FujiState::Handshake)
    kickLink();
  wakeWorker();
}

void FujiCam::loop(uint32_t now) {
  (void)now;  // all BLE work runs in the worker task; nothing to drive here
}

FujiState FujiCam::state() const { return g.st.load(); }

const char* FujiCam::stateText() const {
  switch (g.st.load()) {
    case FujiState::Idle: return g.saved ? "OFF" : "NO CAM";
    case FujiState::PairScan: return "PAIR";
    case FujiState::Scanning: return "SCAN";
    case FujiState::Connecting: return "CONN";
    case FujiState::Securing: return "BOND";
    case FujiState::Handshake: return "SETUP";
    case FujiState::Ready: return "READY";
    case FujiState::Failed: return "FAIL";
  }
  return "?";
}

// P1: for a caller that holds a press back until the link is up and so never reaches
// shutterCommand(): eased-off reconnect scanning goes back to full duty (ignored otherwise).
void FujiCam::wanted() {
  if (!g.started) return;
  g.fastReq = true;
  wakeWorker();
}

bool FujiCam::ready() const {
  return g.started && g.ready && g.client->isConnected();
}

bool FujiCam::hasSaved() const { return g.saved; }

const char* FujiCam::cameraName() const { return g.name; }

void FujiCam::forget() {
  if (!g.started) {
    store_erase("fuji");
    return;
  }
  g.pairReq = false;
  g.pairCancel = true;
  g.saved = false;
  g.forgetOk = false;  // before the request is raised: only its own doForget() sets it
  g.forgetGen.fetch_add(1);
  g.forgetReq = true;
  kickLink();
  wakeWorker();
}

bool FujiCam::forgetAndWait(uint32_t timeout_ms, bool wipe_all_bonds) {
  if (!g.started) return store_erase("fuji");  // no worker: erased right here (true if gone)
  forget();
  const uint32_t want = g.forgetGen.load();  // this request (or a later one) must complete
  bool done = false;
  const uint32_t t0 = millis();
  while (!done && millis() - t0 < timeout_ms) {
    done = (int32_t)(g.forgetDoneGen.load() - want) >= 0;
    if (!done) delay(10);
  }
  // "Done" only if the record really went: a failed erase would come back after a reboot.
  done = done && g.forgetOk.load();
  if (wipe_all_bonds && g.started) {
    // Backstop before a restart: whatever state the worker is in, no bond survives.
    if (g.client->isConnected()) g.client->disconnect();
    NimBLEDevice::deleteAllBonds();
    store_erase("fuji");
  }
  return done;
}

void FujiCam::shutdown(uint32_t timeout_ms) {
  setAutoConnect(false);
  if (!g.started) return;
  const uint32_t t0 = millis();
  while (g.linkUp.load() && millis() - t0 < timeout_ms) delay(5);
}

GeoStatus FujiCam::geotagStatus() const {
  if (!g.geotagOn.load() || !g.ready.load()) return GeoStatus::Off;
  if (!g.geoChar.load()) return GeoStatus::NoChar;
  const GeoStatus st = (GeoStatus)g.geoStatus.load();
  // The camera normally asks within ~10 s of connecting. Silence well past that means no
  // photo is being tagged: report it rather than sit on "waiting".
  const uint32_t ready_at = g.readyAt.load();
  if (st == GeoStatus::Waiting && ready_at && millis() - ready_at > GEO_NO_REQ_MS)
    return GeoStatus::NoReq;
  // A request left unanswered for more than ~2 sync intervals: we had nothing to send.
  const uint32_t req_at = g.geoReqAt.load();
  if (g.geoRequested.load() && req_at && millis() - req_at > GEO_REQ_STALE_MS &&
      st != GeoStatus::LastFail)
    return GeoStatus::NoFix;
  return st;
}

void FujiCam::setAutoConnect(bool on) {
  bool was = g.autoConnect.exchange(on);
  if (was && !on && g.started && g.st.load() != FujiState::PairScan) kickLink();
  wakeWorker();
}

FujiErr FujiCam::focusPress() { return shutterCommand(SHUTTER_FOCUS, "focus"); }
FujiErr FujiCam::shutterPress() {
  if (g.started && g.ready) {  // before the write: its notification can arrive during it
    const uint32_t t = millis();
    g.shutterAt = t ? t : 1;
  }
  return shutterCommand(SHUTTER_PRESS, "shutter");
}
FujiErr FujiCam::release() { return shutterCommand(SHUTTER_RELEASE, "release"); }

void FujiCam::setWaitHook(void (*hook)()) { g.waitHook = hook; }

int8_t FujiCam::rssi() {
  if (!g.started || !g.client->isConnected()) return 0;
  int r = g.client->getRssi();
  return (int8_t)(r < -127 ? -127 : (r > 0 ? 0 : r));
}

void FujiCam::setGeotag(bool on) { g.geotagOn = on; }

void FujiCam::updateGps(double lat, double lon, double alt_m, int y, int mo, int d, int h, int mi,
                        int s, bool valid) {
  if (!g.started) return;
  {
    Lock l(g.mtx);
    g.gps = {lat, lon, alt_m, y, mo, d, h, mi, s, valid && y >= 2020};
  }
  g.gpsFresh = true;
  if (g.geotagOn && g.geoRequested) wakeWorker();
}

uint32_t FujiCam::lastNotifyMs() const { return g.lastNotify.load(); }

uint8_t FujiCam::notifyCount() const { return g.ringCount; }

bool FujiCam::notifyAt(uint8_t i, FujiNotify& out) const {
  bool ok = false;
  portENTER_CRITICAL(&g.ringMux);
  if (i < g.ringCount) {
    out = g.ring[(g.ringHead + RING - 1 - i) % RING];
    ok = true;
  }
  portEXIT_CRITICAL(&g.ringMux);
  return ok;
}

uint32_t FujiCam::passkey() const { return g.passkey; }
