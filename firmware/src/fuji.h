// Fujifilm X-series BLE remote (central), ported from gkoh/furble (MIT):
// lib/furble/Fujifilm*.cpp. Supports both the Basic (token) and Secure
// (bonded, firmware >= ~Jul 2025) flows, chosen from the advertisement.
#pragma once
#include <stdint.h>

enum class FujiState : uint8_t {
  Idle,        // BLE up, nothing saved, not pairing
  PairScan,    // scanning for a camera in Pairing Registration
  Scanning,    // looking for the saved camera
  Connecting,
  Securing,    // bonding / encryption
  Handshake,   // token/status/ident/subscriptions
  Ready,
  Failed,      // last attempt failed; will retry after backoff
};

enum class FujiErr : uint8_t {
  None = 0, NotReady = 1, WriteFailed = 2, NotFound = 3, SecureFailed = 4,
  HandshakeFailed = 5, Timeout = 6,
  // The action write (02 00 etc.) was started but never confirmed: the camera may have acted.
  // Never reported as a plain failure (the UI shows it as an unknown outcome).
  Unsure = 7,
};

// Geotag health, for the UI (failures must be visible, not only on Serial).
enum class GeoStatus : uint8_t {
  Off,       // geotag disabled, or no camera link
  NoChar,    // linked, but the camera has no writable geotag characteristic
  Waiting,   // enabled and linked, no write yet
  LastOk,    // the last location write was acknowledged
  LastFail,  // the last location write failed
  NoFix,     // the camera asked for a location and we have no valid fix to give it
  NoReq,     // linked for a while (> ~30 s) and the camera has never asked for a location
};

struct FujiNotify {      // ring-buffered log of every notification/indication
  uint32_t at_ms;
  uint32_t uuid32;       // first 4 bytes of the characteristic UUID (e.g. 0xf9150137)
  uint8_t len;           // full length
  uint8_t data[12];      // first 12 bytes
};

class FujiCam {
 public:
  // devName: our BLE identity sent to the camera ("XE5-xxxxx" from the chip MAC).
  void begin(const char* devName);
  // Start pairing: scan up to timeout for a Fujifilm advert (company 0x04D8), take the
  // strongest, connect + full handshake, persist record (store_put_blob "fuji").
  void startPairing(uint32_t timeout_ms);
  void cancelPairing();
  // Drives scanning/connect/reconnect with backoff (2 s, 5 s, 10 s, then 30 s cap).
  // After about 5 min of scans that never see the saved camera the scans drop to low duty
  // (battery); a command (focusPress etc.) with no link, wanted() or a connect restores
  // full duty.
  // Non-blocking from the caller's view: connection work runs in its own FreeRTOS task.
  void loop(uint32_t now);
  FujiState state() const;
  const char* stateText() const;    // short UI text, e.g. "READY", "SCAN", "BOND"
  bool ready() const;
  // The user wants the camera now (a press is waiting for the link): scan at full duty again.
  void wanted();
  bool hasSaved() const;
  const char* cameraName() const;   // advertised name or ""
  void forget();                    // disconnect, delete bond + record (async, on the worker)
  // forget(), then waits (<= timeout_ms) until the worker has deleted bond + record. Before a
  // restart it also deletes every NimBLE bond, whatever the worker managed. true if done.
  bool forgetAndWait(uint32_t timeout_ms, bool wipe_all_bonds);
  void setAutoConnect(bool on);     // false = stay disconnected (handheld asleep / camera off)
  // setAutoConnect(false), then waits (<= timeout_ms) for the link to be really down, so the
  // camera sees the disconnect before we deep-sleep (it then advertises for reconnect).
  void shutdown(uint32_t timeout_ms);
  GeoStatus geotagStatus() const;
  // Commands: each does write(01 00) then write(action) with response, like Furble, on the
  // BLE worker task. The caller blocks until both writes are acked, at most ~2 s (calling the
  // wait hook about every 5 ms meanwhile); then the link is dropped so the stalled ATT
  // procedure is abandoned, and the result is FujiErr::Timeout if the worker never started
  // the writes (nothing reached the camera) or FujiErr::Unsure if it did (the camera may
  // have acted: never show that as a plain failure). FujiErr::None if both writes acked.
  FujiErr focusPress();
  FujiErr shutterPress();
  FujiErr release();
  // Called about every 5 ms while a command above waits (e.g. to sample buttons). Runs on the
  // caller's task; must not call FujiCam commands. nullptr disables.
  void setWaitHook(void (*hook)());
  int8_t rssi();                    // 0 if not connected
  // Geotag: when enabled and the camera requested location (notify ad06c7b7 = 01 00),
  // the next update with valid fix+time is written to 0f36ec14 (24-byte struct).
  void setGeotag(bool on);
  void updateGps(double lat, double lon, double alt_m, int y, int mo, int d, int h, int mi,
                 int s, bool valid);
  // millis() of the most recent notification that can follow a shot (NOT1, IND1..IND3), 0 if
  // none. Periodic housekeeping (geotag requests, sync interval, NOT4..NOT10 status) is
  // logged in the ring below but never counts here.
  uint32_t lastNotifyMs() const;
  uint8_t notifyCount() const;
  bool notifyAt(uint8_t i, FujiNotify& out) const;  // 0 = newest, up to 32 kept
  uint32_t passkey() const;         // numeric-comparison value if the stack asked, else 0
};
extern FujiCam fuji;
