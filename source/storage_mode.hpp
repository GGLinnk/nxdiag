#pragma once
#include "probe_mode.hpp"

// Storage I/O: SD-card capacity, a write/read/verify round-trip on a real
// file, measured I/O throughput and directory create/remove - an end-to-end
// filesystem self-test exercised from inside the running process.
class StorageMode : public ProbeMode {
public:
    void seedSkeleton() override;
    void run() override;
    const char* name() const override { return "Storage"; }
};
