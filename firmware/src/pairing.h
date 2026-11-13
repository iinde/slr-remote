// Board-to-board pairing over LoRa at minimum power (radiocfg::PAIR_SX_DBM), SF7.
// Unauthenticated pairing frames: magic "XP" + type + data. Commit/reveal, so neither side
// can be steered onto a chosen code:
//   HELLO  (handheld): C = SHA-256("xe5-commit" || hh_pub || Na)    Na: 16 random bytes
//   REPLY  (camera):   cam_pub[32] || Nb[16]       (camera answers only the first C it hears)
//   REVEAL (handheld): hh_pub[32] || Na[16]        (camera checks it against C)
// key  = SHA-256("xe5-remote-pair-v2" || shared || hh_pub || cam_pub || Na || Nb)
// code = (u32 LE of SHA-256(key)[0..3]) % 1000000, shown as 6 digits on both screens.
// Any pairing frame that does not fit the session (second handheld/camera, bad reveal)
// aborts it, but only before Verify: from then on (keys agreed and confirmed) unauthenticated
// pairing frames are ignored, so nobody can knock a saving board out of its commit. User confirms on both boards (PRG long-press = yes, click = no).
// Then: handheld Ping (seq S) -> camera Ack; handheld Ping (seq S+1) proves it holds that
// Ack -> camera saves and acks; handheld saves on that Ack. Until then the camera keeps
// its old pairing. Every pairing frame is counted by the duty-cycle governor.
#pragma once
#include <stdint.h>
#include "store.h"

// Unsure: the handheld never got the camera's commit Ack (or could not save): the camera board
// may hold the new key while the handheld kept the old one. Pair again.
// StoreFailed: this board could not write the new pairing to flash.
enum class PairResult : uint8_t { Ok, Timeout, Rejected, Failed, Unsure, StoreFailed };

// show(line1, line2): put text on screen. input(): 0 nothing, 1 confirm, -1 reject.
// Restores the previous SF and TX power before returning.
PairResult pair_run(bool initiator, void (*show)(const char*, const char*), int (*input)(),
                    PairRecord& out, uint32_t timeout_ms);
