// Off-device asset generator for NX Diag.
//
// It drives the diagnostic views headlessly through the nxdisplaylib Runner
// (step / select / savePpm), writing a PPM still of every view plus a scripted
// walk-through. tools/host/Makefile turns those into the dist/ PNG stills and
// the showcase video via ffmpeg.
//
// The walk-through explores the app the way a user would: from the menu, open
// each probe module in turn, watch it probe, scroll through its report, return
// to the menu, and finally kick off the run-all JSON export. UI mock only -
// the probe numbers are dummy data.
#include "nxdisplaylib/runner.hpp"
#include "mode.hpp"
#include "probe_mode.hpp"
#include "menu_mode.hpp"
#include "sysinfo_mode.hpp"
#include "cpu_mode.hpp"
#include "mem_mode.hpp"
#include "gpu_mode.hpp"
#include "storage_mode.hpp"
#include "kernel_mode.hpp"
#include "services_mode.hpp"
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

using namespace nxd;

static const double kDt = 1.0 / 30.0;   // walk-through renders at 30 fps

static Runner      g_runner;
static const char* g_dir     = "dist/frames";
static int         g_frame   = 0;
static int         g_menuSel = 0;   // mirrors the menu's persistent cursor

// Step one walk-through frame and write it to the clip sequence.
static void emit(const Input& in) {
    const u32* px = g_runner.step(in);
    char path[256];
    snprintf(path, sizeof(path), "%s/clip_%05d.ppm", g_dir, g_frame++);
    savePpm(path, px);
}

static void idleFrames(int n) {
    for (int f = 0; f < n; f++) {
        Input in{};
        in.dtSec = kDt;
        emit(in);
    }
}

// Press a button for a single frame, then let the view settle.
static void tap(u64 button, int settle) {
    Input in{};
    in.dtSec = kDt;
    in.down  = button;
    emit(in);
    idleFrames(settle);
}

// Scroll the active report by holding a D-pad direction (ScrollView ramps the
// speed up the longer it is held).
static void scrollHeld(u64 button, int n) {
    for (int f = 0; f < n; f++) {
        Input in{};
        in.dtSec = kDt;
        in.held  = button;
        emit(in);
    }
}

// On the menu, walk the cursor down to absolute row `row`. The menu keeps its
// selection across visits, so the walk is relative to wherever it was left.
static void menuWalkTo(int row) {
    g_runner.select(Menu);
    int moves = row - g_menuSel;
    g_menuSel = row;
    int done  = 0;
    int total = 14 + moves * 9 + 12;
    for (int f = 0; f < total; f++) {
        Input in{};
        in.dtSec = kDt;
        if (done < moves && f >= 14 && (f - 14) % 9 == 0) {
            in.down = HidNpadButton_Down;
            done++;
        }
        emit(in);
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1) g_dir = argv[1];

    static MenuMode     menu;
    static SysInfoMode  sysinfo;
    static CpuMode      cpu;
    static MemMode      mem;
    static GpuMode      gpu;
    static StorageMode  storage;
    static KernelMode   kernel;
    static ServicesMode services;
    static View* views[] = {
        &menu, &sysinfo, &cpu, &mem, &gpu, &storage, &kernel, &services,
    };
    static ProbeMode* probes[] = {
        &sysinfo, &cpu, &mem, &gpu, &storage, &kernel, &services,
    };
    const int probeCount = (int)(sizeof(probes) / sizeof(probes[0]));
    menu.setProbes(probes, probeCount);

    RunnerConfig cfg;
    cfg.homeIndex  = Menu;
    cfg.cycleCount = 0;
    cfg.showFps    = false;

    if (!g_runner.init(views, COUNT, cfg)) {
        printf("gen: framebuffer init failed\n");
        return 1;
    }

    char path[256];

    // --- one still per view (settle long enough for the async probes) --
    {
        Input idle{};
        idle.dtSec = kDt;
        for (int i = 0; i < COUNT; i++) {
            g_runner.select(i);
            const u32* px = nullptr;
            for (int s = 0; s < 150; s++) px = g_runner.step(idle);
            char tag[64];
            snprintf(tag, sizeof(tag), "%s", g_runner.view(i)->name());
            for (char* c = tag; *c; c++) if (*c == ' ' || *c == '/') *c = '_';
            snprintf(path, sizeof(path), "%s/still_%s.ppm", g_dir, tag);
            savePpm(path, px);
        }
    }

    // The run-all export writes to sdmc:/nxdiag/report.json; provide that path
    // on the host so the showcase shows a genuine success. Removed at the end.
    mkdir("sdmc:", 0777);
    mkdir("sdmc:/nxdiag", 0777);

    // --- scripted exploration walk-through -----------------------------
    g_runner.select(Menu);
    idleFrames(40);                            // land on the main menu

    // Open each probe module in turn: probe, scroll the report, step back.
    for (int m = 0; m < probeCount; m++) {
        menuWalkTo(m);                         // cursor onto the module row
        tap(HidNpadButton_A, 48);              // open it; the probe runs async
        scrollHeld(HidNpadButton_Down, 74);    // explore the report downward
        idleFrames(16);
        scrollHeld(HidNpadButton_Up, 54);      // scroll back toward the top
        idleFrames(12);
        tap(HidNpadButton_B, 22);              // return to the main menu
    }

    // Finale: the run-all row probes every module and writes the JSON report.
    menuWalkTo(probeCount);
    idleFrames(16);
    tap(HidNpadButton_A, 170);                 // run-all sweep + export result
    idleFrames(40);

    g_runner.deinit();

    remove("sdmc:/nxdiag/report.json");
    rmdir("sdmc:/nxdiag");
    rmdir("sdmc:");

    printf("gen: %d stills + %d walk-through frames -> %s\n",
           COUNT, g_frame, g_dir);
    return 0;
}
