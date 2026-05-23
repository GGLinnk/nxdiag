#include "debug.hpp"
#include <cstdarg>
#include <cstdio>
#ifndef NXD_HOST
#include <switch.h>
#endif

namespace debug {

#ifndef NXD_HOST
namespace { bool g_socketUp = false; }
#endif

void init() {
#ifndef NXD_HOST
    if (g_socketUp) return;
    if (R_FAILED(socketInitializeDefault())) return;   // keep going without nxlink
    g_socketUp = true;
    nxlinkStdio();                                     // -1 = no listener, harmless
#endif
}

void exit() {
#ifndef NXD_HOST
    if (!g_socketUp) return;
    socketExit();
    g_socketUp = false;
#endif
}

void log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf)) n = (int)(sizeof(buf) - 1);
    // Ensure a trailing newline so both sinks render line-by-line.
    if (n == 0 || buf[n - 1] != '\n') {
        if ((size_t)n < sizeof(buf) - 1) { buf[n++] = '\n'; buf[n] = 0; }
    }
    fputs(buf, stdout);
#ifndef NXD_HOST
    svcOutputDebugString(buf, (u64)n);
#endif
}

} // namespace debug
