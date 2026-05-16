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
    // Execute the module's probes/benchmarks, appending to report_.
    virtual void run() {}
    // Clear prior data then re-run: idempotent (run() only appends sections).
    void runFresh() { report_.clear(); run(); hasRun_ = true; }
    bool hasRun() const { return hasRun_; }
    // Block until any background probe worker has finished.
    void joinWorker();
    const report::Report& report() const { return report_; }

protected:
    void startWorker();

    report::Report report_;
    bool           hasRun_ = false;
    ReportView     view_;

private:
    static void workerThunk(void* self);

    Thread            worker_{};
    bool              workerActive_ = false;
    std::atomic<bool> workerDone_{false};
    int               spin_ = 0;        // busy-overlay animation counter
};
