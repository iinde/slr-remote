// CSV formatting for range-test rows.
#pragma once
#include <stddef.h>
#include <stdint.h>

struct RangeRow {
  uint32_t t_s;          // seconds since boot
  uint8_t mode;          // 0 direct, 1 relay
  uint8_t sf;            // 0 in direct mode
  int8_t tx_dbm;         // SX1262 setting
  double hh_lat, hh_lon; // NAN if no fix
  double cam_lat, cam_lon;
  double dist_m;         // NAN if unknown
  float rssi_up, snr_up, rssi_dn, snr_dn;  // NAN if unknown
  int8_t ble_rssi;       // 0 unknown
  uint16_t rtt_ms;
  uint8_t retries;
  uint8_t stage;         // ShotStage numeric value, or RANGE_STAGE_UNKNOWN
};
// Logged instead of a stage when the press may or may not have fired (no reply / no result /
// unconfirmed shutter write): never counted as a success or a failure.
constexpr uint8_t RANGE_STAGE_UNKNOWN = 6;
// Header: "t_s,mode,sf,tx_dbm,hh_lat,hh_lon,cam_lat,cam_lon,dist_m,rssi_up,snr_up,rssi_dn,snr_dn,ble_rssi,rtt_ms,retries,stage\n"
size_t range_csv_header(char* out, size_t cap);
// NAN fields are written empty. lat/lon 7 decimals, dist 1 decimal, rssi/snr 1 decimal.
size_t range_csv_row(const RangeRow& r, char* out, size_t cap);
