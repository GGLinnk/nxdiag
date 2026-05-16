#include "kernel_mode.hpp"
#include "bench.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>

namespace {

// One svcGetInfo query, always emitted: raw value on success, Result on fail.
// Most InfoTypes take the current process handle; a few (DebuggerAttached,
// IdleTickCount, RandomEntropy) require handle 0 instead.
u64 getInfo(report::Section& s, const char* name, u32 id, u64 sub = 0,
            Handle handle = CUR_PROCESS_HANDLE) {
    u64 v = 0;
    Result rc = svcGetInfo(&v, id, handle, sub);
    if (R_SUCCEEDED(rc))
        s.info(name, "0x%llx  (%llu)",
               (unsigned long long)v, (unsigned long long)v);
    else
        s.error(name, rc);
    return v;
}

} // namespace

void KernelMode::run() {
    // --- svcGetInfo InfoType sweep --------------------------------------
    {
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
    }

    // --- Process / thread identity --------------------------------------
    {
        report::Section& s = report_.add("Process & Thread");
        u64 pid = 0;
        Result prc = svcGetProcessId(&pid, CUR_PROCESS_HANDLE);
        if (R_SUCCEEDED(prc)) s.check("Process id", pid != 0, "%llu",
                                      (unsigned long long)pid);
        else                  s.error("Process id", prc);

        u64 tid = 0;
        Result trc = svcGetThreadId(&tid, CUR_THREAD_HANDLE);
        if (R_SUCCEEDED(trc)) s.check("Thread id", true, "%llu",
                                      (unsigned long long)tid);
        else                  s.error("Thread id", trc);

        s32 prio = 0;
        Result rrc = svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        if (R_SUCCEEDED(rrc)) s.expect("Thread priority", prio, 0, 63, "");
        else                  s.error("Thread priority", rrc);

        u32 core = svcGetCurrentProcessorNumber();
        s.check("Running on core", core < 4, "core %u", core);

        u64 progId = 0;
        Result grc = svcGetInfo(&progId, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
        if (R_SUCCEEDED(grc)) s.info("Program id", "%016llX",
                                     (unsigned long long)progId);
        else                  s.error("Program id", grc);
    }

    // --- System tick -----------------------------------------------------
    {
        report::Section& s = report_.add("System Tick");
        u64 svcTick = svcGetSystemTick();
        u64 armTick = armGetSystemTick();
        u64 freq    = armGetSystemTickFreq();

        s.exact("Tick frequency", (double)freq, 19200000.0, "Hz");
        // svc and arm tick read the same physical counter; sampled in order,
        // arm must be >= svc and only a few ticks ahead.
        s.check("svc/arm tick coherent", armTick >= svcTick &&
                                         (armTick - svcTick) < 1000000,
                "arm-svc delta %lld ticks", (long long)(armTick - svcTick));

        // Spin until the counter moves: back-to-back reads are faster than
        // one tick and would otherwise read equal.
        u64 a = armGetSystemTick(), b = a;
        for (int i = 0; i < 2000000 && b == a; i++) b = armGetSystemTick();
        s.check("Tick monotonic", b > a, "counter strictly increasing");
        s.num("Uptime", (double)armTicksToNs(armTick) / 1e9, "s");

        // IdleTickCount requires handle 0; id1 selects the core.
        u64 idle = 0;
        Result rc = svcGetInfo(&idle, InfoType_IdleTickCount, 0,
                               svcGetCurrentProcessorNumber());
        if (R_SUCCEEDED(rc)) s.info("Idle ticks (this core)", "%llu",
                                    (unsigned long long)idle);
        else                 s.error("Idle ticks", rc);
    }

    // --- libnx environment ----------------------------------------------
    {
        report::Section& s = report_.add("libnx Environment");
        s.check("Module type", true, "%s",
                envIsNso() ? "NSO (installed title)" : "NRO (homebrew loader)");
        s.info("Has argv", "%s", envHasArgv() ? "yes" : "no");
        s.info("Heap override", "%s", envHasHeapOverride() ? "yes" : "no");
        s.info("Next-load capable", "%s", envHasNextLoad() ? "yes" : "no");
        Handle own = envGetOwnProcessHandle();
        s.check("Own process handle", own != INVALID_HANDLE, "0x%08X", own);
        Handle mt = envGetMainThreadHandle();
        s.check("Main thread handle", mt != INVALID_HANDLE, "0x%08X", mt);
    }

    // --- Entropy ---------------------------------------------------------
    {
        report::Section& s = report_.add("Entropy");
        u64 ent[4] = {};
        bool allOk = true;
        for (int i = 0; i < 4; i++) {
            Result rc = svcGetInfo(&ent[i], InfoType_RandomEntropy, 0, i);
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

        u8 buf[256];
        randomGet(buf, sizeof(buf));
        int ones = 0;
        for (u8 b : buf) ones += __builtin_popcount(b);
        double ratio = ones / (double)(sizeof(buf) * 8);
        s.expect("randomGet bit balance", ratio * 100.0, 42.0, 58.0, "%");
    }
}
