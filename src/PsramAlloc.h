#ifndef PSRAM_ALLOC_H
#define PSRAM_ALLOC_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace psram {

enum class AllocationTier : uint8_t {
    None = 0,
    Psram,
    Internal
};

struct AllocationResult {
    void* ptr = nullptr;
    AllocationTier tier = AllocationTier::None;

    bool ok() const { return ptr != nullptr; }
};

AllocationResult allocate(size_t sizeBytes);
AllocationResult reallocate(void* ptr, size_t sizeBytes);
void             deallocate(void* ptr);

bool        isPsramAvailable();
size_t      totalBytes();
size_t      freeBytes();
size_t      maxAllocBytes();
const char* tierName(AllocationTier tier);

} // namespace psram

#endif // PSRAM_ALLOC_H
