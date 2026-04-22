#include "UnifiedLogger.h"
#include "DebugLogBuffer.h"
#include <cstdio>
#include <cstring>

namespace {
constexpr size_t kLogBufSize = 512;

// Write text to Serial (always) and to DebugLogBuffer (when a viewer session is active
// AND loggingEnabled is true). Serial output is unconditional — independent of
// the debug.logging_enabled config setting so USB serial always shows system messages.
size_t writeDirect(const char* text) {
    if (!text || *text == '\0') return 0;
    const size_t len = strlen(text);
    Serial.print(text);
    if (UnifiedLogger::isEnabled() && DebugLogBuffer::shouldCaptureFast())
        DebugLogBuffer::append(text, len);
    return len;
}

size_t writeFormatted(const char* format, va_list args) {
    if (!format) return 0;
    char buf[kLogBufSize];
    int len = vsnprintf(buf, sizeof(buf), format, args);
    if (len <= 0) return 0;
    const size_t clipped = strnlen(buf, sizeof(buf));
    Serial.print(buf);
    if (clipped > 0 && UnifiedLogger::isEnabled() && DebugLogBuffer::shouldCaptureFast())
        DebugLogBuffer::append(buf, clipped);
    return static_cast<size_t>(len);
}
} // namespace

bool UnifiedLogger::loggingEnabled = false;

void UnifiedLogger::begin(bool enabled) {
    loggingEnabled = enabled;
}

void UnifiedLogger::setEnabled(bool enabled) {
    loggingEnabled = enabled;
}

bool UnifiedLogger::isEnabled() {
    return loggingEnabled;
}

const char* UnifiedLogger::levelTag(Level level) {
    switch (level) {
        case Level::Error:   return "[ERROR] ";
        case Level::Warning: return "[WARNING] ";
        case Level::Info:    return "[INFO] ";
        case Level::Verbose: return "[VERBOSE] ";
        default:             return "";
    }
}

// raw / vRaw — always to Serial. No level prefix.
size_t UnifiedLogger::vRaw(const char* format, va_list args) {
    return writeFormatted(format, args);
}

size_t UnifiedLogger::raw(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const size_t n = vRaw(format, args);
    va_end(args);
    return n;
}

// vLog — always to Serial. DebugLogBuffer gated by loggingEnabled inside writeDirect/writeFormatted.
size_t UnifiedLogger::vLog(Level level, const char* format, va_list args) {
    size_t n = writeDirect(levelTag(level));
    n += writeFormatted(format, args);
    return n;
}

size_t UnifiedLogger::log(Level level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const size_t n = vLog(level, format, args);
    va_end(args);
    return n;
}

// error / warning / info — always to Serial regardless of loggingEnabled.
size_t UnifiedLogger::error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const size_t n = vLog(Level::Error, format, args);
    va_end(args);
    return n;
}

size_t UnifiedLogger::warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const size_t n = vLog(Level::Warning, format, args);
    va_end(args);
    return n;
}

size_t UnifiedLogger::info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const size_t n = vLog(Level::Info, format, args);
    va_end(args);
    return n;
}

// verbose — gated on loggingEnabled to avoid flooding serial in production.
size_t UnifiedLogger::verbose(const char* format, ...) {
    if (!loggingEnabled) return 0;
    va_list args;
    va_start(args, format);
    const size_t n = vLog(Level::Verbose, format, args);
    va_end(args);
    return n;
}
