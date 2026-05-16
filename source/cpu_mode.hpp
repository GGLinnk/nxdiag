#pragma once
#include "probe_mode.hpp"

// CPU topology, clocks and benchmarks: integer / floating-point / NEON
// throughput, single- vs multi-core scaling and counter resolution.
class CpuMode : public ProbeMode {
public:
    void run() override;
    const char* name() const override { return "CPU"; }
};
