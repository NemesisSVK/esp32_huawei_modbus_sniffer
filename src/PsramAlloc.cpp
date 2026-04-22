#include "PsramAlloc.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif
#include <stdlib.h>

namespace psram {
namespace {

#if defined(ARDUINO_ARCH_ESP32)
AllocationResult allocateWithCaps(size_t sizeBytes, uint32_t caps, AllocationTier tier) {
    AllocationResult r;
    r.ptr  = heap_caps_malloc(sizeBytes, caps);
    r.tier = r.ptr ? tier : AllocationTier::None;
    return r;
}
AllocationResult reallocateWithCaps(void* ptr, size_t sizeBytes, uint32_t caps, AllocationTier tier) {
    AllocationResult r;
    r.ptr  = heap_caps_realloc(ptr, sizeBytes, caps);
    r.tier = r.ptr ? tier : AllocationTier::None;
    return r;
}
#endif

} // namespace

AllocationResult allocate(size_t sizeBytes) {
    if (sizeBytes == 0) return {};
#if defined(ARDUINO_ARCH_ESP32)
    auto r = allocateWithCaps(sizeBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, AllocationTier::Psram);
    if (r.ok()) return r;
    return allocateWithCaps(sizeBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, AllocationTier::Internal);
#else
    AllocationResult r;
    r.ptr  = malloc(sizeBytes);
    r.tier = r.ptr ? AllocationTier::Internal : AllocationTier::None;
    return r;
#endif
}

AllocationResult reallocate(void* ptr, size_t sizeBytes) {
    if (!ptr)          return allocate(sizeBytes);
    if (sizeBytes == 0){ deallocate(ptr); return {}; }
#if defined(ARDUINO_ARCH_ESP32)
    auto r = reallocateWithCaps(ptr, sizeBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, AllocationTier::Psram);
    if (r.ok()) return r;
    return reallocateWithCaps(ptr, sizeBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, AllocationTier::Internal);
#else
    AllocationResult r;
    r.ptr  = realloc(ptr, sizeBytes);
    r.tier = r.ptr ? AllocationTier::Internal : AllocationTier::None;
    return r;
#endif
}

void deallocate(void* ptr) {
    if (!ptr) return;
#if defined(ARDUINO_ARCH_ESP32)
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

bool isPsramAvailable() {
#if defined(ARDUINO_ARCH_ESP32)
    return ESP.getPsramSize() > 0;
#else
    return false;
#endif
}

size_t totalBytes()    { return (size_t)ESP.getPsramSize();     }
size_t freeBytes()     { return (size_t)ESP.getFreePsram();     }
size_t maxAllocBytes() { return (size_t)ESP.getMaxAllocPsram(); }

const char* tierName(AllocationTier tier) {
    switch (tier) {
        case AllocationTier::Psram:    return "psram";
        case AllocationTier::Internal: return "internal";
        default:                       return "none";
    }
}

} // namespace psram
