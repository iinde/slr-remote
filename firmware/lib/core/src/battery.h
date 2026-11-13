#pragma once
#include <stdint.h>
// Single-cell LiPo resting-voltage curve -> 0..100 %. <=3.30 V -> 0, >=4.15 V -> 100,
// piecewise-linear table: 3.30:0 3.60:5 3.70:15 3.75:30 3.80:45 3.85:55 3.90:65 4.00:80 4.10:95 4.15:100
uint8_t lipo_pct(float volts);
// Heltec V4 divider: Vbat = adc_mv / 1000 * 4.9 * 1.045. The 1.045 is Meshtastic's empirical
// correction, measured with the ADC at 2.5 dB attenuation (what hw.cpp uses). It is only valid
// at that attenuation; if a multimeter at 3.7 V / 4.1 V disagrees, re-measure it here.
float vbat_from_adc_mv(uint32_t adc_mv);
// Gauge smoothing: a quarter of each new reading is mixed into the running value, so one
// noisy or sagged burst moves the gauge only a little. prev <= 0 means no reading yet.
float vbat_smooth(float prev, float sample);
