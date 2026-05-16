#pragma once
#include "nxdisplaylib/input.hpp"
#include "nxdisplaylib/widgets.hpp"
#include "report.hpp"

namespace nxd { class Gfx; }

// Scrollable two-column rendering of a report::Report (section headers, then
// "key .... value" rows colour-coded by status). Scrolling is delegated to a
// nxd::ScrollView (D-pad / L-R / left stick / touch-drag).
class ReportView {
public:
    void reset();
    void handleInput(const nxd::Input& in);

    // Draw `r` inside (x,y,w,h). `busy`, when set, adds a trailing animated
    // row so the report can render live while a probe is still filling it.
    void draw(nxd::Gfx& g, const report::Report& r, int x, int y, int w, int h,
              const char* busy = nullptr);

private:
    nxd::ScrollView scroll_;
};
