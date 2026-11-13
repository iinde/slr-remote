// Board power rails, battery, LED, deep sleep.
#pragma once
#include <stdint.h>

void hw_init();                 // pins to safe defaults, ADC setup, Vext ON, FEM OFF, GPS OFF
void hw_vext(bool on);          // OLED + LoRa antenna boost rail (pins::VEXT active LOW)
void hw_fem(bool on);           // FEM LDO + CSD; when on: CPS HIGH (V4.2 PA), CTX_V43 LOW (V4.3 LNA).
                                // radio_send() raises CTX_V43 for the duration of each TX.
float hw_vbat();                // volts, averaged over 8 samples, ADC_CTRL toggled around read
uint8_t hw_batt_pct();          // lipo_pct of hw_vbat() smoothed over the last few calls; 255 if reading implausible (<2.5 V or >4.5 V: USB only)
void hw_led(bool on);
void hw_led_level(uint8_t duty);   // brightness for hw_led(true): 255 bright, ~20 dim
void hw_tally_begin(uint8_t pin);  // external tally LED (0 = none); driven LOW
void hw_tally(bool on);
uint32_t hw_boot_ms();          // millis() at end of hw_init
// Boot TX hold-off counted from hw_boot_ms(): BOOT_TX_HOLDOFF_MS after a power-on or reset,
// 0 on a deep-sleep wake (a wake press may transmit at once).
uint32_t hw_tx_holdoff_ms();
// Powers down OLED (via caller), GPS, FEM, radio (caller puts SX1262 to sleep first),
// isolates GPIOs, arms wake on PRG (ext0 LOW) and, if two_stage, BTN_HALF/BTN_FULL
// (ext1 ANY_LOW), then esp_deep_sleep_start().
// prg_stuck: PRG reads LOW and would wake us at once. It is left out of the wake sources and
// a timer wakes us instead (every 30 s at first, every 2 min after 5 min down); setup() then
// calls hw_stuck_wake_check() first thing. With prg_stuck false, PRG is always armed: a press
// that lands during shutdown wakes the board at once.
// extra: HW_WAKE_TWO_STAGE (true, as before) arms BTN_HALF and BTN_FULL; HW_WAKE_AUX arms
// only GPIO6, the R2/R2b second button (it shares the pin with BTN_HALF).
constexpr uint8_t HW_WAKE_TWO_STAGE = 1;
constexpr uint8_t HW_WAKE_AUX = 2;
[[noreturn]] void hw_deep_sleep(uint8_t extra, bool prg_stuck = false);
// On a timer wake (a stuck-PRG sleep): sleeps again straight away, with PRG armed if it is up
// now, else on the timer again. Returns only if this boot was not such a wake.
void hw_stuck_wake_check(uint8_t extra);
bool hw_woke_from_sleep();      // true if this boot was a deep-sleep wake
