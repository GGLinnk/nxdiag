// Host mock of <deko3d.h> - just the device / memblock surface NX Diag's GPU
// module touches. dkMemBlockCreate hands back real host memory so the module's
// CPU-side writes are safe; everything else is a dummy.
#pragma once
#include <stdint.h>

typedef struct DkDevice_s*   DkDevice;
typedef struct DkMemBlock_s* DkMemBlock;

typedef struct DkDeviceMaker {
    void* userData; void* cbDebug; void* cbAlloc; void* cbFree; uint32_t flags;
} DkDeviceMaker;

typedef struct DkMemBlockMaker {
    DkDevice device; uint32_t size; uint32_t flags; void* storage;
} DkMemBlockMaker;

enum {
    DkMemBlockFlags_CpuUncached = 0u,
    DkMemBlockFlags_CpuCached   = 1u,
    DkMemBlockFlags_GpuUncached = 0u,
    DkMemBlockFlags_GpuCached   = 1u << 2,
};

inline void dkDeviceMakerDefaults(DkDeviceMaker* m) { *m = DkDeviceMaker{}; }
inline void dkMemBlockMakerDefaults(DkMemBlockMaker* m, DkDevice d, uint32_t sz) {
    *m = DkMemBlockMaker{}; m->device = d; m->size = sz;
}

DkDevice dkDeviceCreate(const DkDeviceMaker* maker);
void     dkDeviceDestroy(DkDevice obj);
uint64_t dkDeviceGetCurrentTimestamp(DkDevice obj);
uint64_t dkDeviceGetCurrentTimestampInNs(DkDevice obj);

DkMemBlock dkMemBlockCreate(const DkMemBlockMaker* maker);
void       dkMemBlockDestroy(DkMemBlock obj);
void*      dkMemBlockGetCpuAddr(DkMemBlock obj);
