// NX Diag - Nintendo Switch homebrew CPU / memory / GPU / system diagnostics.
// Probes the device featureset through libnx and exports a JSON report that
// can be diffed against the same run on an emulator.
//
// Entry point: wires the diagnostic views into a nxd::Runner, with a
// text-console fallback if the framebuffer cannot be brought up.

#include <switch.h>
#include <cstdio>
#include <strings.h>
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
#include "debug.hpp"

using namespace nxd;

// --- launch command line -----------------------------------------------------
// exportMode: kExportNone leaves the export alone; kExportAll probes every
// module; any other value is a single module's view index.
enum { kExportNone = -2, kExportAll = -1 };

struct Options {
    int startMode  = Menu;         // view to open on launch
    int exportMode = kExportNone;  // module(s) to JSON-export on launch
};

// Map a module name to its view index, or -1 if it is not a module name.
static int moduleByName(const char* name) {
    if (!strcasecmp(name, "sysinfo"))  return SysInfo;
    if (!strcasecmp(name, "cpu"))      return Cpu;
    if (!strcasecmp(name, "memory"))   return Memory;
    if (!strcasecmp(name, "gpu"))      return Gpu;
    if (!strcasecmp(name, "storage"))  return Storage;
    if (!strcasecmp(name, "kernel"))   return Kernel;
    if (!strcasecmp(name, "services")) return Services;
    return -1;
}

static void printUsage(const char* prog) {
    debug::log("Usage: %s [command]", prog ? prog : "nxdiag");
    debug::log("  <module>           open a module: sysinfo, cpu, memory, gpu,");
    debug::log("                     storage, kernel, services");
    debug::log("  export [<module>]  probe one module (or every module) and write");
    debug::log("                     the categorized JSON report to");
    debug::log("                     sdmc:/nxdiag/report.json");
    debug::log("  help               show this message");
}

// Parse the homebrew launch command line. argv[1] selects a subcommand:
//   export [<module>]  - JSON-export one module, or every module
//   help               - print usage
//   <module>           - open that module
// With no argument the menu opens as usual.
static Options parseArgs(int argc, char* argv[]) {
    Options opt;
    if (argc < 2) return opt;
    const char* cmd = argv[1];

    if (!strcasecmp(cmd, "help") || !strcasecmp(cmd, "-h") ||
        !strcasecmp(cmd, "--help")) {
        printUsage(argv[0]);
        return opt;
    }
    if (!strcasecmp(cmd, "export")) {
        opt.exportMode = kExportAll;                  // "export" alone: all
        if (argc > 2) {
            int m = moduleByName(argv[2]);            // "export cpu": one
            if (m >= 0) opt.exportMode = m;
        }
        return opt;
    }
    int m = moduleByName(cmd);                        // bare module name
    if (m >= 0) opt.startMode = m;
    else        debug::log("nxdiag: unknown command '%s'; try 'nxdiag help'", cmd);
    return opt;
}

int main(int argc, char* argv[]) {
    // Bring up the debug sinks (nxlink + svcOutputDebugString) only when a
    // launch command was passed, so a plain hbmenu launch isn't slowed by the
    // socket-stack init.
    if (argc > 1) debug::init();

    const Options opt = parseArgs(argc, argv);

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
    if (!runner.init(views, (int)(sizeof(views) / sizeof(views[0])), cfg,
                     opt.startMode)) {
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
        debug::exit();
        return 1;
    }

    if (opt.exportMode != kExportNone) {
        // exportMode is a 1-based view index; beginExport wants a 0-based
        // probe index, or -1 for every module.
        int probeIdx = (opt.exportMode == kExportAll) ? -1 : opt.exportMode - 1;
        menu.beginExport(probeIdx);   // JSON-export on launch
    }

    runner.run();
    runner.deinit();
    debug::exit();
    return 0;
}
