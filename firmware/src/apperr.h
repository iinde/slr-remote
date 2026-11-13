// Local error codes (above the FujiErr range) carried in UiShot.err / AckPayload.err.
#pragma once
#include <stdint.h>

namespace apperr {
constexpr uint8_t NO_ACK = 0x80;     // handheld: no LoRa Ack after every retry
constexpr uint8_t NO_TX = 0x81;      // handheld: hold-off / duty cycle kept it off the air
constexpr uint8_t CANCELLED = 0x82;  // one-press replaced before its shutter step / not sent
constexpr uint8_t BUSY = 0x83;       // one-press already running
constexpr uint8_t NO_RESULT = 0x84;  // handheld: delivered, but no outcome ever came back
constexpr uint8_t STALE = 0x85;      // camera board restarted since this command was first sent
constexpr uint8_t STORE = 0x86;      // camera board could not persist its replay guard: not run
constexpr uint8_t QUEUED = 0x87;     // handheld: waited too long behind another command, not sent
constexpr uint8_t NEED_FRESH = 0x88; // camera board: command not bound to a recent frame of ours
                                     // (possible replay): not run; the handheld resends fresh
constexpr uint8_t HOLD_LOST = 0x89;  // camera board released a held half/full press: no word
                                     // from the handheld for its silence time
constexpr uint8_t GAVE_UP = 0x8A;    // Release: the camera never acknowledged it, retries ended
}  // namespace apperr
