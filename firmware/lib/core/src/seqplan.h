// Airtime planner for repeating modes in relay (spec 1.10). Each board may transmit 10 % of
// a rolling hour (360 s). A relayed shot costs the handheld one Shot frame and the camera
// board an Ack and a Result, so the camera board is usually the tight side.
#pragma once
#include <stdint.h>

#include "proto.h"

// Frame lengths come from proto.h so a payload change can never leave the planner stale
// (the Ack was still costed at its old 45 bytes after utc made it 47).
constexpr uint8_t PLAN_SHOT_FRAME = (uint8_t)(PROTO_HDR + SHOOT_PAYLOAD_LEN_TIMED + PROTO_TAG);  // 25
constexpr uint8_t PLAN_ACK_FRAME = (uint8_t)(PROTO_HDR + ACK_PAYLOAD_LEN + PROTO_TAG);           // 47
constexpr uint8_t PLAN_PING_FRAME = (uint8_t)(PROTO_HDR + PING_PAYLOAD_LEN + PROTO_TAG);         // 23
static_assert(PLAN_SHOT_FRAME == 25 && PLAN_ACK_FRAME == 47 && PLAN_PING_FRAME == 23,
              "planner frame lengths changed: re-check the spec 1.10 airtime table and tests");
constexpr uint32_t PLAN_BUDGET_MS = 360000;
constexpr uint32_t PLAN_MAX_RUN_S = 4 * 3600;  // INTERVAL "until stopped" never runs past 4 h

// Airtime of one relayed shot for one side (ms). camera_side: Ack + Result.
uint32_t plan_shot_cost_ms(uint8_t sf, bool camera_side);

struct PlanIn {
  uint8_t sf = 7;
  uint16_t hh_permille = 0;   // handheld duty used now (per mille of its hourly budget)
  uint16_t cam_permille = 0;  // camera board's, from its last Ack
  uint16_t every_s = 10;
  uint16_t count = 120;       // 0 = until stopped (capped at PLAN_MAX_RUN_S)
};

struct PlanOut {
  bool fits = false;
  uint16_t need_permille = 0;  // worse board: its cost in the run's busiest hour, x1.5 + pings
  uint16_t have_permille = 0;  // that board's budget left now
  uint16_t fit_every_s = 0;    // nearest interval that fits with the same count (0 = none)
  uint16_t fit_count = 0;      // most shots that fit at the same interval (0 = none)
  uint32_t duration_s = 0;     // the run's length
};

// The ARM check. Never fits at an interval shorter than one shot's round trip.
PlanOut plan_interval(const PlanIn& in);
// One more shot now: true if both boards have room for it (the handheld's own governor
// still decides at send time).
bool plan_shot_fits(uint8_t sf, uint16_t hh_permille, uint16_t cam_permille);
