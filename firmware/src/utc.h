// Wall-clock UTC for the SLR remote. The camera unit sets it from its GPS; the handheld learns it
// from the camera unit's replies over LoRa. Unknown until then (0).
#pragma once
#include <stdint.h>
void utc_set(uint32_t unix_s);   // anchors unix_s to millis() now; ignores implausible values
bool utc_valid();
uint32_t utc_now();              // 0 while unknown
uint32_t utc_from_ymdhms(int y, int mo, int d, int h, int mi, int s);
