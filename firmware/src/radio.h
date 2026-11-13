// SX1262 via RadioLib, behind the V4 front end. Always returns to continuous RX
// after a transmit. Enforces the legal power cap and the 2 s boot TX hold-off.
#pragma once
#include <stdint.h>

struct RxPacket {
  uint8_t data[64];
  uint8_t len;
  float rssi;   // dBm
  float snr;    // dB
  uint32_t at_ms;  // millis() when it was read out of the chip (before a transmit, if kept over one)
};

bool radio_begin(uint8_t sf, int8_t ant_gain_dbi);  // init, FEM on, RX started. false on failure
bool radio_set_sf(uint8_t sf);          // 7..12, restarts RX. false if the SF was not set
uint8_t radio_sf();
// Highest SX1262 setting that keeps ERP <= 27 dBm for the given antenna gain:
// min(SX_MAX_DBM, ERP_LIMIT + 2.15 - gain - FEM_GAIN), floored.
int8_t radio_legal_sx_dbm(int8_t ant_gain_dbi);
bool radio_set_tx_dbm(int8_t sx_dbm);   // clamped to radio_legal_sx_dbm(); returns false if clamped
int8_t radio_tx_dbm();                  // current SX1262 setting
int8_t radio_tx_out_dbm();              // estimated FEM output (setting + gain)
// Blocking transmit (returns after TX done or the timeout), then back to RX.
// NotStarted: nothing went on air (hold-off, radio down, startTransmit refused).
// Done: TX_DONE seen. TimedOut: the transmit started but TX_DONE never came; the packet may
// well have gone out, so callers must still book its airtime with the duty governor.
enum class RadioTx : uint8_t { NotStarted, Done, TimedOut };
RadioTx radio_send(const uint8_t* data, uint8_t len);
inline bool radio_tx_started(RadioTx r) { return r != RadioTx::NotStarted; }
// Transmits in a row that started but never reported TX_DONE (0 after any good one).
uint8_t radio_tx_fault_streak();
bool radio_poll(RxPacket& out);         // non-blocking; true if a packet with valid CRC arrived
// The receiver failed to start several times in a row (still retried, less often): deaf for now.
bool radio_rx_fault();
// A packet is waiting for radio_poll(). A packet that had arrived when radio_send(),
// radio_set_sf() or radio_set_tx_dbm() was called is kept (one slot) and polled afterwards.
bool radio_rx_pending();
uint32_t radio_airtime_ms(uint8_t len); // for the current SF
// SX1262 sleep + FEM off (before deep sleep or a software restart). Works even if
// radio_begin() never ran or failed: it then brings the chip up just far enough to sleep it.
void radio_sleep();
// Called about every millisecond while radio_send() waits for TX done (e.g. to sample
// buttons). Must not call radio_* functions. nullptr disables.
void radio_set_wait_hook(void (*hook)());
// Fills out with SX1262 wideband-RSSI noise bytes (~10 ms per byte); restarts RX.
// Returns false (out untouched) if the radio is not up.
bool radio_random(uint8_t* out, uint8_t len);
