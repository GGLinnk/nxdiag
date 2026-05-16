#pragma once
#include "probe_mode.hpp"

// System identity: firmware version, hardware model / SoC, region, language,
// performance mode, CFW detection and the system tick frequency.
class SysInfoMode : public ProbeMode {
public:
    void run() override;
    const char* name() const override { return "System Info"; }
};
