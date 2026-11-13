// R1: reset gestures (spec 3.2). The RST button is wired to CHIP_PU (chip enable), not a
// GPIO, so firmware cannot read it. What it can see is that a power-on boot happened, and a
// flag in NVS says whether the previous power-on was less than RSTGEST_WINDOW_MS ago. Two
// taps of RST inside that window = open the setup menu. One tap = a plain reboot, exactly as
// before (it cannot be told apart from a battery going in).
#pragma once
#include <stdint.h>

constexpr uint32_t RSTGEST_WINDOW_MS = 3000;

enum class BootKind : uint8_t {
  PowerOn,    // ESP_RST_POWERON: battery in, USB plugged in, or RST (CHIP_PU) pressed
  DeepSleep,  // a deep-sleep wake (never a tap)
  Other,      // software, panic, watchdog, brownout, USB-JTAG: never a tap
};

struct RstDecision {
  bool open_setup = false;  // this boot is the second tap: go to the setup menu
  bool write = false;       // write value to NVS now
  uint8_t value = 0;
  bool armed = false;       // value 1 was written: clear it once uptime passes the window
};

// read_ok false (NVS unreadable): a normal boot, no write. stored: the NVS flag (0 or 1).
RstDecision rstgest_boot(BootKind kind, bool enabled, bool read_ok, uint8_t stored);
// The armed flag must be cleared now (the window has passed).
inline bool rstgest_settle_due(bool armed, uint32_t uptime_ms) {
  return armed && uptime_ms >= RSTGEST_WINDOW_MS;
}
