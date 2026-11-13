// SX1262 on the Heltec V4 via RadioLib, continuous RX, region power cap, boot TX hold-off.
#include "radio.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <math.h>

#include "airtime.h"
#include "config.h"
#include "hw.h"

namespace {

SPIClass g_spi(FSPI);
SX1262 g_radio(new Module(pins::LORA_CS, pins::LORA_DIO1, pins::LORA_RST, pins::LORA_BUSY,
                          g_spi));

Region g_region = Region::Unset;
volatile bool g_irq = false;
bool g_ready = false;
uint8_t g_sf = 7;
int8_t g_ant_gain = radiocfg::DEFAULT_ANT_GAIN_DBI;
int8_t g_tx_dbm = 0;
void (*g_wait_hook)() = nullptr;
uint8_t g_tx_faults = 0;
// S4: false while the last startReceive failed, so radio_poll() knows to try again.
bool g_rx_armed = false;
uint32_t g_rx_retry_ms = 0;
uint8_t g_rx_fails = 0;  // S4: startReceive failures in a row (0 once RX is armed again)
// S5: one packet read out of the chip before a transmit or retune would have wiped it.
RxPacket g_stash;
bool g_stash_valid = false;

constexpr int8_t kSxMinDbm = -9;  // SX1262 lowest setting
constexpr uint32_t kTxTimeoutMs = 3000;
constexpr uint8_t kMaxPacket = sizeof(RxPacket::data);
constexpr uint32_t kRxRetryMs = 500;  // S4: gap between attempts to re-arm a failed RX
// S4: a chip that does not answer blocks each attempt for about 2 s (RadioLib's busy timeout,
// twice), so after a few failures in a row the attempts are spaced out and the loop stays live.
constexpr uint8_t kRxFaultFails = 3;
uint32_t rx_retry_gap_ms() {
  return g_rx_fails < kRxFaultFails ? kRxRetryMs : g_rx_fails < 2 * kRxFaultFails ? 2000 : 10000;
}

void IRAM_ATTR on_dio1() { g_irq = true; }

bool tx_hold_off_active() {
  // L1: the hold-off is for a power-on (antenna check); a deep-sleep wake gets 0 ms.
  return millis() - hw_boot_ms() < hw_tx_holdoff_ms();
}

bool restart_rx() {
  g_irq = false;
  // S4: remember a failed start so radio_poll() retries it and the radio cannot stay deaf.
  g_rx_armed = g_radio.startReceive() == RADIOLIB_ERR_NONE;
  if (!g_rx_armed) g_rx_retry_ms = millis();
  if (g_rx_armed) g_rx_fails = 0;
  else if (g_rx_fails < 255) g_rx_fails++;
  return g_rx_armed;
}

// Reads the packet behind a DIO1 interrupt out of the chip. false if it was bad.
bool read_packet(RxPacket& out) {
  g_irq = false;
  size_t len = g_radio.getPacketLength();
  uint8_t buf[kMaxPacket];
  int16_t st = g_radio.readData(buf, kMaxPacket);
  if (st != RADIOLIB_ERR_NONE || len == 0 || len > kMaxPacket) {
    // readData clears the chip's IRQ flags only on success or CRC mismatch. Anything else
    // (SPI error, bad length) would leave RX_DONE latched and DIO1 high, so no further
    // rising edge would ever reach on_dio1. Restart RX, which clears them.
    if (st != RADIOLIB_ERR_CRC_MISMATCH) {
      g_radio.standby();
      restart_rx();
    }
    return false;
  }
  memcpy(out.data, buf, len);
  out.len = static_cast<uint8_t>(len);
  out.rssi = g_radio.getRSSI();
  out.snr = g_radio.getSNR();
  out.at_ms = millis();
  return true;
}

// S5: call before anything that leaves RX (standby, transmit, retune). A packet that has
// fully arrived but not been polled is kept for the next radio_poll() instead of being lost.
// One slot only: with a packet already held, a second one is still lost, as before.
void stash_pending() {
  if (!g_irq || g_stash_valid) return;
  g_stash_valid = read_packet(g_stash);
}

// V4.3 KCT8103L: CTX must be HIGH for the PA path (CSD=1 CTX=1 CPS=1, CPS from DIO2) and
// LOW for the RX LNA. On V4.2 (GC1109, CTX on DIO2) GPIO5 is not part of the FEM control.
void fem_ctx_tx(bool tx) { digitalWrite(pins::FEM_CTX_V43, tx ? HIGH : LOW); }

// Puts an SX1262 that RadioLib may not own (never begun, or begin failed) into cold sleep.
void force_chip_sleep() {
  g_spi.begin(pins::LORA_SCK, pins::LORA_MISO, pins::LORA_MOSI, pins::LORA_CS);
  // Receive side only, nothing goes on air (with no region set this tunes to the EU figures).
  const RegionCfg& rc = region_cfg(g_region);
  if (g_radio.begin(rc.freq_mhz, rc.bw_khz, radiocfg::SF_MIN, radiocfg::CR_DENOM,
                    radiocfg::SYNC_WORD, kSxMinDbm, radiocfg::PREAMBLE,
                    radiocfg::TCXO_V) == RADIOLIB_ERR_NONE) {
    g_radio.sleep(false);
  }
}

bool begin_failed(const char* what, int16_t st) {
  Serial.printf("[radio] %s failed %d\n", what, st);
  g_ready = false;
  g_radio.clearDio1Action();
  g_radio.sleep(false);  // best effort; a dead chip just ignores it
  hw_fem(false);
  return false;
}

}  // namespace

void radio_set_region(Region r) { g_region = r; }

Region radio_region() { return g_region; }

float radio_bw_khz() { return region_cfg(g_region).bw_khz; }

int8_t radio_legal_sx_dbm(int8_t ant_gain_dbi) {
  return region_legal_sx_dbm(g_region, ant_gain_dbi, radiocfg::FEM_GAIN_DB, radiocfg::SX_MAX_DBM,
                             kSxMinDbm);
}

bool radio_begin(uint8_t sf, int8_t ant_gain_dbi) {
  // No region chosen: the radio stays down, so nothing can go out on the wrong band.
  if (!region_can_tx(g_region)) {
    Serial.println("[radio] no region set: radio stays off");
    return false;
  }
  const RegionCfg& rc = region_cfg(g_region);
  if (sf < radiocfg::SF_MIN || sf > radiocfg::SF_MAX) sf = radiocfg::SF_MIN;
  g_ant_gain = ant_gain_dbi;
  g_tx_dbm = radio_legal_sx_dbm(ant_gain_dbi);

  hw_fem(true);
  g_spi.begin(pins::LORA_SCK, pins::LORA_MISO, pins::LORA_MOSI, pins::LORA_CS);

  int16_t st = g_radio.begin(rc.freq_mhz, rc.bw_khz, sf, radiocfg::CR_DENOM,
                             radiocfg::SYNC_WORD, g_tx_dbm, radiocfg::PREAMBLE,
                             radiocfg::TCXO_V);
  if (st != RADIOLIB_ERR_NONE) return begin_failed("begin", st);
  bool ok = g_radio.setDio2AsRfSwitch(true) == RADIOLIB_ERR_NONE;
  ok = ok && g_radio.setCurrentLimit(140) == RADIOLIB_ERR_NONE;
  ok = ok && g_radio.setCRC(2) == RADIOLIB_ERR_NONE;
  // Range first: boosted LNA gain (~+2 mA in RX).
  ok = ok && g_radio.setRxBoostedGainMode(true) == RADIOLIB_ERR_NONE;
  if (!ok) return begin_failed("config", 0);
  g_radio.setDio1Action(on_dio1);
  g_sf = sf;
  g_ready = restart_rx();
  if (!g_ready) return begin_failed("start rx", 0);
  return true;
}

bool radio_set_sf(uint8_t sf) {
  if (!g_ready || sf < radiocfg::SF_MIN || sf > radiocfg::SF_MAX) return false;
  stash_pending();  // S5: keep a packet that arrived on the old SF
  g_radio.standby();
  bool ok = g_radio.setSpreadingFactor(sf) == RADIOLIB_ERR_NONE;
  if (ok) g_sf = sf;
  // S4: the result says only whether the SF was really set; a failed RX start is retried
  // from radio_poll().
  restart_rx();
  return ok;
}

uint8_t radio_sf() { return g_sf; }

bool radio_set_tx_dbm(int8_t sx_dbm) {
  int8_t legal = radio_legal_sx_dbm(g_ant_gain);
  int8_t v = sx_dbm;
  if (v > legal) v = legal;
  if (v < kSxMinDbm) v = kSxMinDbm;
  bool clamped = v != sx_dbm;
  if (!g_ready) return false;
  stash_pending();  // S5: standby would drop a packet that has not been polled yet
  g_radio.standby();
  if (g_radio.setOutputPower(v) == RADIOLIB_ERR_NONE) g_tx_dbm = v;
  restart_rx();
  return !clamped;
}

int8_t radio_tx_dbm() { return g_tx_dbm; }

int8_t radio_tx_out_dbm() { return g_tx_dbm + radiocfg::FEM_GAIN_DB; }

RadioTx radio_send(const uint8_t* data, uint8_t len) {
  if (!g_ready || data == nullptr || len == 0) return RadioTx::NotStarted;
  if (!region_can_tx(g_region)) return RadioTx::NotStarted;
  if (tx_hold_off_active()) return RadioTx::NotStarted;

  stash_pending();  // S5: the transmit overwrites the chip's buffer, so read it out first
  hw_fem(true);
  g_radio.standby();
  g_irq = false;
  fem_ctx_tx(true);
  if (g_radio.startTransmit(data, len) != RADIOLIB_ERR_NONE) {
    fem_ctx_tx(false);
    restart_rx();
    return RadioTx::NotStarted;
  }
  uint32_t timeout = kTxTimeoutMs;
  uint32_t air = radio_airtime_ms(len) + 200;
  if (air > timeout) timeout = air;
  uint32_t start = millis();
  while (!g_irq && millis() - start < timeout) {
    if (g_wait_hook) g_wait_hook();
    delay(1);
  }
  bool done = g_irq;
  g_radio.finishTransmit();
  fem_ctx_tx(false);
  restart_rx();
  if (done) {
    g_tx_faults = 0;
    return RadioTx::Done;
  }
  if (g_tx_faults < 255) g_tx_faults++;
  Serial.printf("[radio] TX_DONE not seen after %lu ms (streak %u)\n", (unsigned long)timeout,
                g_tx_faults);
  return RadioTx::TimedOut;
}

uint8_t radio_tx_fault_streak() { return g_tx_faults; }

bool radio_poll(RxPacket& out) {
  if (!g_ready) return false;
  // S5: a packet saved before a transmit or retune comes out first, so order is kept.
  if (g_stash_valid) {
    out = g_stash;
    g_stash_valid = false;
    return true;
  }
  // S4: RX failed to start earlier. Try again here, at most every kRxRetryMs (backed off
  // after repeated failures, rx_retry_gap_ms).
  if (!g_rx_armed && !g_irq && millis() - g_rx_retry_ms >= rx_retry_gap_ms()) {
    g_radio.standby();
    restart_rx();
  }
  if (!g_irq) return false;
  return read_packet(out);
}

// S4: the receiver could not be re-armed several times in a row: we are deaf for now.
bool radio_rx_fault() { return g_ready && g_rx_fails >= kRxFaultFails; }

// S5: true if radio_poll() has something to read (a saved packet or a fresh interrupt).
bool radio_rx_pending() { return g_ready && (g_stash_valid || g_irq); }

uint32_t radio_airtime_ms(uint8_t len) {
  return lora_airtime_ms(g_sf, radio_bw_khz(), radiocfg::CR_DENOM, radiocfg::PREAMBLE, len);
}

void radio_sleep() {
  if (g_ready) {
    g_radio.clearDio1Action();
    g_radio.sleep(false);
  } else {
    force_chip_sleep();  // otherwise it idles in STDBY_RC (~0.6 mA) or stays in RX (~5 mA)
  }
  g_ready = false;
  g_irq = false;
  g_stash_valid = false;  // S5: a saved packet must not outlive the radio session
  hw_fem(false);
}

void radio_set_wait_hook(void (*hook)()) { g_wait_hook = hook; }

bool radio_random(uint8_t* out, uint8_t len) {
  if (!g_ready || !out) return false;
  stash_pending();  // S5: the RNG takes the receiver over, same as a transmit
  for (uint8_t i = 0; i < len; i++) out[i] = g_radio.randomByte();
  restart_rx();
  return true;
}
