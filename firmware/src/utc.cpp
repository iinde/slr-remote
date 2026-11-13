#include "utc.h"
#include <Arduino.h>

namespace {
uint32_t g_base = 0, g_base_ms = 0;
constexpr uint32_t MIN_UNIX = 1767225600u;   // 2026-01-01
constexpr uint32_t MAX_UNIX = 4102444800u;   // 2100-01-01
}  // namespace

void utc_set(uint32_t unix_s) {
  if (unix_s < MIN_UNIX || unix_s >= MAX_UNIX) return;
  g_base = unix_s;
  g_base_ms = millis();
}
bool utc_valid() { return g_base != 0; }
uint32_t utc_now() { return g_base ? g_base + (millis() - g_base_ms) / 1000 : 0; }

uint32_t utc_from_ymdhms(int y, int mo, int d, int h, int mi, int s) {
  // days from civil (Howard Hinnant), valid for the Gregorian calendar
  y -= mo <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153u * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = (long)era * 146097 + (long)doe - 719468;
  return (uint32_t)(days * 86400L + h * 3600L + mi * 60L + s);
}
