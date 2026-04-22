#ifndef PSRAM_LIMITS_H
#define PSRAM_LIMITS_H

#include <stddef.h>

/**
 * PsramLimits — compile-time PSRAM budget constants for the Modbus Sniffer.
 *
 * These guide PSRAM allocation decisions across the codebase and serve as
 * the single source of truth for memory planning. Tune after stress-testing
 * with the /logs viewer and the heap heartbeat.
 */
namespace psram_limits {

// DebugLogBuffer ring buffer (allocated in PSRAM only while /logs viewer is active)
constexpr size_t kDebugLogBufferBytes = 32U * 1024U;

// JSON parse/serialize budget (ConfigManager load + save)
constexpr size_t kJsonConfigBudgetBytes = 16U * 1024U;

// Web UI page budget — largest rendered pages (settings, logs)
constexpr size_t kWebPageBudgetBytes = 48U * 1024U;

// Heap health thresholds used by the heartbeat and LED diagnostics
constexpr size_t kHeapWarningThresholdBytes  = 30U * 1024U;  // log [WARNING]
constexpr size_t kHeapCriticalThresholdBytes = 15U * 1024U;  // LED → magenta

} // namespace psram_limits

#endif // PSRAM_LIMITS_H
