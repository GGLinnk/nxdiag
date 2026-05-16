#include "cpu_mode.hpp"
#include "bench.hpp"
#include <switch.h>
#include <arm_neon.h>
#include <cstdio>

namespace {

// A global volatile sink keeps the optimiser from deleting benchmark loops.
volatile u64   g_sinkU = 0;
volatile float g_sinkF = 0.0f;

// --- benchmark kernels ------------------------------------------------------
// Each kernel does a fixed, known amount of arithmetic per iteration so the
// measured wall time converts straight to ops/s.

constexpr int kIntOpsPerIter  = 6;   // mul, add, shr, xor, add, xor
constexpr int kFpFlopsPerIter = 4;   // two fused multiply-adds
constexpr int kNeonFlopsPerIt = 16;  // two 4-wide multiply-adds

__attribute__((noinline)) u64 intKernel(u64 iters) {
    u64 a = 0x0123456789ABCDEFull, b = 0x9E3779B97F4A7C15ull, acc = 1;
    for (u64 i = 0; i < iters; i++) {
        acc += a * b;
        acc ^= acc >> 13;
        a   += acc;
        b   ^= a;
    }
    return acc;
}

__attribute__((noinline)) double fpKernel(u64 iters) {
    double a = 1.0000000113, b = 0.9999999971, acc = 1.0;
    for (u64 i = 0; i < iters; i++) {
        acc = acc * a + b;
        acc = acc * b + a;
    }
    return acc;
}

__attribute__((noinline)) float neonKernel(u64 iters) {
    float32x4_t a = vdupq_n_f32(1.0000001f);
    float32x4_t b = vdupq_n_f32(0.9999998f);
    float32x4_t acc = vdupq_n_f32(1.0f);
    for (u64 i = 0; i < iters; i++) {
        acc = vmlaq_f32(b, acc, a);
        acc = vmlaq_f32(a, acc, b);
    }
    return vgetq_lane_f32(acc, 0);
}

// --- worker thread for the multi-core scaling test --------------------------
struct Worker {
    u64    iters;
    Thread thread;
};

void intWorkerEntry(void* p) {
    Worker* w = (Worker*)p;
    g_sinkU += intKernel(w->iters);
}

// Run intKernel for `iters` and return millions of integer ops per second.
double measureIntMops(u64 iters) {
    u64 t0 = bench::tick();
    g_sinkU += intKernel(iters);
    double sec = bench::since(t0);
    if (sec <= 0.0) return 0.0;
    return (double)iters * kIntOpsPerIter / sec / 1.0e6;
}

} // namespace

void CpuMode::run() {
    // --- Topology --------------------------------------------------------
    int cores[4];
    int nCores = 0;
    s32 mainPrio = 0x2C;
    {
        report::Section& s = report_.add("Topology");

        u64 coreMask = 0;
        Result rc = svcGetInfo(&coreMask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
        if (R_SUCCEEDED(rc)) {
            char list[32]; int n = 0;
            for (int c = 0; c < 4; c++) {
                if (coreMask & (1u << c)) {
                    cores[nCores++] = c;
                    n += snprintf(list + n, sizeof(list) - n, n ? ",%d" : "%d", c);
                }
            }
            s.info("Core mask", "0x%llX  ->  cores %s",
                   (unsigned long long)coreMask, list);
            // Application processes are granted at least 3 of the 4 A57 cores.
            s.atLeast("Usable core count", nCores, 3, "cores");
        } else {
            s.error("Core mask (svcGetInfo)", rc);
        }
        if (nCores == 0) { cores[0] = 0; nCores = 1; }

        u32 cur = svcGetCurrentProcessorNumber();
        s.check("Current core", cur < 4, "running on core %u", cur);

        Result prc = svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
        if (R_SUCCEEDED(prc)) s.expect("Main thread priority", mainPrio, 0, 63, "");
        else                  s.error("Main thread priority", prc);
        s.info("CPU", "ARM Cortex-A57 (ARMv8-A, 64-bit)");
    }

    // --- Clocks ----------------------------------------------------------
    {
        report::Section& s = report_.add("Clocks");
        Result rc = clkrstInitialize();
        s.result("clkrst: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            ClkrstSession sess;
            Result orc = clkrstOpenSession(&sess, PcvModuleId_CpuBus, 3);
            if (R_SUCCEEDED(orc)) {
                u32 hz = 0;
                Result grc = clkrstGetClockRate(&sess, &hz);
                if (R_SUCCEEDED(grc))
                    s.expect("CPU clock", hz / 1.0e6, 70.0, 2500.0, "MHz");
                else
                    s.error("CPU clock", grc);

                u32 rates[24];
                s32 count = 0;
                PcvClockRatesListType type;
                Result rrc = clkrstGetPossibleClockRates(&sess, rates, 24, &type, &count);
                if (R_SUCCEEDED(rrc) && count > 0) {
                    s.expect("Max CPU clock", rates[count - 1] / 1.0e6,
                             700.0, 2500.0, "MHz");
                    s.num("Min CPU clock", rates[0] / 1.0e6, "MHz");
                    s.atLeast("Clock step count", count, 2, "");
                } else {
                    s.error("Possible clock rates", rrc);
                }
                clkrstCloseSession(&sess);
            } else {
                s.error("clkrst open session (CpuBus)", orc);
            }
            clkrstExit();
        }
    }

    // --- Counter resolution ---------------------------------------------
    {
        report::Section& s = report_.add("Counter Resolution");
        u64 minDelta = ~0ull;
        u64 prev = bench::tick();
        for (int i = 0; i < 200000; i++) {
            u64 t = bench::tick();
            u64 d = t - prev;
            if (d > 0 && d < minDelta) minDelta = d;
            prev = t;
        }
        if (minDelta == ~0ull) minDelta = 0;
        s.atLeast("Min tick delta observed", (double)minDelta, 1, "ticks");
        // The 19.2 MHz counter should resolve to well under a microsecond.
        s.expect("Effective resolution", (double)bench::toNs(minDelta),
                 0.0, 2000.0, "ns");
    }

    // --- Single-core throughput -----------------------------------------
    double baseIntMops = 0.0;
    {
        report::Section& s = report_.add("Single-Core Throughput");

        const u64 intIters = 80ull * 1000 * 1000;
        baseIntMops = measureIntMops(intIters);
        s.atLeast("Integer", baseIntMops, 100.0, "Mops/s");

        const u64 fpIters = 80ull * 1000 * 1000;
        u64 t0 = bench::tick();
        g_sinkF += (float)fpKernel(fpIters);
        double fpSec = bench::since(t0);
        double fpMflops = fpSec > 0 ? (double)fpIters * kFpFlopsPerIter / fpSec / 1e6 : 0;
        s.atLeast("Double-precision FP", fpMflops, 30.0, "MFLOP/s");

        const u64 neonIters = 50ull * 1000 * 1000;
        t0 = bench::tick();
        g_sinkF += neonKernel(neonIters);
        double neonSec = bench::since(t0);
        double neonMflops = neonSec > 0
            ? (double)neonIters * kNeonFlopsPerIt / neonSec / 1e6 : 0;
        s.atLeast("NEON SIMD (f32x4)", neonMflops, 80.0, "MFLOP/s");
        // NEON moves 4 lanes per op, so it must beat scalar double FP.
        s.check("NEON faster than scalar FP", neonMflops > fpMflops,
                "%.0f vs %.0f MFLOP/s", neonMflops, fpMflops);
    }

    // --- Per-core throughput --------------------------------------------
    {
        report::Section& s = report_.add("Per-Core Throughput");
        const u64 iters = 80ull * 1000 * 1000;
        double coreMops[4] = {};
        int measured = 0;
        for (int i = 0; i < nCores; i++) {
            int core = cores[i];
            Worker w{iters, {}};
            Result rc = threadCreate(&w.thread, intWorkerEntry, &w,
                                     nullptr, 128 * 1024, mainPrio, core);
            if (R_FAILED(rc)) {
                char key[24];
                snprintf(key, sizeof(key), "Core %d thread", core);
                s.error(key, rc);
                continue;
            }
            u64 t0 = bench::tick();
            threadStart(&w.thread);
            threadWaitForExit(&w.thread);
            double sec = bench::since(t0);
            threadClose(&w.thread);

            double mops = sec > 0 ? (double)iters * kIntOpsPerIter / sec / 1e6 : 0;
            coreMops[measured++] = mops;
            char key[20];
            snprintf(key, sizeof(key), "Core %d integer", core);
            s.atLeast(key, mops, 100.0, "Mops/s");
        }
        // The A57 cluster is symmetric: every core should perform alike.
        if (measured >= 2) {
            double lo = coreMops[0], hi = coreMops[0];
            for (int i = 1; i < measured; i++) {
                if (coreMops[i] < lo) lo = coreMops[i];
                if (coreMops[i] > hi) hi = coreMops[i];
            }
            double spread = lo > 0 ? hi / lo : 99.0;
            s.check("Core symmetry", spread < 1.5,
                    "fastest/slowest core ratio %.2fx", spread);
        }
    }

    // --- Multi-core scaling ---------------------------------------------
    {
        report::Section& s = report_.add("Multi-Core Scaling");
        const u64 iters = 80ull * 1000 * 1000;

        Worker workers[4];
        int started = 0;
        u64 t0 = bench::tick();
        for (int i = 0; i < nCores; i++) {
            workers[i].iters = iters;
            Result rc = threadCreate(&workers[i].thread, intWorkerEntry, &workers[i],
                                     nullptr, 128 * 1024, mainPrio, cores[i]);
            if (R_SUCCEEDED(rc)) { threadStart(&workers[i].thread); started++; }
        }
        for (int i = 0; i < started; i++)
            threadWaitForExit(&workers[i].thread);
        double sec = bench::since(t0);
        for (int i = 0; i < started; i++)
            threadClose(&workers[i].thread);

        double totalMops = sec > 0
            ? (double)iters * started * kIntOpsPerIter / sec / 1e6 : 0;
        s.check("All worker threads launched", started == nCores,
                "%d of %d threads started", started, nCores);
        s.atLeast("Aggregate integer", totalMops, 100.0, "Mops/s");
        if (baseIntMops > 0 && started > 0) {
            double scaling = totalMops / baseIntMops;
            // True multi-core execution should scale near-linearly; anything
            // below 1.5x with 3+ cores means the threads are not parallel.
            s.check("Scaling factor", scaling >= started * 0.6,
                    "%.2fx over %d cores", scaling, started);
        } else {
            s.missing("Scaling factor", "no single-core baseline to compare");
        }
    }
}
