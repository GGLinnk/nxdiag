// NX Diag - Nintendo Switch homebrew CPU / memory / GPU / system diagnostics.
// Probes the device featureset through libnx and exports a JSON report that
// can be diffed against the same run on an emulator.
//
// Entry point: wires the diagnostic views into a nxd::Runner, with a
// text-console fallback if the framebuffer cannot be brought up.

#include <switch.h>
#include <cstdio>
#include "nxdisplaylib/runner.hpp"
#include "mode.hpp"
#include "menu_mode.hpp"
#include "sysinfo_mode.hpp"
#include "cpu_mode.hpp"
#include "mem_mode.hpp"
#include "gpu_mode.hpp"
#include "storage_mode.hpp"
#include "kernel_mode.hpp"
#include "services_mode.hpp"

using namespace nxd;

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    static MenuMode     menu;
    static SysInfoMode  sysinfo;
    static CpuMode      cpu;
    static MemMode      mem;
    static GpuMode      gpu;
    static StorageMode  storage;
    static KernelMode   kernel;
    static ServicesMode services;

    // View list - index order matches the ModeId enum (Menu = 0).
    static View* views[] = {
        &menu, &sysinfo, &cpu, &mem, &gpu, &storage, &kernel, &services,
    };
    // Probe modules in screen order, for the menu's run-all export.
    static ProbeMode* probes[] = {
        &sysinfo, &cpu, &mem, &gpu, &storage, &kernel, &services,
    };
    menu.setProbes(probes, (int)(sizeof(probes) / sizeof(probes[0])));

    RunnerConfig cfg;
    cfg.homeIndex  = Menu;   // B returns to the menu
    cfg.cycleCount = 0;      // ZL/ZR cycle spans every view
    cfg.showFps    = false;
#ifdef NXD_HOST
    cfg.exitOnHomeBack = true;   // host preview: B/Esc on the menu quits
#endif

    Runner runner;
    if (!runner.init(views, (int)(sizeof(views) / sizeof(views[0])), cfg)) {
        // Fall back to a text console so the failure is visible.
        consoleInit(NULL);
        printf("NX Diag: failed to initialise framebuffer.\n");
        printf("Press + to exit.\n");

        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
        return 1;
    }

    runner.run();
    runner.deinit();
    return 0;
}
