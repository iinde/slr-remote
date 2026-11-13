// L76K GNSS on Serial1 (9600 baud) with TinyGPSPlus.
#pragma once
#include <stdint.h>

struct GpsFix {
  bool valid;            // location valid and age < 5 s
  double lat, lon, alt_m;
  uint8_t sats;
  float hdop;
  uint32_t age_ms;
  bool time_valid;
  int year, month, day, hour, minute, second;  // UTC
};

void gps_begin();          // configure pins, leave powered OFF
void gps_power(bool on);   // GPS_EN active LOW, STBY HIGH when on; Serial1 begin/end
bool gps_is_on();
void gps_loop();           // drain UART into the parser; call every loop
GpsFix gps_fix();
