#include "ui.hpp"
#include "nxdisplaylib/gfx.hpp"
#include <vector>
#include <cstring>

using namespace nxd;

namespace {
constexpr int kLineH = 22;   // pixel pitch of one report row at text scale 2
constexpr int kScale = 2;

u32 statusColor(report::Status s) {
    switch (s) {
        case report::Status::Good: return Gfx::rgb(120, 224, 140);
        case report::Status::Warn: return Gfx::rgb(244, 208, 100);
        case report::Status::Bad:  return Gfx::rgb(244, 116, 110);
        default:                   return Gfx::rgb(214, 218, 230);
    }
}

// One rendered row. kind: 0 = section header, 1 = entry, 2 = spacer,
// 3 = trailing "probing..." indicator.
struct Line {
    int            kind;
    const char*    key;
    const char*    value;
    report::Status status;
};

// Flatten a report into the ordered list of rows the view scrolls through.
void flatten(const report::Report& r, std::vector<Line>& out) {
    const auto& secs = r.sections();
    for (size_t si = 0; si < secs.size(); si++) {
        out.push_back(Line{0, secs[si].title.c_str(), nullptr, report::Status::Info});
        for (const report::Entry& e : secs[si].entries)
            out.push_back(Line{1, e.key.c_str(), e.value.c_str(), e.status});
        if (si + 1 < secs.size())
            out.push_back(Line{2, nullptr, nullptr, report::Status::Info});
    }
}
} // namespace

void ReportView::reset() {
    scroll_.reset();
}

void ReportView::handleInput(const Input& in) {
    scroll_.handleInput(in);
}

void ReportView::draw(Gfx& g, const report::Report& r, int x, int y, int w, int h,
                      const char* busy) {
    const u32 bg     = Gfx::rgb(14, 16, 22);
    const u32 header = Gfx::rgb(120, 180, 255);
    const u32 keyCol = Gfx::rgb(150, 156, 170);
    const u32 rule   = Gfx::rgb(40, 44, 56);

    g.fillRect(x, y, w, h, bg);

    std::vector<Line> lines;
    flatten(r, lines);
    // While a probe is running, a trailing animated row marks "more coming";
    // the report above it grows live as sections complete.
    if (busy)
        lines.push_back(Line{3, busy, nullptr, report::Status::Info});
    int total = (int)lines.size();

    scroll_.setMetrics(total * kLineH, h);
    int firstLine = scroll_.offset() / kLineH;
    int pixOff    = scroll_.offset() - firstLine * kLineH;

    const int pad  = 14;
    const int keyX = x + pad;
    const int valX = x + w / 2 + 20;

    for (int li = firstLine; li < total; li++) {
        int ry = y - pixOff + (li - firstLine) * kLineH;
        if (ry >= y + h) break;
        const Line& L = lines[li];

        if (L.kind == 0) {                       // section header
            g.fillRect(x, ry, w, kLineH, Gfx::rgb(22, 26, 36));
            g.drawText(keyX, ry + 3, kScale, header, L.key);
        } else if (L.kind == 1) {                // key / value row
            g.drawText(keyX, ry + 3, kScale, keyCol, L.key);
            int maxChars = (x + w - pad - 8 - valX) / g.charW(kScale);
            if (maxChars > 1 && (int)strlen(L.value) > maxChars) {
                char clipped[160];
                int n = maxChars < 159 ? maxChars : 159;
                for (int i = 0; i < n; i++) clipped[i] = L.value[i];
                clipped[n] = 0;
                g.drawText(valX, ry + 3, kScale, statusColor(L.status), clipped);
            } else {
                g.drawText(valX, ry + 3, kScale, statusColor(L.status), L.value);
            }
        } else if (L.kind == 2) {                // spacer rule
            g.hLine(x + pad, ry + kLineH / 2, w - pad * 2, rule);
        } else {                                 // trailing "probing..." row
            g.fillRect(x, ry, w, kLineH, Gfx::rgb(34, 30, 18));
            g.drawText(keyX, ry + 3, kScale, Gfx::rgb(244, 208, 100), L.key);
        }
    }

    scroll_.drawScrollbar(g, x, y, w, h);
}
