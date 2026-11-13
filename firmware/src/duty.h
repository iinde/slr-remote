// The one transmit duty-cycle governor for this board (both roles + board pairing).
// Survives deep sleep and software resets (RTC memory + a clock that keeps running through
// sleep). After a power-on, where that history is gone, it restores the last NVS snapshot
// with its buckets' ages kept (as if the power cut took no time), plus the most airtime that
// can have gone out unsaved, but only if something was recorded after that snapshot.
#pragma once
#include <stdint.h>

void duty_begin();                          // once per boot, before any TX
uint32_t duty_now_ms();                     // ms clock that keeps counting through deep sleep
bool duty_can_send(uint32_t airtime_ms, bool priority);  // call right before a transmit
bool duty_allows(uint32_t airtime_ms, bool priority);    // same answer, no side effects
void duty_record(uint32_t airtime_ms);
uint32_t duty_used_ms();
uint16_t duty_permille();
// Writes the NVS snapshot. force: now, if anything changed (before deep sleep / restart).
// Not forced: same as duty_idle().
void duty_checkpoint(bool force);
// Call from the app loop only at an idle moment (no press running, nothing held, no BLE
// write due): writes a snapshot that duty_record() marked due, or the periodic one (every
// 5 min, if anything was recorded or has rolled off since). Never inside a press.
void duty_idle();
void duty_utc_tick();   // call from the loop: ages the saved airtime once real (GPS) time is known
// The last snapshot write failed (NVS full/worn). Past the unsaved bound nothing is sent.
bool duty_store_failed();
