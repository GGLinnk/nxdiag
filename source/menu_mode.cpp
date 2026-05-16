#include "menu_mode.hpp"
#include "probe_mode.hpp"
#include "nxdisplaylib/gfx.hpp"
#include <cstdio>

// Provided by the build (-DAPP_VERSION, from the Makefile's APP_VERSION);
// fall back so the file still compiles outside the project build.
#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

using namespace nxd;

namespace {
// One label/blurb per probe module, in screen order (view index = item + 1).
struct Item { const char* label; const char* blurb; };
const Item kItems[] = {
    { "System Info", "Firmware, hardware model, region, clocks" },
    { "CPU",         "Cores, clocks, integer/float/NEON benchmarks" },
    { "Memory",      "Regions, VM map, bandwidth, latency" },
    { "GPU",         "deko3d bring-up, GPU memory, clocks" },
    { "Storage",     "SD card capacity, file I/O round-trip" },
    { "Kernel / SVC","svcGetInfo sweep, process, entropy" },
    { "Services",    "Reachability probe of system services" },
};
constexpr int kItemCount = (int)(sizeof(kItems) / sizeof(kItems[0]));

constexpr int kListX   = 90;
constexpr int kListY   = 152;
constexpr int kRowH    = 86;              // row pitch
constexpr int kRowVis  = 72;              // drawn (and touchable) row height
constexpr int kRowW    = 1100;
constexpr int kPerPage = 5;

const char* kReportPath = "sdmc:/nxdiag/report.json";
} // namespace

// Worker body: probe every module, then write the combined JSON. Runs on a
// background thread so the menu keeps rendering throughout.
void MenuMode::runExport() {
    for (int i = 0; i < probeCount_; i++) {
        exportStage_.store(i + 1, std::memory_order_relaxed);
        ProbeMode* m = probes_[i];
        m->joinWorker();        // never race a module's own probe worker
        m->runFresh();
        master_.append(m->report());
    }
    exportStage_.store(probeCount_ + 1, std::memory_order_relaxed);

    Result rc = master_.writeJson(kReportPath);
    exportOk_ = R_SUCCEEDED(rc);
    char buf[160];
    if (exportOk_)
        snprintf(buf, sizeof(buf), "Exported %s", kReportPath);
    else
        snprintf(buf, sizeof(buf), "Export failed (%s) - is an SD card present?",
                 report::describeResult(rc).c_str());
    exportMsg_ = buf;
    exportDone_.store(true, std::memory_order_release);
}

void MenuMode::exportThunk(void* self) {
    static_cast<MenuMode*>(self)->runExport();
}

void MenuMode::startExport() {
    master_.clear();
    exportMsg_.clear();
    exportDone_.store(false, std::memory_order_relaxed);
    exportStage_.store(0, std::memory_order_relaxed);

    s32 prio = 0x2C;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    Result rc = threadCreate(&exportThread_, exportThunk, this, nullptr,
                             256 * 1024, prio, -2);
    if (R_SUCCEEDED(rc) && R_SUCCEEDED(threadStart(&exportThread_)))
        exportActive_ = true;
    else
        runExport();            // no worker thread available: run inline
}

void MenuMode::update(const Input& in) {
    if (exportActive_) {
        if (exportDone_.load(std::memory_order_acquire)) {
            threadWaitForExit(&exportThread_);
            threadClose(&exportThread_);
            exportActive_ = false;
        }
        return;                 // menu still renders, but ignores input
    }

    ListMenu::Layout lay;
    lay.x = kListX; lay.y = kListY;
    lay.rowPitch = kRowH; lay.rowW = kRowW; lay.rowVis = kRowVis;
    lay.perPage = kPerPage;
    menu_.configure(kItemCount + 1, lay);     // +1 synthetic "run all" row
    menu_.handleInput(in);

    int act = menu_.activated();
    if (act >= 0) {
        if (act == kItemCount) startExport();          // the run-all row
        else                   requestSwitch(act + 1); // view index = item + 1
    }
}

void MenuMode::render(Gfx& g) {
    const u32 bg     = Gfx::rgb(13, 15, 23);
    const u32 fg     = Gfx::rgb(232, 234, 244);
    const u32 dim    = Gfx::rgb(150, 156, 170);
    const u32 accent = Gfx::rgb(120, 180, 255);
    const u32 selBg  = Gfx::rgb(30, 52, 86);
    const u32 rowBg  = Gfx::rgb(20, 23, 32);
    const u32 runFg  = Gfx::rgb(120, 224, 140);

    g.clear(bg);
    g.drawText(kListX, 50, 4, accent, "NX Diag");
    g.drawText(kListX, 98, 2, dim,
               "Device diagnostics - probe libnx, services and hardware");

    // Build version, aligned to the right of the title row.
    const char* ver = "v" APP_VERSION;
    g.drawText(kListX + kRowW - g.textWidth(2, ver), 58, 2, dim, ver);

    char pg[24];
    snprintf(pg, sizeof(pg), "Page %d / %d", menu_.page() + 1, menu_.pageCount());
    g.drawText(kListX + kRowW - g.textWidth(2, pg), 98, 2, accent, pg);

    for (int row = 0; row < kPerPage; row++) {
        int x, y, w, h;
        int item = menu_.visibleItem(row, x, y, w, h);
        if (item < 0) break;
        bool sel = (item == menu_.selected());
        g.fillRect(x, y, w, h, sel ? selBg : rowBg);
        if (sel) g.drawRectThick(x, y, w, h, 2, accent);

        if (item == kItemCount) {
            g.drawText(x + 20, y + 14, 3, runFg, "Run all tests & export JSON");
            g.drawText(x + 20, y + 46, 2, dim,
                       "Probe every module and write report.json to the SD card");
        } else {
            g.drawText(x + 20, y + 14, 3, fg,  kItems[item].label);
            g.drawText(x + 20, y + 46, 2, dim, kItems[item].blurb);
        }
    }

    // Export status line.
    int sy = kListY + kPerPage * kRowH + 10;
    if (exportActive_) {
        int stage = exportStage_.load(std::memory_order_relaxed);
        char buf[96];
        if (stage >= 1 && stage <= probeCount_)
            snprintf(buf, sizeof(buf), "Running module %d/%d: %s ...",
                     stage, probeCount_, probes_[stage - 1]->name());
        else if (stage > probeCount_)
            snprintf(buf, sizeof(buf), "Writing report.json ...");
        else
            snprintf(buf, sizeof(buf), "Starting ...");
        g.drawText(kListX, sy, 2, Gfx::rgb(244, 208, 100), buf);
    } else if (!exportMsg_.empty()) {
        g.drawText(kListX, sy, 2,
                   exportOk_ ? runFg : Gfx::rgb(244, 116, 110),
                   exportMsg_.c_str());
    }
}
