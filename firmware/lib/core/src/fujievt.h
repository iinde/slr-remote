// Which Fujifilm BLE notifications may count as evidence that the camera exposed (CAM EVT).
// Pure logic, so it is host-tested; fuji.cpp classifies the characteristic UUID.
#pragma once
#include <stddef.h>
#include <stdint.h>

// After the link becomes ready the camera sends its own status traffic (NOT1 = 02 00
// "configured", IND1/IND2 status) for a second or two. None of it follows a shot.
constexpr uint32_t FUJI_READY_SETTLE_MS = 3000;

// evidence_uuid: NOT1 / IND1..IND3 (the characteristics that can follow a shutter press).
// is_not1: it is NOT1. ready_at_ms: millis() when the link became ready, 0 = not ready.
// shutter_at_ms: millis() when the last shutter write on this board began, 0 = never.
//
// The settle window only hides notifications that are not answering a press: once a shutter
// write has begun on this link (a wake or deferred shot fires within a second of ready), what
// follows it counts again. Otherwise those shots, the most common direct-mode case, could
// never reach CAM EVT. CamExec additionally only looks at notifications from the shutter
// write's start onwards. Still to confirm on hardware: no handshake chatter trails a shot this early.
inline bool fuji_notify_is_shot_evidence(bool evidence_uuid, bool is_not1, const uint8_t* data,
                                         size_t len, uint32_t now_ms, uint32_t ready_at_ms,
                                         uint32_t shutter_at_ms = 0) {
  if (!evidence_uuid) return false;
  // NOT1 = 02 00 is the "configured" handshake event, never an exposure.
  if (is_not1 && len >= 2 && data && data[0] == 0x02 && data[1] == 0x00) return false;
  if (ready_at_ms == 0) return false;  // handshake traffic, before the link is usable
  if ((uint32_t)(now_ms - ready_at_ms) >= FUJI_READY_SETTLE_MS) return true;
  // Inside the settle window: only after a shutter write that began on this link.
  return shutter_at_ms != 0 && (int32_t)(shutter_at_ms - ready_at_ms) >= 0 &&
         (int32_t)(now_ms - shutter_at_ms) >= 0;
}
