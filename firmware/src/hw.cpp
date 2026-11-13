// Board power rails, battery, LED, deep sleep for the Heltec WiFi LoRa 32 V4.
#include "hw.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "battery.h"
#include "config.h"

namespace {

uint32_t g_boot_ms = 0;
constexpr uint64_t kStuckRecheckUs = 30ULL * 1000 * 1000;
// C9: timer wakes in a row with PRG still LOW. Survives deep sleep, 0 after a power-on.
RTC_DATA_ATTR uint16_t g_stuck_wakes = 0;
// C10: smoothed battery voltage behind hw_batt_pct(); 0 = no reading yet.
float g_vbat_smooth = 0;
// S3: GPIO3 is the external tally LED (tally_pin 3 or 34), latched LOW through sleep.
constexpr int kTallyHoldPin = 3;

// Matches the reference (Meshtastic heltec_v4: ADC_ATTEN_DB_2_5 with the 4.9 * 1.045
// multiplier, see battery.h). 4.2 V / 4.9 = 0.86 V is inside the 2.5 dB range.
constexpr adc_attenuation_t kVbatAtten = ADC_2_5db;

// Output pins we latch during deep sleep. Released again on the next boot.
constexpr int kHeldOutputs[] = {pins::VEXT,     pins::GPS_EN,  pins::GPS_RST,
                                pins::GPS_STBY, pins::FEM_PWR, pins::FEM_CSD,
                                pins::FEM_CPS,  pins::FEM_CTX_V43, pins::ADC_CTRL,
                                pins::LED,      pins::LORA_CS};
constexpr int kWakeInputs[] = {pins::BTN_PRG, pins::BTN_HALF, pins::BTN_FULL};

void release_sleep_holds() {
  gpio_deep_sleep_hold_dis();
  for (int p : kHeldOutputs) gpio_hold_dis(static_cast<gpio_num_t>(p));
  // S3: always, because the tally setting is not loaded yet; a no-op if it was not held.
  gpio_hold_dis(static_cast<gpio_num_t>(kTallyHoldPin));
  for (int p : kWakeInputs) {
    gpio_num_t g = static_cast<gpio_num_t>(p);
    if (rtc_gpio_is_valid_gpio(g)) {
      rtc_gpio_hold_dis(g);
      rtc_gpio_deinit(g);  // hand the pad back to the digital GPIO matrix
    }
  }
}

void out(int pin, int level) {
  digitalWrite(pin, level);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}

void hold(int pin, int level) {
  out(pin, level);
  gpio_hold_en(static_cast<gpio_num_t>(pin));
}

void arm_wake_pin(int pin) {
  gpio_num_t g = static_cast<gpio_num_t>(pin);
  rtc_gpio_init(g);
  rtc_gpio_set_direction(g, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis(g);
  rtc_gpio_pullup_en(g);
  rtc_gpio_hold_en(g);
}

}  // namespace

void hw_init() {
  release_sleep_holds();

  out(pins::LED, LOW);
  out(pins::ADC_CTRL, LOW);
  hw_fem(false);
  // GNSS off until gps_power(true): EN active LOW. RST/STBY LOW so nothing back-feeds
  // the unpowered module through its I/O protection diodes.
  out(pins::GPS_EN, HIGH);
  out(pins::GPS_RST, LOW);
  out(pins::GPS_STBY, LOW);
  // Keep the SX1262 deselected until RadioLib takes the pin.
  out(pins::LORA_CS, HIGH);

  pinMode(pins::BTN_PRG, INPUT_PULLUP);
  pinMode(pins::BTN_HALF, INPUT_PULLUP);
  pinMode(pins::BTN_FULL, INPUT_PULLUP);

  analogReadResolution(12);
  analogSetPinAttenuation(pins::ADC_VBAT, kVbatAtten);

  hw_vext(true);
  g_boot_ms = millis();
}

void hw_vext(bool on) { out(pins::VEXT, on ? LOW : HIGH); }

void hw_fem(bool on) {
  if (on) {
    out(pins::FEM_PWR, HIGH);
    out(pins::FEM_CSD, HIGH);
    out(pins::FEM_CPS, HIGH);      // V4.2 GC1109: full PA in TX (DIO2 selects TX/RX)
    out(pins::FEM_CTX_V43, LOW);   // V4.3 KCT8103L: RX through the LNA
  } else {
    out(pins::FEM_CSD, LOW);
    out(pins::FEM_CPS, LOW);
    out(pins::FEM_CTX_V43, LOW);
    out(pins::FEM_PWR, LOW);
  }
}

float hw_vbat() {
  // Re-applied each time: the pairing RNG borrows the SAR ADC (bootloader_random_enable).
  analogSetPinAttenuation(pins::ADC_VBAT, kVbatAtten);
  digitalWrite(pins::ADC_CTRL, HIGH);
  delay(5);  // let the high-impedance divider settle
  uint32_t sum = 0;
  for (int i = 0; i < 8; ++i) sum += analogReadMilliVolts(pins::ADC_VBAT);
  digitalWrite(pins::ADC_CTRL, LOW);
  return vbat_from_adc_mv(sum / 8);
}

uint8_t hw_batt_pct() {
  float v = hw_vbat();
  if (v < 2.5f || v > 4.5f) {
    g_vbat_smooth = 0;  // C10: start again from the next good reading
    return 255;
  }
  // C10: one burst is noisy on the steep part of the curve, so the gauge uses a smoothed value.
  g_vbat_smooth = vbat_smooth(g_vbat_smooth, v);
  return lipo_pct(g_vbat_smooth);
}

static uint8_t g_led_duty = 255, g_tally = 0;
static bool g_ledc = false;
void hw_led_level(uint8_t duty) { g_led_duty = duty; }
void hw_led(bool on) {
  if (!g_ledc) { ledcSetup(7, 2000, 8); ledcAttachPin(pins::LED, 7); g_ledc = true; }
  ledcWrite(7, on ? g_led_duty : 0);
}
static uint8_t g_tally_gnd = 0;
void hw_tally_begin(uint8_t pin) {
  if (pin == 34) { g_tally = 3; g_tally_gnd = 4; }   // LED straight across GPIO3 (+) and GPIO4 (held at 0 V)
  else { g_tally = pin; g_tally_gnd = 0; }
  if (g_tally) { pinMode(g_tally, OUTPUT); digitalWrite(g_tally, LOW); }
  if (g_tally_gnd) { pinMode(g_tally_gnd, OUTPUT); digitalWrite(g_tally_gnd, LOW); }
}
void hw_tally(bool on) { if (g_tally) digitalWrite(g_tally, on ? HIGH : LOW); }

uint32_t hw_boot_ms() { return g_boot_ms; }

// L1: the 2 s hold-off gives time to notice a missing antenna at power-on. A deep-sleep wake
// is a button press on a unit that was already in use, so it may transmit at once.
uint32_t hw_tx_holdoff_ms() {
  return hw_woke_from_sleep() ? 0 : radiocfg::BOOT_TX_HOLDOFF_MS;
}

void hw_deep_sleep(uint8_t extra, bool prg_stuck) {
  Serial1.end();

  // Rails off and latched off through sleep.
  hold(pins::FEM_CSD, LOW);
  hold(pins::FEM_CPS, LOW);
  hold(pins::FEM_CTX_V43, LOW);
  hold(pins::FEM_PWR, LOW);
  hold(pins::GPS_EN, HIGH);
  hold(pins::GPS_RST, LOW);   // module unpowered: don't back-feed it
  hold(pins::GPS_STBY, LOW);
  pinMode(pins::GPS_TX, INPUT);
  hold(pins::ADC_CTRL, LOW);
  if (g_ledc) { ledcDetachPin(pins::LED); g_ledc = false; }
  hold(pins::LED, LOW);
  if (g_tally) hold(g_tally, LOW);
  if (g_tally_gnd) hold(g_tally_gnd, LOW);
  hold(pins::LORA_CS, HIGH);  // keep the sleeping SX1262 deselected
  hold(pins::VEXT, HIGH);
  gpio_deep_sleep_hold_en();
  Serial.flush();

  // RTC pull-ups on the wake buttons need the RTC peripheral domain; the duty-cycle
  // history lives in RTC slow memory.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);

  arm_wake_pin(pins::BTN_PRG);
  if (prg_stuck) {
    // Held or stuck LOW: ext0 would wake us at once, forever. Check again on a timer.
    Serial.println("[hw] PRG held LOW: timer wake instead");
    // C9: a button that stays down (in a bag) is checked less often as time goes on:
    // every 30 s for 5 min, then every 2 min. No longer step: PRG cannot wake us in this
    // sleep, so the recheck is also how long the button stays dead after it is freed.
    uint64_t recheck_us = kStuckRecheckUs;
    if (g_stuck_wakes >= 10) recheck_us = kStuckRecheckUs * 4;
    if (g_stuck_wakes < UINT16_MAX) g_stuck_wakes++;
    esp_sleep_enable_timer_wakeup(recheck_us);
  } else {
    // C2: the caller saw PRG up. If it is LOW by now, that is a new press made while we were
    // shutting down, not a stuck button: ext0 is level triggered, so it wakes us at once and
    // the press is handled like any wake press. A really stuck pin is caught by the callers.
    g_stuck_wakes = 0;  // C9: PRG is up again
    // Let the RTC pull-up settle first, so only a real press (still LOW) can wake us.
    if (rtc_gpio_get_level(static_cast<gpio_num_t>(pins::BTN_PRG)) == 0) delay(2);
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(pins::BTN_PRG), 0);
  }

  if (extra & HW_WAKE_AUX && !(extra & HW_WAKE_TWO_STAGE)) {
    // Second button (R2/R2b) on GPIO6: a wake source only if it reads up with Vext off.
    arm_wake_pin(pins::BTN_HALF);
    delay(2);
    if (rtc_gpio_get_level(static_cast<gpio_num_t>(pins::BTN_HALF)) != 0)
      esp_sleep_enable_ext1_wakeup(1ULL << pins::BTN_HALF, ESP_EXT1_WAKEUP_ANY_LOW);
    else Serial.println("[hw] aux GPIO6 held LOW with Vext off: not a wake source");
  }
  if (extra & HW_WAKE_TWO_STAGE) {
    // GPIO4 is also the QuickLink I2C SDA (pins_arduino.h SDA=4). If the board's I2C
    // pull-ups sit on the now-off Vext rail they drag the pin LOW and ext1 would fire at
    // once, forever. With the rails already off, any switch pin still reading LOW is held
    // by the board, not a finger (buttons_up() was checked before sleeping): leave it out.
    arm_wake_pin(pins::BTN_HALF);
    arm_wake_pin(pins::BTN_FULL);
    delay(2);
    uint64_t mask = 0;
    for (int p : {pins::BTN_HALF, pins::BTN_FULL}) {
      if (rtc_gpio_get_level(static_cast<gpio_num_t>(p)) != 0) mask |= 1ULL << p;
      else Serial.printf("[hw] GPIO%d held LOW with Vext off: not a wake source\n", p);
    }
    if (mask) esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  }

  esp_deep_sleep_start();
  for (;;) {
  }
}

void hw_stuck_wake_check(uint8_t extra) {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    g_stuck_wakes = 0;  // C9: any other wake (or a power-on) starts the recheck steps again
    return;
  }
  // Nothing else has started yet (no UI, radio or BLE). PRG still LOW: back to the timer.
  // Up now: a normal sleep, PRG armed, as the user left it (OFF / asleep).
  pinMode(pins::BTN_PRG, INPUT_PULLUP);
  delay(5);
  hw_deep_sleep(extra, digitalRead(pins::BTN_PRG) == LOW);
}

bool hw_woke_from_sleep() {
  return esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
}
