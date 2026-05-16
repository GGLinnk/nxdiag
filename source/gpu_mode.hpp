#pragma once
#include "probe_mode.hpp"

// GPU: deko3d device bring-up, GPU memory allocation, the GPU timestamp clock
// and the GPU / EMC clock rates reported by clkrst.
class GpuMode : public ProbeMode {
public:
    void run() override;
    const char* name() const override { return "GPU"; }
};
