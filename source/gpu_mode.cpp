#include "gpu_mode.hpp"
#include "bench.hpp"
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
    ClkrstSession sess;
    Result rc = clkrstOpenSession(&sess, mod, 3);
    if (R_FAILED(rc)) { s.error(label, rc); return; }

    u32 hz = 0;
    Result grc = clkrstGetClockRate(&sess, &hz);
    if (R_SUCCEEDED(grc)) {
        char key[48];
        snprintf(key, sizeof(key), "%s clock", label);
        s.expect(key, hz / 1.0e6, loMHz, hiMHz, "MHz");
    } else {
        s.error(label, grc);
    }

    u32 rates[24];
    s32 count = 0;
    PcvClockRatesListType type;
    if (R_SUCCEEDED(clkrstGetPossibleClockRates(&sess, rates, 24, &type, &count))
        && count > 0) {
        char key[48];
        snprintf(key, sizeof(key), "%s max clock", label);
        s.expect(key, rates[count - 1] / 1.0e6, loMHz, hiMHz, "MHz");
    }
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
    // --- deko3d device bring-up -----------------------------------------
    DkDevice device = nullptr;
    {
        report::Section& s = report_.add("deko3d Device");
        DkDeviceMaker maker;
        dkDeviceMakerDefaults(&maker);

        u64 t0 = bench::tick();
        device = dkDeviceCreate(&maker);
        double ms = bench::since(t0) * 1e3;

        s.check("dkDeviceCreate", device != nullptr,
                device ? "GPU device created" : "FAILED to create GPU device");
        if (device) {
            s.expect("Device create time", ms, 0.0, 2000.0, "ms");
            s.info("Graphics API", "deko3d (libnx low-level GPU API)");
            s.info("GPU", "NVIDIA Tegra X1 Maxwell (GM20B)");
        }
    }

    // --- GPU memory ------------------------------------------------------
    {
        report::Section& s = report_.add("GPU Memory");
        if (!device) {
            s.missing("GPU memory probe", "no deko3d device");
        } else {
            const u32 sizes[] = { 1u << 20, 16u << 20, 64u << 20 };
            for (u32 sz : sizes) {
                DkMemBlockMaker mm;
                dkMemBlockMakerDefaults(&mm, device, sz);
                mm.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;

                u64 t0 = bench::tick();
                DkMemBlock blk = dkMemBlockCreate(&mm);
                double ms = bench::since(t0) * 1e3;

                char key[40];
                snprintf(key, sizeof(key), "%u MiB GPU block", sz >> 20);
                s.check(key, blk != nullptr,
                        blk ? "allocated in %.2f ms" : "allocation FAILED", ms);
                if (!blk) continue;

                void* cpu = dkMemBlockGetCpuAddr(blk);
                if (cpu && sz >= (16u << 20)) {
                    u64 w0 = bench::tick();
                    memset(cpu, 0x5A, sz);
                    double wsec = bench::since(w0);
                    snprintf(key, sizeof(key), "%u MiB CPU->GPU write", sz >> 20);
                    s.atLeast(key, (double)sz / wsec / 1e9, 0.1, "GB/s");
                    g_sink += *(volatile u8*)cpu;
                } else if (!cpu) {
                    snprintf(key, sizeof(key), "%u MiB CPU mapping", sz >> 20);
                    s.missing(key, "block has no CPU address");
                }
                dkMemBlockDestroy(blk);
            }
        }
    }

    // --- GPU timestamp clock --------------------------------------------
    {
        report::Section& s = report_.add("GPU Timestamp");
        if (!device) {
            s.missing("GPU timestamp probe", "no deko3d device");
        } else {
            u64 ns0 = dkDeviceGetCurrentTimestampInNs(device);
            u64 minDelta = ~0ull;
            u64 prev = dkDeviceGetCurrentTimestamp(device);
            for (int i = 0; i < 100000; i++) {
                u64 t = dkDeviceGetCurrentTimestamp(device);
                u64 d = t - prev;
                if (d > 0 && d < minDelta) minDelta = d;
                prev = t;
            }
            if (minDelta == ~0ull) minDelta = 0;
            s.check("Timestamp non-zero", ns0 > 0, "%llu ns",
                    (unsigned long long)ns0);
            s.check("GPU clock advancing", minDelta > 0,
                    minDelta > 0 ? "min delta %llu ticks" : "counter stalled",
                    (unsigned long long)minDelta);
        }
    }

    if (device)
        dkDeviceDestroy(device);

    // --- Clocks ----------------------------------------------------------
    {
        report::Section& s = report_.add("GPU / Memory Clocks");
        Result rc = clkrstInitialize();
        s.result("clkrst: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            // GM20B: 76.8 MHz idle up to ~1267 MHz; EMC up to ~1600 MHz.
            probeClock(s, "GPU",          PcvModuleId_GPU, 70.0,  1400.0);
            probeClock(s, "EMC (memory)", PcvModuleId_EMC, 200.0, 4200.0);
            clkrstExit();
        }
    }
}
