#include "probe_mode.hpp"
#include "nxdisplaylib/gfx.hpp"
#include <switch.h>
#include <cstdio>

using namespace nxd;

void ProbeMode::onEnter() {
    // Probe once on first visit; later visits show the cached report and the
    // user re-runs explicitly with A.
    if (!workerActive_ && !hasRun_)
        startWorker();
}

void ProbeMode::workerThunk(void* self) {
    ProbeMode* m = static_cast<ProbeMode*>(self);
    m->run();
    m->workerDone_.store(true, std::memory_order_release);
}

void ProbeMode::startWorker() {
    if (workerActive_) return;          // a probe is already in flight

    report_.clear();
    hasRun_ = false;
    workerDone_.store(false, std::memory_order_relaxed);
    view_.reset();

    s32 prio = 0x2C;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);

    Result rc = threadCreate(&worker_, workerThunk, this, nullptr,
                             256 * 1024, prio, -2);
    if (R_SUCCEEDED(rc) && R_SUCCEEDED(threadStart(&worker_))) {
        workerActive_ = true;
    } else {
        // No worker thread available: fall back to running inline.
        run();
        hasRun_ = true;
    }
}

void ProbeMode::joinWorker() {
    if (!workerActive_) return;
    threadWaitForExit(&worker_);
    threadClose(&worker_);
    workerActive_ = false;
    hasRun_ = true;
}

void ProbeMode::update(const Input& in) {
    if (workerActive_ && workerDone_.load(std::memory_order_acquire)) {
        threadWaitForExit(&worker_);
        threadClose(&worker_);
        workerActive_ = false;
        hasRun_ = true;
    }
    // A re-runs (only when idle); scrolling works even while a probe runs,
    // since the report fills in live.
    if (!workerActive_ && (in.down & HidNpadButton_A))
        startWorker();
    view_.handleInput(in);
}

void ProbeMode::render(Gfx& g) {
    const int top = 30, bot = 30;
    const int y = top, h = Gfx::H - top - bot;

    char busyBuf[96];
    const char* busy = nullptr;
    if (workerActive_) {
        spin_++;
        static const char* kSpin[] = { "|", "/", "-", "\\" };
        snprintf(busyBuf, sizeof(busyBuf), "  %s  probing %s ...",
                 kSpin[(spin_ / 6) & 3], name());
        busy = busyBuf;
    }
    // Render the report while the worker is still filling it: the report lock
    // keeps the read consistent against the worker's section/entry appends.
    report_.lock();
    view_.draw(g, report_, 0, y, Gfx::W, h, busy);
    report_.unlock();
}
