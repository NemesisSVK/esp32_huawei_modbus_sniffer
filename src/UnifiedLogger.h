#pragma once
#include <Arduino.h>
#include <cstdarg>

/**
 * UnifiedLogger — sniffer-specific variant.
 *
 * API is intentionally API-compatible with the HA Manager's UnifiedLogger so
 * shared files (AsyncMqttTransport, etc.) compile without modification.
 *
 * Key differences from the HA Manager version:
 *  - No ConfigManager / DebugLogBuffer dependency.
 *  - Enabled by default on begin(); call setEnabled(false) to silence.
 *  - No runtime config reload (sniffer has no debug.logging_enabled setting).
 *  - All output goes to Serial only.
 */
class UnifiedLogger {
public:
    enum class Level : uint8_t {
        Error   = 0,
        Warning,
        Info,
        Verbose
    };

    /** Call once after Serial.begin() to activate logging. */
    static void begin(bool enabled = true);

    /** Runtime enable/disable toggle. */
    static void setEnabled(bool enabled);

    static bool isEnabled();

    // Severity-prefixed logging
    static size_t log(Level level, const char* format, ...);
    static size_t vLog(Level level, const char* format, va_list args);

    static size_t error(const char* format, ...);
    static size_t warning(const char* format, ...);
    static size_t info(const char* format, ...);
    static size_t verbose(const char* format, ...);

    // Raw passthrough (no prefix)
    static size_t raw(const char* format, ...);
    static size_t vRaw(const char* format, va_list args);

private:
    static bool loggingEnabled;
    static const char* levelTag(Level level);
};
