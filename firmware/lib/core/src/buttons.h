// Debounced button logic. Feed raw pressed/not-pressed samples with millis().
#pragma once
#include <stdint.h>

enum class BtnEvent : uint8_t { None, Click, Long, VeryLong };

// Single button (PRG). Debounce 25 ms.
//  Click    : released before 1000 ms
//  Long     : emitted once when held reaches 1000 ms
//  VeryLong : emitted once when held reaches 4000 ms (after Long)
// Nothing is emitted on the release that follows Long/VeryLong.
class ClickButton {
 public:
  BtnEvent update(bool pressed, uint32_t now);
  bool isDown() const { return stable_; }
 private:
  bool raw_ = false, stable_ = false;
  uint32_t raw_since_ = 0, down_at_ = 0;
  bool long_sent_ = false, vlong_sent_ = false;
};

// Two-stage shutter switch. half closes first, full closes with further travel.
// Debounce 15 ms each contact. Emits transitions of the combined state
// Up -> Half -> Full and back. If full is seen without half, treat as Full.
enum class StageEvent : uint8_t { None, HalfDown, FullDown, FullUp, AllUp };
class TwoStage {
 public:
  StageEvent update(bool half, bool full, uint32_t now);
  uint8_t level() const { return level_; }  // 0 up, 1 half, 2 full
 private:
  bool h_raw_ = false, f_raw_ = false, h_ = false, f_ = false;
  uint32_t h_since_ = 0, f_since_ = 0;
  uint8_t level_ = 0;
};
