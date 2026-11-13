#include "battery.h"

#include <math.h>

namespace {
struct Pt { float v; float pct; };
constexpr Pt CURVE[] = {{3.30f, 0},  {3.60f, 5},  {3.70f, 15}, {3.75f, 30}, {3.80f, 45},
                        {3.85f, 55}, {3.90f, 65}, {4.00f, 80}, {4.10f, 95}, {4.15f, 100}};
constexpr int N = sizeof(CURVE) / sizeof(CURVE[0]);
}  // namespace

uint8_t lipo_pct(float volts) {
  if (!(volts > CURVE[0].v)) return 0;  // also NAN
  if (volts >= CURVE[N - 1].v) return 100;
  for (int i = 1; i < N; i++) {
    if (volts <= CURVE[i].v) {
      const Pt a = CURVE[i - 1], b = CURVE[i];
      const float pct = a.pct + (volts - a.v) * (b.pct - a.pct) / (b.v - a.v);
      return (uint8_t)lroundf(pct);
    }
  }
  return 100;
}

float vbat_from_adc_mv(uint32_t adc_mv) { return (float)adc_mv / 1000.f * 4.9f * 1.045f; }

float vbat_smooth(float prev, float sample) {
  if (!(prev > 0)) return sample;  // no history yet (also NAN): start from this reading
  return prev + 0.25f * (sample - prev);
}
