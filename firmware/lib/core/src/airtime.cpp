#include "airtime.h"

#include <math.h>

// Semtech LoRa time on air, explicit header, CRC on. Rounded up so the duty
// governor never under-counts.
uint32_t lora_airtime_ms(uint8_t sf, float bw_khz, uint8_t cr_denom, uint16_t preamble,
                         uint8_t payload_len) {
  if (bw_khz <= 0.f || sf < 5 || sf > 12) return 0;
  const double tsym = (double)(1u << sf) / bw_khz;  // ms
  const int de = tsym >= 16.0 ? 1 : 0;              // low data-rate optimisation
  const int crc = 1, ih = 0;
  const double num = 8.0 * payload_len - 4.0 * sf + 28 + 16 * crc - 20 * ih;
  const double den = 4.0 * (sf - 2 * de);
  double extra = ceil(num / den) * cr_denom;
  if (extra < 0) extra = 0;
  const double n_payload = 8 + extra;
  const double t = (preamble + 4.25) * tsym + n_payload * tsym;
  return (uint32_t)ceil(t);
}
