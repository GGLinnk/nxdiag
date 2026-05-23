#pragma once

// Dev-time debug sink for nxdiag.
//
// Each log() line is sent to two places:
//   1. stdout, which init() redirects to nxlink-host over the local network
//      when a dev PC is running `nxlink`. With no listener, stdout stays as
//      the default no-op sink and printf calls are silently discarded.
//   2. svcOutputDebugString, which the kernel discards on retail hardware
//      but every Switch emulator (Ryujinx, yuzu, suyu, ...) surfaces in its
//      log window or console.
//
// Both sinks are best-effort and require no per-call setup beyond init().
namespace debug {

// Bring up the socket stack and try the nxlink redirect. Safe to call when
// no nxlink listener is reachable - silently falls back to no nxlink stdout.
void init();

// Tear down the socket stack at process exit.
void exit();

// printf-style log line, broadcast to both sinks. A trailing newline is
// added if missing so each call shows as one line.
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

} // namespace debug
