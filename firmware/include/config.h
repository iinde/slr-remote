// XE5 Remote: board pins and fixed constants. Single source of truth.
// Pin map from Meshtastic's variants/esp32s3/heltec_v4/variant.h + pins_arduino.h
// (Heltec WiFi LoRa 32 V4.2 = GC1109 front end, V4.3 = KCT8103L front end).
#pragma once
#include <stdint.h>

namespace pins {
// Power / misc
constexpr int VEXT = 36;          // ACTIVE LOW: powers OLED and LoRa antenna boost
constexpr int LED = 35;
constexpr int BTN_PRG = 0;        // on-board PRG button, active LOW, RTC-capable (wake)
// External two-stage shutter switch (both to GND, internal pull-ups).
// GPIO 4 and 6 are RTC-capable, not strapping pins, not used on the V4.
// VERIFY on the real board's header silkscreen before wiring.
// GPIO4 (and GPIO3) are also the QuickLink I2C bus (pins_arduino.h SDA=4, SCL=3): do not use
// the QuickLink connector with a two-stage switch fitted. If that bus has pull-ups on Vext,
// GPIO4 reads LOW once Vext is off; hw_deep_sleep() then drops it as a wake source (PRG still
// wakes). Check the V4 schematic / measure, and move BTN_FULL to a free RTC pin if so.
constexpr int BTN_HALF = 6;
constexpr int BTN_FULL = 4;
// Battery sense
constexpr int ADC_VBAT = 1;
constexpr int ADC_CTRL = 37;      // HIGH enables the VBAT divider
// SX1262
constexpr int LORA_CS = 8, LORA_SCK = 9, LORA_MOSI = 10, LORA_MISO = 11;
constexpr int LORA_RST = 12, LORA_BUSY = 13, LORA_DIO1 = 14;
// RF front end (FEM). DIO2 drives TX/RX switching automatically.
constexpr int FEM_PWR = 7;        // HIGH: FEM LDO on
constexpr int FEM_CSD = 2;        // HIGH: FEM enabled
constexpr int FEM_CPS = 46;       // V4.2 GC1109: HIGH = full PA. (unconnected on V4.3)
constexpr int FEM_CTX_V43 = 5;    // V4.3 KCT8103L: LOW = RX LNA mode
// OLED (SSD1315, SSD1306-compatible, 128x64, I2C 0x3C)
constexpr int OLED_SDA = 17, OLED_SCL = 18, OLED_RST = 21;
// GNSS (L76K on the V4 expansion kit, 9600 baud)
constexpr int GPS_TX = 38;        // ESP -> GPS
constexpr int GPS_RX = 39;        // GPS -> ESP
constexpr int GPS_EN = 34;        // ACTIVE LOW
constexpr int GPS_RST = 42;       // LOW > 100 ms resets
constexpr int GPS_STBY = 40;      // HIGH = force awake
constexpr int GPS_PPS = 41;
}  // namespace pins

namespace radiocfg {
// Frequency, bandwidth, power limit and duty limit depend on the region: lib/core region.h.
// What follows is the same everywhere.
constexpr uint8_t CR_DENOM = 5;           // coding rate 4/5
constexpr uint8_t SYNC_WORD = 0x12;       // private network
constexpr uint16_t PREAMBLE = 8;
constexpr float TCXO_V = 1.8f;
constexpr uint8_t SF_MIN = 7, SF_MAX = 12;
// Front end adds ~+11 dB at SX1262 inputs <= 15 dBm (Meshtastic measurements).
constexpr int8_t FEM_GAIN_DB = 11;
constexpr int8_t SX_MAX_DBM = 14;         // hard ceiling on the SX1262 setting (~25 dBm out)
constexpr int8_t DEFAULT_ANT_GAIN_DBI = 3;
constexpr int8_t PAIR_SX_DBM = -9;        // board pairing: deliberately tiny range
constexpr uint32_t BOOT_TX_HOLDOFF_MS = 2000;  // never TX in first 2 s after boot
}  // namespace radiocfg

namespace timing {
constexpr uint16_t DEFAULT_FOCUS_MS = 500;  // one-press: focus -> wait -> shoot
constexpr uint16_t SHUTTER_HOLD_MS = 200;   // shutter press -> release
constexpr uint32_t CAM_EVENT_WINDOW_MS = 2000;
constexpr uint32_t HH_IDLE_SLEEP_MS = 60000;
constexpr uint32_t CAM_SCREEN_SLEEP_MS = 10000;
constexpr uint32_t CAM_RESCUE_SILENCE_MS = 8000;
constexpr uint32_t PING_MIN_INTERVAL_MS = 2000;
constexpr uint32_t HUNT_PING_INTERVAL_MS = 500;
}  // namespace timing

enum class Role : uint8_t { Unset = 0, Handheld = 1, Camera = 2 };
enum class Mode : uint8_t { Direct = 0, Relay = 1 };  // handheld only
