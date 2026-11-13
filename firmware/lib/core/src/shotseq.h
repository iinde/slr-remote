// Turns user intent into the timed camera command stream, used by the camera
// board (relay) and the handheld (direct). Pure timing logic, no BLE.
#pragma once
#include <stdint.h>

enum class CamCmd : uint8_t { FocusPress, ShutterPress, Release };

class ShotSequencer {
 public:
  // One press: FocusPress now, ShutterPress at now+focus_ms (or right away after
  // afLocked()), Release SHUTTER_HOLD_MS (200) after ShutterPress. Ignored while busy().
  bool startShot(uint32_t now, uint16_t focus_ms);
  // Two-stage passthrough. half(): FocusPress. full(): ShutterPress (a FocusPress is
  // emitted first if not already focusing). release(): Release. These cancel any
  // running one-press sequence (its pending steps are dropped, then the new cmd queued).
  void half(uint32_t now);
  void full(uint32_t now);
  void release(uint32_t now);
  // full() with its own Release hold_ms after the ShutterPress write returns (timed by this
  // board, not by a second command): a relay Shoot whose Release frame can take a whole SF12
  // airtime, or be lost. A Release that arrives sooner still lets go at once.
  void fullTimed(uint32_t now, uint16_t hold_ms);
  void afLocked(uint32_t now);  // optional hook: shoot now if a one-press is waiting
  // Pops the next due command. Call often. Returns false if nothing due.
  bool poll(uint32_t now, CamCmd& out);
  // Call after the popped step's camera write has returned (it can block for seconds). A
  // one-press then times its next step from that moment: the AF wait (focus_ms) counts from
  // the end of the FocusPress write, the shutter hold from the end of the ShutterPress
  // write, so time lost before or during a write never shortens either.
  void stepDone(CamCmd done, uint32_t now);
  bool busy() const;            // a one-press sequence is in progress
  bool buttonDown() const { return focusing_; }   // a Focus/ShutterPress ran, no Release yet
  bool shutterDown() const { return shutter_; }   // a ShutterPress ran, no Release yet
  bool releaseQueued() const;                     // a Release step is waiting to run
 private:
  struct Step { CamCmd cmd; uint32_t at; };
  Step q_[4];
  uint8_t n_ = 0;
  uint16_t focus_ms_ = 0;
  bool af_locked_ = false;
  bool oneshot_ = false;
  bool timed_ = false;     // fullTimed(): the queued Release is timed from the shutter write
  uint16_t hold_ms_ = 0;
  bool focusing_ = false;
  bool shutter_ = false;
  void push(CamCmd c, uint32_t at);
  void clear();
};
