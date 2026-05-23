#include "kernel_mode.hpp"
#include "bench.hpp"
#include "debug.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>

namespace {

// One svcGetInfo query, always emitted: raw value on success, Result on fail.
// Most InfoTypes take the current process handle; a few (DebuggerAttached,
// IdleTickCount, RandomEntropy) require handle 0 instead.
u64 getInfo(report::Section& s, const char* name, u32 id, u64 sub = 0,
            Handle handle = CUR_PROCESS_HANDLE) {
    debug::log("[kernel]   svcGetInfo(%s, id=%u, h=0x%08X, sub=%llu)...",
               name, id, handle, (unsigned long long)sub);
    u64 v = 0;
    Result rc = svcGetInfo(&v, id, handle, sub);
    debug::log("[kernel]   svcGetInfo(%s) -> rc=0x%08X v=0x%llx",
               name, rc, (unsigned long long)v);
    if (R_SUCCEEDED(rc))
        s.info(name, "0x%llx  (%llu)",
               (unsigned long long)v, (unsigned long long)v);
    else
        s.error(name, rc);
    return v;
}

} // namespace

void KernelMode::seedSkeleton() {
    {
        report::Section& s = report_.add("svcGetInfo InfoType Sweep");
        s.info("CoreMask (0)",                "...");
        s.info("CoreMask non-zero",           "...");
        s.info("PriorityMask (1)",            "...");
        s.info("AliasRegionAddress (2)",      "...");
        s.info("AliasRegionSize (3)",         "...");
        s.info("HeapRegionAddress (4)",       "...");
        s.info("HeapRegionSize (5)",          "...");
        s.info("TotalMemorySize (6)",         "...");
        s.info("TotalMemorySize non-zero",    "...");
        s.info("UsedMemorySize (7)",          "...");
        s.info("DebuggerAttached (8)",        "...");
        s.info("AslrRegionAddress (12)",      "...");
        s.info("AslrRegionSize (13)",         "...");
        s.info("StackRegionAddress (14)",     "...");
        s.info("StackRegionSize (15)",        "...");
        s.info("SystemResourceSizeTotal (16)","...");
        s.info("SystemResourceSizeUsed (17)", "...");
        s.info("ProgramId (18)",              "...");
        s.info("UserExceptionContext (20)",   "...");
        s.info("TotalNonSystemMemory (21)",   "...");
        s.info("UsedNonSystemMemory (22)",    "...");
        s.info("IsApplication (23)",          "...");
        s.info("FreeThreadCount (24)",        "...");
    }
    {
        report::Section& s = report_.add("Process & Thread");
        s.info("Process id",      "...");
        s.info("Thread id",       "...");
        s.info("Thread priority", "...");
        s.info("Running on core", "...");
        s.info("Program id",      "...");
    }
    {
        report::Section& s = report_.add("System Tick");
        s.info("Tick frequency",          "...");
        s.info("svc/arm tick coherent",   "...");
        s.info("Tick monotonic",          "...");
        s.info("Uptime",                  "...");
        s.info("Idle ticks (this core)",  "...");
    }
    {
        report::Section& s = report_.add("libnx Environment");
        s.info("Module type",         "...");
        s.info("Has argv",            "...");
        s.info("Heap override",       "...");
        s.info("Next-load capable",   "...");
        s.info("Own process handle",  "...");
        s.info("Main thread handle",  "...");
    }
    {
        report::Section& s = report_.add("Entropy");
        for (int i = 0; i < 4; i++) {
            char k[24];
            snprintf(k, sizeof(k), "RandomEntropy[%d]", i);
            s.info(k, "...");
        }
        s.info("Entropy words distinct",  "...");
        s.info("randomGet bit balance",   "...");
    }
}

void KernelMode::run() {
    debug::log("[kernel] run() begin");
    // --- svcGetInfo InfoType sweep --------------------------------------
    {
        debug::log("[kernel] svcGetInfo InfoType Sweep: section begin");
        report::Section& s = report_.add("svcGetInfo InfoType Sweep");
        u64 mask = getInfo(s, "CoreMask (0)",            InfoType_CoreMask);
        s.check("CoreMask non-zero", mask != 0, "0x%llx", (unsigned long long)mask);
        getInfo(s, "PriorityMask (1)",                   InfoType_PriorityMask);
        getInfo(s, "AliasRegionAddress (2)",             InfoType_AliasRegionAddress);
        getInfo(s, "AliasRegionSize (3)",                InfoType_AliasRegionSize);
        getInfo(s, "HeapRegionAddress (4)",              InfoType_HeapRegionAddress);
        getInfo(s, "HeapRegionSize (5)",                 InfoType_HeapRegionSize);
        u64 total = getInfo(s, "TotalMemorySize (6)",    InfoType_TotalMemorySize);
        s.check("TotalMemorySize non-zero", total != 0, "%llu bytes",
                (unsigned long long)total);
        getInfo(s, "UsedMemorySize (7)",                 InfoType_UsedMemorySize);
        getInfo(s, "DebuggerAttached (8)",               InfoType_DebuggerAttached, 0, 0);
        getInfo(s, "AslrRegionAddress (12)",             InfoType_AslrRegionAddress);
        getInfo(s, "AslrRegionSize (13)",                InfoType_AslrRegionSize);
        getInfo(s, "StackRegionAddress (14)",            InfoType_StackRegionAddress);
        getInfo(s, "StackRegionSize (15)",               InfoType_StackRegionSize);
        getInfo(s, "SystemResourceSizeTotal (16)",       InfoType_SystemResourceSizeTotal);
        getInfo(s, "SystemResourceSizeUsed (17)",        InfoType_SystemResourceSizeUsed);
        getInfo(s, "ProgramId (18)",                     InfoType_ProgramId);
        getInfo(s, "UserExceptionContext (20)",          InfoType_UserExceptionContextAddress);
        getInfo(s, "TotalNonSystemMemory (21)",          InfoType_TotalNonSystemMemorySize);
        getInfo(s, "UsedNonSystemMemory (22)",           InfoType_UsedNonSystemMemorySize);
        getInfo(s, "IsApplication (23)",                 InfoType_IsApplication);
        getInfo(s, "FreeThreadCount (24)",               InfoType_FreeThreadCount);
        debug::log("[kernel] svcGetInfo InfoType Sweep: section end");
    }

    // --- Process / thread identity --------------------------------------
    {
        debug::log("[kernel] Process & Thread: section begin");
        report::Section& s = report_.add("Process & Thread");
        debug::log("[kernel]   svcGetProcessId(CUR_PROCESS_HANDLE)...");
        u64 pid = 0;
        Result prc = svcGetProcessId(&pid, CUR_PROCESS_HANDLE);
        debug::log("[kernel]   svcGetProcessId -> rc=0x%08X pid=%llu",
                   prc, (unsigned long long)pid);
        if (R_SUCCEEDED(prc)) s.check("Process id", pid != 0, "%llu",
                                      (unsigned long long)pid);
        else                  s.error("Process id", prc);

        debug::log("[kernel]   svcGetThreadId(CUR_THREAD_HANDLE)...");
        u64 tid = 0;
        Result trc = svcGetThreadId(&tid, CUR_THREAD_HANDLE);
        debug::log("[kernel]   svcGetThreadId -> rc=0x%08X tid=%llu",
                   trc, (unsigned long long)tid);
        if (R_SUCCEEDED(trc)) s.check("Thread id", true, "%llu",
                                      (unsigned long long)tid);
        else                  s.error("Thread id", trc);

        debug::log("[kernel]   svcGetThreadPriority(CUR_THREAD_HANDLE)...");
        s32 prio = 0;
        Result rrc = svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        debug::log("[kernel]   svcGetThreadPriority -> rc=0x%08X prio=%d",
                   rrc, prio);
        if (R_SUCCEEDED(rrc)) s.expect("Thread priority", prio, 0, 63, "");
        else                  s.error("Thread priority", rrc);

        debug::log("[kernel]   svcGetCurrentProcessorNumber...");
        u32 core = svcGetCurrentProcessorNumber();
        debug::log("[kernel]   svcGetCurrentProcessorNumber -> %u", core);
        s.check("Running on core", core < 4, "core %u", core);

        debug::log("[kernel]   svcGetInfo(ProgramId)...");
        u64 progId = 0;
        Result grc = svcGetInfo(&progId, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
        debug::log("[kernel]   svcGetInfo(ProgramId) -> rc=0x%08X id=0x%016llX",
                   grc, (unsigned long long)progId);
        if (R_SUCCEEDED(grc)) s.info("Program id", "%016llX",
                                     (unsigned long long)progId);
        else                  s.error("Program id", grc);
        debug::log("[kernel] Process & Thread: section end");
    }

    // --- System tick -----------------------------------------------------
    {
        debug::log("[kernel] System Tick: section begin");
        report::Section& s = report_.add("System Tick");
        debug::log("[kernel]   svcGetSystemTick / armGetSystemTick / armGetSystemTickFreq");
        u64 svcTick = svcGetSystemTick();
        u64 armTick = armGetSystemTick();
        u64 freq    = armGetSystemTickFreq();
        debug::log("[kernel]   svcTick=%llu armTick=%llu freq=%llu",
                   (unsigned long long)svcTick, (unsigned long long)armTick,
                   (unsigned long long)freq);

        s.exact("Tick frequency", (double)freq, 19200000.0, "Hz");
        // svc and arm tick read the same physical counter; sampled in order,
        // arm must be >= svc and only a few ticks ahead.
        s.check("svc/arm tick coherent", armTick >= svcTick &&
                                         (armTick - svcTick) < 1000000,
                "arm-svc delta %lld ticks", (long long)(armTick - svcTick));

        debug::log("[kernel]   tick advance spin...");
        // Spin until the counter moves: back-to-back reads are faster than
        // one tick and would otherwise read equal.
        u64 a = armGetSystemTick(), b = a;
        for (int i = 0; i < 2000000 && b == a; i++) b = armGetSystemTick();
        debug::log("[kernel]   tick advance -> %s (delta=%llu)",
                   b > a ? "ok" : "STALLED", (unsigned long long)(b - a));
        s.check("Tick monotonic", b > a, "counter strictly increasing");
        s.num("Uptime", (double)armTicksToNs(armTick) / 1e9, "s");

        // IdleTickCount requires handle 0; id1 selects the core.
        debug::log("[kernel]   svcGetInfo(IdleTickCount, handle=0, core=%u)...",
                   svcGetCurrentProcessorNumber());
        u64 idle = 0;
        Result rc = svcGetInfo(&idle, InfoType_IdleTickCount, 0,
                               svcGetCurrentProcessorNumber());
        debug::log("[kernel]   svcGetInfo(IdleTickCount) -> rc=0x%08X idle=%llu",
                   rc, (unsigned long long)idle);
        if (R_SUCCEEDED(rc)) s.info("Idle ticks (this core)", "%llu",
                                    (unsigned long long)idle);
        else                 s.error("Idle ticks", rc);
        debug::log("[kernel] System Tick: section end");
    }

    // --- libnx environment ----------------------------------------------
    {
        debug::log("[kernel] libnx Environment: section begin");
        report::Section& s = report_.add("libnx Environment");
        debug::log("[kernel]   envIsNso/envHasArgv/envHasHeapOverride/envHasNextLoad...");
        s.check("Module type", true, "%s",
                envIsNso() ? "NSO (installed title)" : "NRO (homebrew loader)");
        s.info("Has argv", "%s", envHasArgv() ? "yes" : "no");
        s.info("Heap override", "%s", envHasHeapOverride() ? "yes" : "no");
        s.info("Next-load capable", "%s", envHasNextLoad() ? "yes" : "no");
        debug::log("[kernel]   envGetOwnProcessHandle/envGetMainThreadHandle...");
        Handle own = envGetOwnProcessHandle();
        Handle mt  = envGetMainThreadHandle();
        debug::log("[kernel]   own=0x%08X mainThread=0x%08X", own, mt);
        s.check("Own process handle", own != INVALID_HANDLE, "0x%08X", own);
        s.check("Main thread handle", mt != INVALID_HANDLE, "0x%08X", mt);
        debug::log("[kernel] libnx Environment: section end");
    }

    // --- Entropy ---------------------------------------------------------
    {
        debug::log("[kernel] Entropy: section begin");
        report::Section& s = report_.add("Entropy");
        u64 ent[4] = {};
        bool allOk = true;
        for (int i = 0; i < 4; i++) {
            debug::log("[kernel]   svcGetInfo(RandomEntropy, sub=%d)...", i);
            Result rc = svcGetInfo(&ent[i], InfoType_RandomEntropy, 0, i);
            debug::log("[kernel]   svcGetInfo(RandomEntropy[%d]) -> rc=0x%08X v=0x%016llx",
                       i, rc, (unsigned long long)ent[i]);
            char key[24];
            snprintf(key, sizeof(key), "RandomEntropy[%d]", i);
            if (R_SUCCEEDED(rc)) s.info(key, "0x%016llx",
                                        (unsigned long long)ent[i]);
            else { s.error(key, rc); allOk = false; }
        }
        // Genuine entropy: the four words must not be identical.
        if (allOk) {
            bool distinct = !(ent[0] == ent[1] && ent[1] == ent[2] && ent[2] == ent[3]);
            s.check("Entropy words distinct", distinct,
                    "%s", distinct ? "four differing values"
                                   : "all four words identical");
        }

        debug::log("[kernel]   randomGet(256 bytes)...");
        u8 buf[256];
        randomGet(buf, sizeof(buf));
        int ones = 0;
        for (u8 b : buf) ones += __builtin_popcount(b);
        double ratio = ones / (double)(sizeof(buf) * 8);
        debug::log("[kernel]   randomGet bit ratio = %.3f", ratio);
        s.expect("randomGet bit balance", ratio * 100.0, 42.0, 58.0, "%");
        debug::log("[kernel] Entropy: section end");
    }
    debug::log("[kernel] run() end");
}
