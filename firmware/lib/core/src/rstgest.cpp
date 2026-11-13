#include "rstgest.h"

RstDecision rstgest_boot(BootKind kind, bool enabled, bool read_ok, uint8_t stored) {
  RstDecision d;
  if (!read_ok) return d;  // NVS failed: boot normally, touch nothing
  if (kind != BootKind::PowerOn || !enabled) {
    // Not a tap (or gestures off): a stale flag must never turn a later power-on into one.
    if (stored != 0) {
      d.write = true;
      d.value = 0;
    }
    return d;
  }
  if (stored == 1) {
    // The previous power-on was inside the window: second tap.
    d.open_setup = true;
    d.write = true;
    d.value = 0;
    return d;
  }
  d.write = true;
  d.value = 1;
  d.armed = true;
  return d;
}
