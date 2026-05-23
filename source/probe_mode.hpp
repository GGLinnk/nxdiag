#pragma once
#include "nxdisplaylib/view.hpp"
#include "report.hpp"
#include "ui.hpp"
#include <atomic>

// Common base for the probe modules (SysInfo, CPU, Memory, GPU, Storage,
// Kernel, Services). It runs run() - the actual measurements - on a background
// worker thread, so the main loop keeps rendering at full frame rate and the
// applet stays responsive no matter how long a probe takes.
//
// A concrete module only implements run() plus name().
class ProbeMode : public nxd::View {
public:
    void onEnter() override;
    void update(const nxd::Input& in) override;
    void render(nxd::Gfx& g) override;

    const char* controls() const override {
        return "Up/Down/L/R/Stick/Drag: scroll   A: re-run   ZL/ZR: module";
    }

    // --- probe API, also driven by the menu's run-all export ------------
    // Lay out the report's sections and entries with "..." placeholders so
    // the first render frame shows the structure immediately. Runs on the
    // main thread, synchronously, before the probe worker is spawned. Real
    // values overwrite the placeholders in place (Section entries upsert).
    virtual void seedSkeleton() {}
    // Execute the module's probes/benchmarks, overwriting the seeded entries
    // (or appending them if no skeleton was seeded, as in the run-all export).
    virtual void run() {}
    // Clear prior data then re-run end-to-end (used by the run-all export).
    void runFresh() { report_.clear(); run(); hasRun_ = true; }
    bool hasRun() const { return hasRun_; }
    // Block until any background probe worker has finished.
    void joinWorker();
    const report::Report& report() const { return report_; }

protected:
    // Seed the skeleton and defer spawning the worker by a couple of frames,
    // so at least one rendered frame shows pure placeholders before any probe
    // touches report_. update() does the actual spawn when the countdown ends.
    void startWorker();

    report::Report report_;
    bool           hasRun_ = false;
    ReportView     view_;

private:
    void        spawnWorker();          // create + start the worker thread
    static void workerThunk(void* self);

    Thread            worker_{};
    bool              workerActive_ = false;
    std::atomic<bool> workerDone_{false};
    // 0 = no pending start; >0 = frames left to render before spawning. Set
    // to 2 by startWorker: one update decrements, render shows the seeded
    // placeholders, then the next update reaches 0 and spawns the worker.
    int               pendingFrames_ = 0;
    int               spin_ = 0;        // busy-overlay animation counter
};
