// LoRa time-on-air (Semtech SX1262 datasheet formula, explicit header, CRC on).
#pragma once
#include <stdint.h>
// Low data-rate optimisation is enabled automatically when symbol time >= 16 ms
// (SF11/SF12 at 125 kHz).
uint32_t lora_airtime_ms(uint8_t sf, float bw_khz, uint8_t cr_denom, uint16_t preamble,
                         uint8_t payload_len);
