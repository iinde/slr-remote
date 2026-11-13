#include "rangelog_fmt.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {
const char HEADER[] =
    "t_s,mode,sf,tx_dbm,hh_lat,hh_lon,cam_lat,cam_lon,dist_m,rssi_up,snr_up,rssi_dn,snr_dn,"
    "ble_rssi,rtt_ms,retries,stage\n";

// Appends to out at *pos; on overflow marks *pos = cap (sticky failure).
void app(char* out, size_t cap, size_t* pos, const char* fmt, ...) {
  if (*pos >= cap) return;
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(out + *pos, cap - *pos, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= cap - *pos) *pos = cap;
  else *pos += (size_t)n;
}

void num(char* out, size_t cap, size_t* pos, double v, int decimals) {
  if (isnan(v)) app(out, cap, pos, ",");
  else app(out, cap, pos, "%.*f,", decimals, v);
}
}  // namespace

size_t range_csv_header(char* out, size_t cap) {
  const size_t n = sizeof(HEADER) - 1;
  if (out == nullptr || cap <= n) return 0;
  memcpy(out, HEADER, n + 1);
  return n;
}

size_t range_csv_row(const RangeRow& r, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  size_t pos = 0;
  app(out, cap, &pos, "%lu,%u,%u,%d,", (unsigned long)r.t_s, (unsigned)r.mode, (unsigned)r.sf,
      (int)r.tx_dbm);
  num(out, cap, &pos, r.hh_lat, 7);
  num(out, cap, &pos, r.hh_lon, 7);
  num(out, cap, &pos, r.cam_lat, 7);
  num(out, cap, &pos, r.cam_lon, 7);
  num(out, cap, &pos, r.dist_m, 1);
  num(out, cap, &pos, r.rssi_up, 1);
  num(out, cap, &pos, r.snr_up, 1);
  num(out, cap, &pos, r.rssi_dn, 1);
  num(out, cap, &pos, r.snr_dn, 1);
  app(out, cap, &pos, "%d,%u,%u,%u\n", (int)r.ble_rssi, (unsigned)r.rtt_ms, (unsigned)r.retries,
      (unsigned)r.stage);
  if (pos >= cap) {
    out[0] = '\0';
    return 0;
  }
  return pos;
}
