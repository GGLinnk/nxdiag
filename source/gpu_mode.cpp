#include "gpu_mode.hpp"
#include "bench.hpp"
#include "debug.hpp"
#include <switch.h>
#include <deko3d.h>
#include <cstdio>
#include <cstring>

namespace {

volatile u64 g_sink = 0;

// Probe one clkrst module, always emitting a verdict: the current rate must
// fall inside the documented Switch range [loMHz, hiMHz].
void probeClock(report::Section& s, const char* label, PcvModuleId mod,
                double loMHz, double hiMHz) {
    debug::log("[gpu]   probeClock(%s): clkrstOpenSession(mod=%d)...", label, (int)mod);
    ClkrstSession sess;
    Result rc = clkrstOpenSession(&sess, mod, 3);
    debug::log("[gpu]   probeClock(%s): clkrstOpenSession -> 0x%08X", label, rc);
    if (R_FAILED(rc)) { s.error(label, rc); return; }

    debug::log("[gpu]   probeClock(%s): clkrstGetClockRate...", label);
    u32 hz = 0;
    Result grc = clkrstGetClockRate(&sess, &hz);
    debug::log("[gpu]   probeClock(%s): clkrstGetClockRate -> rc=0x%08X hz=%u",
               label, grc, hz);
    if (R_SUCCEEDED(grc)) {
        char key[48];
        snprintf(key, sizeof(key), "%s clock", label);
        s.expect(key, hz / 1.0e6, loMHz, hiMHz, "MHz");
    } else {
        s.error(label, grc);
    }

    debug::log("[gpu]   probeClock(%s): clkrstGetPossibleClockRates...", label);
    u32 rates[24];
    s32 count = 0;
    PcvClockRatesListType type;
    Result prc = clkrstGetPossibleClockRates(&sess, rates, 24, &type, &count);
    debug::log("[gpu]   probeClock(%s): clkrstGetPossibleClockRates -> rc=0x%08X count=%d",
               label, prc, (int)count);
    if (R_SUCCEEDED(prc) && count > 0) {
        char key[48];
        snprintf(key, sizeof(key), "%s max clock", label);
        s.expect(key, rates[count - 1] / 1.0e6, loMHz, hiMHz, "MHz");
    }
    debug::log("[gpu]   probeClock(%s): clkrstCloseSession", label);
    clkrstCloseSession(&sess);
}

} // namespace

void GpuMode::seedSkeleton() {
    {
        report::Section& s = report_.add("deko3d Device");
        s.info("dkDeviceCreate",      "...");
        s.info("Device create time",  "...");
        s.info("Graphics API",        "...");
        s.info("GPU",                 "...");
    }
    {
        report::Section& s = report_.add("GPU Memory");
        s.info("1 MiB GPU block",       "...");
        s.info("16 MiB GPU block",      "...");
        s.info("16 MiB CPU->GPU write", "...");
        s.info("64 MiB GPU block",      "...");
        s.info("64 MiB CPU->GPU write", "...");
    }
    {
        report::Section& s = report_.add("GPU Timestamp");
        s.info("Timestamp non-zero",  "...");
        s.info("GPU clock advancing", "...");
    }
    {
        report::Section& s = report_.add("GPU / Memory Clocks");
        s.info("clkrst: initialize",      "...");
        s.info("GPU clock",               "...");
        s.info("GPU max clock",           "...");
        s.info("EMC (memory) clock",      "...");
        s.info("EMC (memory) max clock",  "...");
    }
}

void GpuMode::run() {
    debug::log("[gpu] run() begin");
    // --- deko3d device bring-up -----------------------------------------
    DkDevice device = nullptr;
    {
        debug::log("[gpu] deko3d Device: section begin");
        report::Section& s = report_.add("deko3d Device");
        DkDeviceMaker maker;
        dkDeviceMakerDefaults(&maker);

        debug::log("[gpu]   dkDeviceCreate...");
        u64 t0 = bench::tick();
        device = dkDeviceCreate(&maker);
        double ms = bench::since(t0) * 1e3;
        debug::log("[gpu]   dkDeviceCreate -> %p in %.2f ms", (void*)device, ms);

        s.check("dkDeviceCreate", device != nullptr,
                device ? "GPU device created" : "FAILED to create GPU device");
        if (device) {
            s.expect("Device create time", ms, 0.0, 2000.0, "ms");
            s.info("Graphics API", "deko3d (libnx low-level GPU API)");
            s.info("GPU", "NVIDIA Tegra X1 Maxwell (GM20B)");
        }
        debug::log("[gpu] deko3d Device: section end");
    }

    // --- GPU memory ------------------------------------------------------
    {
        debug::log("[gpu] GPU Memory: section begin");
        report::Section& s = report_.add("GPU Memory");
        if (!device) {
            s.missing("GPU memory probe", "no deko3d device");
        } else {
            const u32 sizes[] = { 1u << 20, 16u << 20, 64u << 20 };
            for (u32 sz : sizes) {
                debug::log("[gpu]   block %u MiB: dkMemBlockCreate...", sz >> 20);
                DkMemBlockMaker mm;
                dkMemBlockMakerDefaults(&mm, device, sz);
                mm.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;

                u64 t0 = bench::tick();
                DkMemBlock blk = dkMemBlockCreate(&mm);
                double ms = bench::since(t0) * 1e3;
                debug::log("[gpu]   block %u MiB: dkMemBlockCreate -> %p in %.2f ms",
                           sz >> 20, (void*)blk, ms);

                char key[40];
                snprintf(key, sizeof(key), "%u MiB GPU block", sz >> 20);
                s.check(key, blk != nullptr,
                        blk ? "allocated in %.2f ms" : "allocation FAILED", ms);
                if (!blk) continue;

                debug::log("[gpu]   block %u MiB: dkMemBlockGetCpuAddr...", sz >> 20);
                void* cpu = dkMemBlockGetCpuAddr(blk);
                debug::log("[gpu]   block %u MiB: cpu=%p", sz >> 20, cpu);
                if (cpu && sz >= (16u << 20)) {
                    debug::log("[gpu]   block %u MiB: CPU->GPU memset bench...", sz >> 20);
                    u64 w0 = bench::tick();
                    memset(cpu, 0x5A, sz);
                    double wsec = bench::since(w0);
                    debug::log("[gpu]   block %u MiB: write %.2f GB/s in %.3fs",
                               sz >> 20, (double)sz / wsec / 1e9, wsec);
                    snprintf(key, sizeof(key), "%u MiB CPU->GPU write", sz >> 20);
                    s.atLeast(key, (double)sz / wsec / 1e9, 0.1, "GB/s");
                    g_sink += *(volatile u8*)cpu;
                } else if (!cpu) {
                    snprintf(key, sizeof(key), "%u MiB CPU mapping", sz >> 20);
                    s.missing(key, "block has no CPU address");
                }
                debug::log("[gpu]   block %u MiB: dkMemBlockDestroy", sz >> 20);
                dkMemBlockDestroy(blk);
            }
        }
        debug::log("[gpu] GPU Memory: section end");
    }

    // --- GPU timestamp clock --------------------------------------------
    {
        debug::log("[gpu] GPU Timestamp: section begin");
        report::Section& s = report_.add("GPU Timestamp");
        if (!device) {
            s.missing("GPU timestamp probe", "no deko3d device");
        } else {
            debug::log("[gpu]   dkDeviceGetCurrentTimestampInNs...");
            u64 ns0 = dkDeviceGetCurrentTimestampInNs(device);
            debug::log("[gpu]   dkDeviceGetCurrentTimestampInNs -> %llu",
                       (unsigned long long)ns0);
            debug::log("[gpu]   100k timestamp reads for min-delta...");
            u64 minDelta = ~0ull;
            u64 prev = dkDeviceGetCurrentTimestamp(device);
            for (int i = 0; i < 100000; i++) {
                u64 t = dkDeviceGetCurrentTimestamp(device);
                u64 d = t - prev;
                if (d > 0 && d < minDelta) minDelta = d;
                prev = t;
            }
            if (minDelta == ~0ull) minDelta = 0;
            debug::log("[gpu]   timestamp min-delta = %llu ticks",
                       (unsigned long long)minDelta);
            s.check("Timestamp non-zero", ns0 > 0, "%llu ns",
                    (unsigned long long)ns0);
            s.check("GPU clock advancing", minDelta > 0,
                    minDelta > 0 ? "min delta %llu ticks" : "counter stalled",
                    (unsigned long long)minDelta);
        }
        debug::log("[gpu] GPU Timestamp: section end");
    }

    if (device) {
        debug::log("[gpu] dkDeviceDestroy");
        dkDeviceDestroy(device);
    }

    // --- Clocks ----------------------------------------------------------
    {
        debug::log("[gpu] GPU / Memory Clocks: section begin");
        report::Section& s = report_.add("GPU / Memory Clocks");
        debug::log("[gpu]   clkrstInitialize...");
        Result rc = clkrstInitialize();
        debug::log("[gpu]   clkrstInitialize -> 0x%08X", rc);
        s.result("clkrst: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            // GM20B: 76.8 MHz idle up to ~1267 MHz; EMC up to ~1600 MHz.
            probeClock(s, "GPU",          PcvModuleId_GPU, 70.0,  1400.0);
            probeClock(s, "EMC (memory)", PcvModuleId_EMC, 200.0, 4200.0);
            debug::log("[gpu]   clkrstExit");
            clkrstExit();
        }
        debug::log("[gpu] GPU / Memory Clocks: section end");
    }
    debug::log("[gpu] run() end");
}
