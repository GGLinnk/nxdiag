#pragma once
#include "probe_mode.hpp"

// Kernel / SVC surface: the svcGetInfo InfoType sweep, system tick behaviour,
// process / thread identity, the libnx environment and entropy sources.
class KernelMode : public ProbeMode {
public:
    void run() override;
    const char* name() const override { return "Kernel / SVC"; }
};
