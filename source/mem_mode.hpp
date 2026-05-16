#pragma once
#include "probe_mode.hpp"

// Memory: kernel region sizes, the virtual-address-space map, copy/fill
// bandwidth, cache-latency curve and the effective heap allocation limit.
class MemMode : public ProbeMode {
public:
    void run() override;
    const char* name() const override { return "Memory"; }
};
