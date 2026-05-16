#pragma once
#include <switch.h>

// Small timing helpers shared by the CPU / Memory / GPU probes. All timing is
// based on the ARM system counter (armGetSystemTick), the same monotonic clock
// libnx exposes to homebrew, so results are directly comparable across runs.
namespace bench {

inline u64 tick() { return armGetSystemTick(); }

// Convert a tick delta to seconds / nanoseconds.
inline double toSec(u64 dt) { return (double)armTicksToNs(dt) / 1.0e9; }
inline u64    toNs (u64 dt) { return armTicksToNs(dt); }

// Wall-clock seconds elapsed since `since`.
inline double since(u64 t0) { return toSec(tick() - t0); }

} // namespace bench
