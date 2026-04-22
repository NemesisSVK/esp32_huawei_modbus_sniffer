#pragma once

#include <Arduino.h>
#include <stdint.h>

/**
 * DebugLogBuffer — PSRAM-backed ring buffer for in-browser log capture.
 *
 * How it works:
 *   - Capture is gated by a viewer session (identified by client IP + TTL).
 *   - While active, UnifiedLogger feeds every formatted log line here.
 *   - The web UI's /api/logs endpoint polls readSince() to stream new lines.
 *   - Buffer is allocated from PSRAM on demand and freed when the session ends.
 *
 * Thread safety:
 *   - All methods are protected by a portMUX spin lock.
 *   - shouldCaptureFast() is a lock-free fast-path gate for the logger hot-path.
 *
 * Ported from ESP32 Temp HA Manager — adapted for the Modbus Sniffer
 * (no ConfigManager / NTP dependency in this variant).
 */
class DebugLogBuffer {
public:
    static void clear();

    // Viewer lock + capture gating (single viewer by IP).
    // - If not locked, claims lock and clears buffer.
    // - If locked by same IP, refreshes TTL.
    // - If locked by different IP and not expired, returns Busy + sets remainingMs.
    enum class EnableResult : uint8_t {
        Ok    = 0,
        Busy  = 1,
        NoMem = 2,
    };
    static EnableResult enableFor(IPAddress ip, uint32_t ttlMs, uint32_t& remainingMs);

    /** Returns true if capture is enabled and owned by ip; refreshes TTL. */
    static bool keepAlive(IPAddress ip, uint32_t ttlMs);

    static void     disableIfOwner(IPAddress ip);
    static bool     isEnabled();
    static bool     isOwner(IPAddress ip);
    static uint32_t remainingMs();

    /**
     * Fast capture gate for UnifiedLogger hot-path.
     * Returns false with near-zero overhead when no viewer is active
     * or after TTL expiry even if state hasn't been cleaned up yet.
     */
    static bool shouldCaptureFast();

    /** Append raw bytes to line assembler. Safe to call often; returns quickly when disabled. */
    static void append(const char* data, size_t len);

    static uint32_t latestId();

    struct LineOut {
        uint32_t id;
        bool     hasEpoch;   // true if ts is a Unix epoch second; false = millis
        uint32_t ts;
        bool     truncated;
        const char* msg;     // points into ring buffer — use immediately after readSince()
        uint16_t    msgLen;
    };

    /**
     * Iterates up to maxLines lines after sinceId, within maxBytes budget.
     * Returns number of lines written; sets dropped=true if sinceId is too old.
     * NOTE: msg pointers are valid only until the next append() call.
     */
    static size_t readSince(uint32_t sinceId, size_t maxLines, size_t maxBytes,
                            LineOut* outLines, bool& dropped);

private:
    static void expireIfNeeded();
    static void commitPartialLine();
};
