#pragma once
#include "probe_mode.hpp"

// System-service reachability: each entry tries to initialise a libnx service,
// records the Result, reads one representative value and exits. The set of
// services that succeed is itself a fingerprint of the host environment.
class ServicesMode : public ProbeMode {
public:
    void seedSkeleton() override;
    void run() override;
    const char* name() const override { return "Services"; }
};
