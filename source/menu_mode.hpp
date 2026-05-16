#pragma once
#include "nxdisplaylib/view.hpp"
#include "nxdisplaylib/widgets.hpp"
#include "report.hpp"
#include <atomic>
#include <string>

class ProbeMode;

// Landing screen. A paged list of the probe modules plus a final "run all &
// export" action that probes every module and writes the combined JSON report
// to the SD card on a background thread.
class MenuMode : public nxd::View {
public:
    // The probe modules, in screen order, used by the run-all export.
    void setProbes(ProbeMode** probes, int count) {
        probes_ = probes; probeCount_ = count;
    }

    void update(const nxd::Input& in) override;
    void render(nxd::Gfx& g) override;
    const char* name() const override { return "NX Diag"; }
    const char* controls() const override {
        return "D-Pad: select   L/R: page   A: open   ZL/ZR: module   +: exit";
    }

private:
    void startExport();
    void runExport();                       // worker body
    static void exportThunk(void* self);

    ProbeMode**   probes_     = nullptr;
    int           probeCount_ = 0;
    nxd::ListMenu menu_;

    // Run-all probes every module on a background thread, so the menu keeps
    // rendering and the applet stays responsive throughout.
    Thread            exportThread_{};
    bool              exportActive_ = false;
    std::atomic<bool> exportDone_{false};
    std::atomic<int>  exportStage_{0};      // 1-based probe index in progress
    report::Report    master_;              // accumulates each module's report

    // Result of the most recent export, shown under the menu.
    std::string exportMsg_;
    bool        exportOk_ = false;
};
