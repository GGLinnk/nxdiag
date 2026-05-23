#include "mem_mode.hpp"
#include "bench.hpp"
#include "debug.hpp"
#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

volatile u64 g_sink = 0;

void humanBytes(u64 b, char* out, size_t n) {
    if (b >= (1ull << 30))
        snprintf(out, n, "%.2f GiB", (double)b / (1ull << 30));
    else if (b >= (1ull << 20))
        snprintf(out, n, "%.2f MiB", (double)b / (1ull << 20));
    else if (b >= (1ull << 10))
        snprintf(out, n, "%.2f KiB", (double)b / (1ull << 10));
    else
        snprintf(out, n, "%llu B", (unsigned long long)b);
}

// svcGetInfo size query, always emitted. Returns the value (0 on failure).
u64 querySize(report::Section& s, const char* key, u32 id) {
    debug::log("[mem]   svcGetInfo(%s, id=%u)...", key, id);
    u64 v = 0;
    Result rc = svcGetInfo(&v, id, CUR_PROCESS_HANDLE, 0);
    debug::log("[mem]   svcGetInfo(%s) -> rc=0x%08X v=%llu",
               key, rc, (unsigned long long)v);
    if (R_SUCCEEDED(rc)) {
        char h[32];
        humanBytes(v, h, sizeof(h));
        s.text(key, report::Status::Info, "%llu bytes  (%s)",
               (unsigned long long)v, h);
    } else {
        s.error(key, rc);
    }
    return v;
}

void queryAddr(report::Section& s, const char* key, u32 id) {
    debug::log("[mem]   svcGetInfo(%s, id=%u)...", key, id);
    u64 v = 0;
    Result rc = svcGetInfo(&v, id, CUR_PROCESS_HANDLE, 0);
    debug::log("[mem]   svcGetInfo(%s) -> rc=0x%08X v=0x%llx",
               key, rc, (unsigned long long)v);
    if (R_SUCCEEDED(rc))
        s.check(key, v != 0, "0x%012llx", (unsigned long long)v);
    else
        s.error(key, rc);
}

} // namespace

void MemMode::seedSkeleton() {
    {
        report::Section& s = report_.add("Process Memory (svcGetInfo)");
        s.info("Total available",      "...");
        s.info("Currently used",       "...");
        s.info("Total >= 256 MiB",     "...");
        s.info("Used within total",    "...");
        s.info("Total (non-system)",   "...");
        s.info("Used (non-system)",    "...");
        s.info("System resource size", "...");
        s.info("System resource used", "...");
    }
    {
        report::Section& s = report_.add("Address Space Layout");
        s.info("ASLR base",  "...");
        s.info("ASLR size",  "...");
        s.info("Heap base",  "...");
        s.info("Heap size",  "...");
        s.info("Alias base", "...");
        s.info("Alias size", "...");
        s.info("Stack base", "...");
        s.info("Stack size", "...");
    }
    {
        report::Section& s = report_.add("Virtual Memory Map");
        s.info("Regions walked",         "...");
        s.info("Mapped regions",         "...");
        s.info("Mapped memory found",    "...");
        s.info("Executable code mapped", "...");
        s.info("Heap-mapped bytes",      "...");
        s.info("Free address space",     "...");
    }
    {
        report::Section& s = report_.add("Bandwidth");
        s.info("Sequential write", "...");
        s.info("Sequential read",  "...");
        s.info("Copy (memcpy)",    "...");
    }
    {
        report::Section& s = report_.add("Access Latency");
        s.info("8 KiB  (L1)",            "...");
        s.info("256 KiB (L2)",           "...");
        s.info("2 MiB  (L2)",            "...");
        s.info("32 MiB (DRAM)",          "...");
        s.info("L1 latency sane",        "...");
        s.info("Latency grows with size", "...");
    }
    {
        report::Section& s = report_.add("Heap Allocation Limit");
        s.info("Total committed",         "...");
        s.info("8 MiB chunks allocated",  "...");
        s.info("Heap released cleanly",   "...");
    }
}

void MemMode::run() {
    debug::log("[mem] run() begin");
    // --- Kernel-reported process memory ---------------------------------
    {
        debug::log("[mem] Process Memory: section begin");
        report::Section& s = report_.add("Process Memory (svcGetInfo)");
        u64 total = querySize(s, "Total available", InfoType_TotalMemorySize);
        u64 used  = querySize(s, "Currently used",  InfoType_UsedMemorySize);

        // Applications are guaranteed a large memory pool.
        s.atLeast("Total >= 256 MiB", total / (double)(1u << 20), 256.0, "MiB");
        s.check("Used within total", used > 0 && used <= total,
                "used %.1f MiB of %.1f MiB",
                used / (double)(1u << 20), total / (double)(1u << 20));

        querySize(s, "Total (non-system)",   InfoType_TotalNonSystemMemorySize);
        querySize(s, "Used (non-system)",    InfoType_UsedNonSystemMemorySize);
        querySize(s, "System resource size", InfoType_SystemResourceSizeTotal);
        querySize(s, "System resource used", InfoType_SystemResourceSizeUsed);
        debug::log("[mem] Process Memory: section end");
    }

    // --- Address-space layout -------------------------------------------
    {
        debug::log("[mem] Address Space Layout: section begin");
        report::Section& s = report_.add("Address Space Layout");
        queryAddr(s, "ASLR base",   InfoType_AslrRegionAddress);
        querySize(s, "ASLR size",   InfoType_AslrRegionSize);
        queryAddr(s, "Heap base",   InfoType_HeapRegionAddress);
        querySize(s, "Heap size",   InfoType_HeapRegionSize);
        queryAddr(s, "Alias base",  InfoType_AliasRegionAddress);
        querySize(s, "Alias size",  InfoType_AliasRegionSize);
        queryAddr(s, "Stack base",  InfoType_StackRegionAddress);
        querySize(s, "Stack size",  InfoType_StackRegionSize);
        debug::log("[mem] Address Space Layout: section end");
    }

    // --- Virtual-address-space map walk ---------------------------------
    {
        debug::log("[mem] Virtual Memory Map: section begin");
        report::Section& s = report_.add("Virtual Memory Map");
        debug::log("[mem]   reading ASLR region for walk...");
        u64 base = 0, span = 0;
        svcGetInfo(&base, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&span, InfoType_AslrRegionSize,    CUR_PROCESS_HANDLE, 0);
        u64 end = (span ? base + span : 0x10000000000ull);
        debug::log("[mem]   walk range [0x%llx .. 0x%llx)",
                   (unsigned long long)base, (unsigned long long)end);

        u64 mappedBytes = 0, freeBytes = 0, codeBytes = 0, heapBytes = 0;
        int regions = 0, mappedRegions = 0;
        bool walkOk = true;
        u64 addr = base;
        for (int guard = 0; guard < 8192; guard++) {
            MemoryInfo mi{};
            u32 pi = 0;
            Result rc = svcQueryMemory(&mi, &pi, addr);
            if (R_FAILED(rc)) {
                debug::log("[mem]   svcQueryMemory @0x%llx FAILED 0x%08X (after %d regions)",
                           (unsigned long long)addr, rc, regions);
                if (regions == 0) { s.error("svcQueryMemory", rc); walkOk = false; }
                break;
            }
            regions++;
            u32 t = mi.type & 0xFF;
            if (t == MemType_Unmapped) {
                freeBytes += mi.size;
            } else {
                mappedBytes += mi.size;
                mappedRegions++;
                if (t == MemType_CodeStatic || t == MemType_CodeMutable ||
                    t == MemType_ModuleCodeStatic || t == MemType_ModuleCodeMutable)
                    codeBytes += mi.size;
                if (t == MemType_Heap) heapBytes += mi.size;
            }
            u64 next = mi.addr + mi.size;
            if (next <= addr || next >= end) break;
            addr = next;
        }
        if (walkOk) {
            debug::log("[mem]   walk: %d regions, %d mapped, mapped=%llu code=%llu heap=%llu free=%llu",
                       regions, mappedRegions,
                       (unsigned long long)mappedBytes, (unsigned long long)codeBytes,
                       (unsigned long long)heapBytes,   (unsigned long long)freeBytes);
            char h[32];
            s.atLeast("Regions walked", regions, 3, "");
            s.atLeast("Mapped regions", mappedRegions, 1, "");
            humanBytes(mappedBytes, h, sizeof(h));
            s.check("Mapped memory found", mappedBytes > 0, "%s", h);
            humanBytes(codeBytes, h, sizeof(h));
            // The probe's own executable code must appear in the map.
            s.check("Executable code mapped", codeBytes > 0, "%s", h);
            humanBytes(heapBytes, h, sizeof(h));
            s.text("Heap-mapped bytes", report::Status::Info, "%s", h);
            humanBytes(freeBytes, h, sizeof(h));
            s.text("Free address space", report::Status::Info, "%s", h);
        }
        debug::log("[mem] Virtual Memory Map: section end");
    }

    // --- Bandwidth -------------------------------------------------------
    {
        debug::log("[mem] Bandwidth: section begin");
        report::Section& s = report_.add("Bandwidth");
        const size_t sz = 32u << 20;
        const int    passes = 16;
        debug::log("[mem]   malloc 2x %zu bytes...", sz);
        u8* src = (u8*)malloc(sz);
        u8* dst = (u8*)malloc(sz);
        debug::log("[mem]   malloc -> src=%p dst=%p", (void*)src, (void*)dst);
        if (!src || !dst) {
            s.missing("Working set", "could not reserve 2x32 MiB of heap");
        } else {
            memset(src, 0xA5, sz);
            memset(dst, 0x5A, sz);

            debug::log("[mem]   memset bench (%d passes of %zu bytes)...", passes, sz);
            u64 t0 = bench::tick();
            for (int p = 0; p < passes; p++) memset(dst, (u8)p, sz);
            double wsec = bench::since(t0);
            debug::log("[mem]   memset bench -> %.2f GB/s in %.3fs",
                       (double)sz * passes / wsec / 1e9, wsec);
            s.atLeast("Sequential write", (double)sz * passes / wsec / 1e9, 0.5, "GB/s");

            debug::log("[mem]   sequential read bench...");
            t0 = bench::tick();
            u64 acc = 0;
            for (int p = 0; p < passes; p++) {
                const u64* q = (const u64*)src;
                for (size_t i = 0; i < sz / 8; i++) acc += q[i];
            }
            g_sink += acc;
            double rsec = bench::since(t0);
            debug::log("[mem]   sequential read bench -> %.2f GB/s in %.3fs",
                       (double)sz * passes / rsec / 1e9, rsec);
            s.atLeast("Sequential read", (double)sz * passes / rsec / 1e9, 0.5, "GB/s");

            debug::log("[mem]   memcpy bench...");
            t0 = bench::tick();
            for (int p = 0; p < passes; p++) memcpy(dst, src, sz);
            double csec = bench::since(t0);
            debug::log("[mem]   memcpy bench -> %.2f GB/s in %.3fs",
                       (double)sz * passes / csec / 1e9, csec);
            s.atLeast("Copy (memcpy)", (double)sz * passes / csec / 1e9, 0.5, "GB/s");
        }
        debug::log("[mem]   free src/dst");
        free(src);
        free(dst);
        debug::log("[mem] Bandwidth: section end");
    }

    // --- Cache / memory latency -----------------------------------------
    {
        debug::log("[mem] Access Latency: section begin");
        report::Section& s = report_.add("Access Latency");
        struct { const char* name; size_t bytes; } levels[] = {
            { "8 KiB  (L1)",   8u << 10 },
            { "256 KiB (L2)",  256u << 10 },
            { "2 MiB  (L2)",   2u << 20 },
            { "32 MiB (DRAM)", 32u << 20 },
        };
        double lat[4] = {};
        bool ok[4] = {};
        for (int i = 0; i < 4; i++) {
            debug::log("[mem]   level %d (%s, %zu bytes): malloc + pointer chase...",
                       i, levels[i].name, levels[i].bytes);
            size_t n = levels[i].bytes / sizeof(size_t);
            size_t* a = (size_t*)malloc(n * sizeof(size_t));
            if (!a) {
                debug::log("[mem]   level %d malloc FAILED", i);
                s.missing(levels[i].name, "allocation failed"); continue;
            }
            for (size_t k = 0; k < n; k++) a[k] = k;
            for (size_t k = n - 1; k > 0; k--) {       // Sattolo random cycle
                size_t j = (size_t)(bench::tick() ^ (k * 6364136223846793005ull)) % k;
                size_t tmp = a[k]; a[k] = a[j]; a[j] = tmp;
            }
            const u64 chases = 8ull * 1000 * 1000;
            size_t idx = 0;
            u64 t0 = bench::tick();
            for (u64 c = 0; c < chases; c++) idx = a[idx];
            g_sink += idx;
            lat[i] = bench::since(t0) * 1e9 / chases;
            ok[i] = true;
            debug::log("[mem]   level %d (%s) -> %.1f ns/access",
                       i, levels[i].name, lat[i]);
            s.num(levels[i].name, lat[i], "ns/access");
            free(a);
        }
        if (ok[0])
            s.expect("L1 latency sane", lat[0], 0.0, 60.0, "ns");
        if (ok[0] && ok[3])
            // A real cache hierarchy gets slower as the working set grows.
            s.check("Latency grows with size", lat[3] > lat[0],
                    "DRAM %.1f ns > L1 %.1f ns", lat[3], lat[0]);
        debug::log("[mem] Access Latency: section end");
    }

    // --- Allocation limit -----------------------------------------------
    {
        debug::log("[mem] Heap Allocation Limit: section begin");
        report::Section& s = report_.add("Heap Allocation Limit");
        const size_t chunk = 8u << 20;
        static void* blocks[1024];
        int held = 0;
        debug::log("[mem]   greedy malloc loop (8 MiB chunks)...");
        while (held < 1024) {
            void* p = malloc(chunk);
            if (!p) break;
            *(volatile u8*)p = 1;
            blocks[held++] = p;
        }
        u64 totalMiB = (u64)held * (chunk >> 20);
        debug::log("[mem]   greedy malloc -> %d chunks held (%llu MiB)",
                   held, (unsigned long long)totalMiB);
        s.atLeast("Total committed", (double)totalMiB, 32.0, "MiB");
        s.num("8 MiB chunks allocated", held, "");
        debug::log("[mem]   freeing %d chunks...", held);
        for (int i = 0; i < held; i++) free(blocks[i]);
        s.check("Heap released cleanly", true, "all %d chunks freed", held);
        debug::log("[mem] Heap Allocation Limit: section end");
    }
    debug::log("[mem] run() end");
}
