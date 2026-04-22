#include "DebugLogBuffer.h"

#include <string.h>
#include <time.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <esp_heap_caps.h>
#endif

namespace {
constexpr size_t   kBufferBytes  = 32 * 1024;
constexpr uint16_t kMaxLineBytes = 512;

constexpr uint8_t kFlagHasEpoch = 0x01;
constexpr uint8_t kFlagTruncated = 0x02;
constexpr uint8_t kFlagWrap = 0x80; // special wrap-around marker

struct __attribute__((packed)) RecordHeader {
    uint32_t id;
    uint32_t ts;       // epoch seconds if kFlagHasEpoch, else millis
    uint16_t len;      // payload length in bytes
    uint8_t  flags;
    uint8_t  reserved;
};
static_assert(sizeof(RecordHeader) == 12, "RecordHeader size must be 12 bytes");

uint8_t* ring    = nullptr;
size_t   head    = 0;
size_t   tail    = 0;
uint32_t nextId  = 1;

char     partial[kMaxLineBytes + 1];
uint16_t partialLen       = 0;
bool     partialTruncated = false;

volatile bool     viewerEnabled  = false;
volatile uint32_t viewerIp       = 0;
volatile uint32_t viewerUntilMs  = 0;

#if defined(ARDUINO_ARCH_ESP32)
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#define LOCK()   portENTER_CRITICAL(&mux)
#define UNLOCK() portEXIT_CRITICAL(&mux)
#else
#define LOCK()
#define UNLOCK()
#endif

// --------------- helpers ---------------

uint32_t ipToU32(const IPAddress& ip) {
    return (static_cast<uint32_t>(ip[0]) << 24) |
           (static_cast<uint32_t>(ip[1]) << 16) |
           (static_cast<uint32_t>(ip[2]) <<  8) |
            static_cast<uint32_t>(ip[3]);
}

bool ensureRingAllocatedUnlocked() {
    if (ring) return true;
#if defined(ARDUINO_ARCH_ESP32)
    ring = static_cast<uint8_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!ring)
        ring = static_cast<uint8_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
    ring = static_cast<uint8_t*>(malloc(kBufferBytes));
#endif
    if (!ring) return false;
    memset(ring, 0, kBufferBytes);
    head = 0; tail = 0; nextId = 1;
    partialLen = 0; partialTruncated = false;
    return true;
}

void freeRingUnlocked() {
    if (!ring) return;
#if defined(ARDUINO_ARCH_ESP32)
    heap_caps_free(ring);
#else
    free(ring);
#endif
    ring = nullptr;
}

size_t usedBytesUnlocked() {
    return (head >= tail) ? (head - tail) : (kBufferBytes - tail + head);
}

size_t freeBytesUnlocked() {
    return (kBufferBytes - 1) - usedBytesUnlocked();
}

bool isTimeSetPlausible(time_t now) {
    return now >= 1577836800; // 2020-01-01
}

void advanceTailOneUnlocked() {
    if (tail == head) return;
    if (tail + sizeof(RecordHeader) > kBufferBytes) { tail = 0; return; }
    RecordHeader hdr;
    memcpy(&hdr, &ring[tail], sizeof(hdr));
    if ((hdr.flags & kFlagWrap) != 0) { tail = 0; return; }
    tail = (tail + sizeof(RecordHeader) + hdr.len) % kBufferBytes;
}

void writeWrapMarkerUnlocked() {
    if (head + sizeof(RecordHeader) > kBufferBytes) { head = 0; return; }
    RecordHeader wrap;
    memset(&wrap, 0, sizeof(wrap));
    wrap.flags = kFlagWrap;
    memcpy(&ring[head], &wrap, sizeof(wrap));
    head = 0;
}

void writeRecordUnlocked(uint32_t ts, uint8_t flags, const char* msg, uint16_t len) {
    if (!ring) return;
    if (len > kMaxLineBytes) { len = kMaxLineBytes; flags |= kFlagTruncated; }

    const size_t recSize = sizeof(RecordHeader) + len;
    if (recSize >= kBufferBytes) return;

    while (freeBytesUnlocked() < recSize) {
        advanceTailOneUnlocked();
        if (tail == head) break;
    }

    if (head + recSize > kBufferBytes) writeWrapMarkerUnlocked();

    RecordHeader hdr;
    hdr.id       = nextId++;
    hdr.ts       = ts;
    hdr.len      = len;
    hdr.flags    = flags;
    hdr.reserved = 0;

    memcpy(&ring[head],               &hdr, sizeof(hdr));
    memcpy(&ring[head + sizeof(hdr)],  msg, len);
    head = (head + recSize) % kBufferBytes;
}

/** Returns false when the TTL has expired (and cleans up state). */
bool enabledUnlocked() {
    if (!viewerEnabled) return false;
    const uint32_t now = millis();
    if (viewerUntilMs != 0 && static_cast<int32_t>(viewerUntilMs - now) <= 0) {
        viewerEnabled = false;
        viewerUntilMs = 0;
        viewerIp      = 0;
        head = 0; tail = 0; nextId = 1;
        partialLen = 0; partialTruncated = false;
        // Keep ring allocated — avoids PSRAM fragmentation on rapid reconnect.
        // disableIfOwner() (Stop button) still frees it explicitly.
        return false;
    }
    return true;
}
} // namespace

// ============================================================
// Public API
// ============================================================

bool DebugLogBuffer::shouldCaptureFast() {
    if (!viewerEnabled) return false;
    const uint32_t until = viewerUntilMs;
    if (until == 0) return false;
    return static_cast<int32_t>(until - millis()) > 0;
}

void DebugLogBuffer::expireIfNeeded() {
    LOCK(); (void)enabledUnlocked(); UNLOCK();
}

void DebugLogBuffer::clear() {
    LOCK();
    head = 0; tail = 0; nextId = 1;
    partialLen = 0; partialTruncated = false;
    UNLOCK();
}

DebugLogBuffer::EnableResult DebugLogBuffer::enableFor(IPAddress ip, uint32_t ttlMs, uint32_t& remainingMs) {
    remainingMs = 0;
    const uint32_t now   = millis();
    const uint32_t ipU32 = ipToU32(ip);

    LOCK();
    (void)enabledUnlocked();

    if (viewerEnabled && viewerIp != ipU32) {
        const uint32_t until = viewerUntilMs;
        if (until > now) remainingMs = until - now;
        UNLOCK();
        return EnableResult::Busy;
    }

    viewerEnabled = true;
    viewerIp      = ipU32;
    viewerUntilMs = now + ttlMs;
    remainingMs   = ttlMs;

    if (!ensureRingAllocatedUnlocked()) {
        viewerEnabled = false; viewerUntilMs = 0; viewerIp = 0; remainingMs = 0;
        UNLOCK();
        return EnableResult::NoMem;
    }

    UNLOCK();
    return EnableResult::Ok;
}

bool DebugLogBuffer::keepAlive(IPAddress ip, uint32_t ttlMs) {
    const uint32_t now   = millis();
    const uint32_t ipU32 = ipToU32(ip);
    LOCK();
    if (!enabledUnlocked() || viewerIp != ipU32) { UNLOCK(); return false; }
    viewerUntilMs = now + ttlMs;
    UNLOCK();
    return true;
}

void DebugLogBuffer::disableIfOwner(IPAddress ip) {
    const uint32_t ipU32 = ipToU32(ip);
    LOCK();
    if (viewerEnabled && viewerIp == ipU32) {
        viewerEnabled = false; viewerUntilMs = 0; viewerIp = 0;
        head = 0; tail = 0; nextId = 1;
        partialLen = 0; partialTruncated = false;
        freeRingUnlocked();
    }
    UNLOCK();
}

bool DebugLogBuffer::isEnabled() {
    expireIfNeeded();
    LOCK(); const bool e = viewerEnabled; UNLOCK();
    return e;
}

bool DebugLogBuffer::isOwner(IPAddress ip) {
    const uint32_t ipU32 = ipToU32(ip);
    expireIfNeeded();
    LOCK(); const bool ok = viewerEnabled && viewerIp == ipU32; UNLOCK();
    return ok;
}

uint32_t DebugLogBuffer::remainingMs() {
    const uint32_t now = millis();
    expireIfNeeded();
    LOCK();
    if (!viewerEnabled || viewerUntilMs == 0 || viewerUntilMs <= now) { UNLOCK(); return 0; }
    const uint32_t rem = viewerUntilMs - now;
    UNLOCK();
    return rem;
}

void DebugLogBuffer::commitPartialLine() {
    LOCK();
    if (!enabledUnlocked() || !ring) {
        partialLen = 0; partialTruncated = false;
        UNLOCK(); return;
    }
    if (partialLen == 0 && !partialTruncated) { UNLOCK(); return; }

    partial[partialLen] = '\0';
    uint8_t flags = 0;
    time_t now = time(nullptr);
    if (isTimeSetPlausible(now)) {
        flags |= kFlagHasEpoch;
        writeRecordUnlocked((uint32_t)now, flags | (partialTruncated ? kFlagTruncated : 0),
                            partial, partialLen);
    } else {
        writeRecordUnlocked(millis(), flags | (partialTruncated ? kFlagTruncated : 0),
                            partial, partialLen);
    }
    partialLen = 0; partialTruncated = false;
    UNLOCK();
}

void DebugLogBuffer::append(const char* data, size_t len) {
    if (!data || len == 0) return;

    // Fast-exit before per-char processing
    LOCK();
    if (!enabledUnlocked() || !ring) { UNLOCK(); return; }
    UNLOCK();

    for (size_t i = 0; i < len; i++) {
        const char c = data[i];
        if (c == '\r') continue;
        if (c == '\n') { commitPartialLine(); continue; }

        LOCK();
        if (!enabledUnlocked()) {
            partialLen = 0; partialTruncated = false;
            UNLOCK(); return;
        }
        if (partialLen < kMaxLineBytes) partial[partialLen++] = c;
        else                            partialTruncated = true;
        UNLOCK();
    }
}

uint32_t DebugLogBuffer::latestId() {
    LOCK();
    const uint32_t latest = (nextId > 1) ? (nextId - 1) : 0;
    UNLOCK();
    return latest;
}

size_t DebugLogBuffer::readSince(uint32_t sinceId, size_t maxLines, size_t maxBytes,
                                  LineOut* outLines, bool& dropped) {
    dropped = false;
    if (!outLines || maxLines == 0 || maxBytes == 0) return 0;

    expireIfNeeded();

    LOCK();
    if (!viewerEnabled || !ring) { UNLOCK(); return 0; }

    const uint32_t latest = (nextId > 1) ? (nextId - 1) : 0;
    if (sinceId != 0 && sinceId + 1 > latest) { UNLOCK(); return 0; }

    size_t idx = tail, written = 0, usedMsgBytes = 0;
    bool   foundSince = (sinceId == 0);

    while (idx != head && written < maxLines) {
        if (idx + sizeof(RecordHeader) > kBufferBytes) { idx = 0; continue; }

        RecordHeader hdr;
        memcpy(&hdr, &ring[idx], sizeof(hdr));
        if ((hdr.flags & kFlagWrap) != 0) { idx = 0; continue; }

        const size_t recSize  = sizeof(RecordHeader) + hdr.len;
        const size_t msgOffset = idx + sizeof(RecordHeader);
        const char*  msgPtr    = (msgOffset < kBufferBytes)
                                 ? reinterpret_cast<const char*>(&ring[msgOffset])
                                 : nullptr;

        if (!foundSince) {
            if (hdr.id <= sinceId) { idx = (idx + recSize) % kBufferBytes; continue; }
            foundSince = true;
        }

        if (hdr.len > 0 && msgPtr) {
            if (usedMsgBytes + hdr.len > maxBytes) break;
            outLines[written].id       = hdr.id;
            outLines[written].hasEpoch = (hdr.flags & kFlagHasEpoch) != 0;
            outLines[written].ts       = hdr.ts;
            outLines[written].truncated= (hdr.flags & kFlagTruncated) != 0;
            outLines[written].msg      = msgPtr;
            outLines[written].msgLen   = hdr.len;
            usedMsgBytes += hdr.len;
            written++;
        }
        idx = (idx + recSize) % kBufferBytes;
    }

    if (!foundSince && sinceId != 0) dropped = true;

    UNLOCK();
    return written;
}
