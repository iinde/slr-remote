// L76K GNSS (V4 expansion kit) on Serial1, parsed with TinyGPSPlus.
#include "gps.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "config.h"

namespace {

TinyGPSPlus g_gps;
bool g_on = false;

constexpr uint32_t kFreshMs = 5000;
constexpr size_t kMaxDrainPerLoop = 512;  // ~0.5 s of NMEA at 9600 baud

}  // namespace

void gps_begin() {
  pinMode(pins::GPS_EN, OUTPUT);
  digitalWrite(pins::GPS_EN, HIGH);  // active LOW: off
  pinMode(pins::GPS_RST, OUTPUT);
  digitalWrite(pins::GPS_RST, LOW);  // never drive the unpowered module
  pinMode(pins::GPS_STBY, OUTPUT);
  digitalWrite(pins::GPS_STBY, LOW);
  pinMode(pins::GPS_PPS, INPUT);
  g_on = false;
}

void gps_power(bool on) {
  if (on == g_on) return;
  if (on) {
    // Power first, then lift RST/STBY: driving them HIGH into an unpowered L76K back-feeds
    // its rail through the pin diodes.
    digitalWrite(pins::GPS_EN, LOW);
    delay(10);
    digitalWrite(pins::GPS_RST, HIGH);
    digitalWrite(pins::GPS_STBY, HIGH);
    Serial1.setRxBufferSize(4096);  // ~4 s of NMEA: survives a blocking SF12 transmit
    Serial1.begin(9600, SERIAL_8N1, pins::GPS_RX, pins::GPS_TX);
  } else {
    Serial1.end();
    pinMode(pins::GPS_TX, INPUT);  // don't drive the unpowered module's RX
    digitalWrite(pins::GPS_STBY, LOW);
    digitalWrite(pins::GPS_RST, LOW);
    digitalWrite(pins::GPS_EN, HIGH);
  }
  g_on = on;
}

bool gps_is_on() { return g_on; }

void gps_loop() {
  if (!g_on) return;
  for (size_t n = 0; n < kMaxDrainPerLoop && Serial1.available() > 0; ++n) {
    g_gps.encode(static_cast<char>(Serial1.read()));
  }
}

GpsFix gps_fix() {
  GpsFix f{};
  uint32_t age = g_gps.location.age();
  f.age_ms = age;
  f.valid = g_on && g_gps.location.isValid() && age < kFreshMs;
  f.lat = g_gps.location.lat();
  f.lon = g_gps.location.lng();
  f.alt_m = g_gps.altitude.isValid() ? g_gps.altitude.meters() : 0.0;
  f.sats = g_gps.satellites.isValid() ? static_cast<uint8_t>(g_gps.satellites.value()) : 0;
  f.hdop = g_gps.hdop.isValid() ? static_cast<float>(g_gps.hdop.hdop()) : 99.9f;

  f.time_valid = g_on && g_gps.date.isValid() && g_gps.time.isValid() &&
                 g_gps.time.age() < kFreshMs && g_gps.date.year() >= 2024;
  f.year = g_gps.date.year();
  f.month = g_gps.date.month();
  f.day = g_gps.date.day();
  f.hour = g_gps.time.hour();
  f.minute = g_gps.time.minute();
  f.second = g_gps.time.second();
  return f;
}
