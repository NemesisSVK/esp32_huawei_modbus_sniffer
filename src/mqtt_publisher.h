#pragma once
#include "reg_groups.h"
#include "MQTTManager.h"
#include "ConfigManager.h"
#include <stdint.h>

struct MQTTAvailabilityGroupStats {
    uint32_t last_seen_ms;
    uint32_t last_online_ms;
    uint32_t last_offline_ms;
    uint32_t online_transitions;
    uint32_t offline_transitions;
    bool offline;
};

struct MQTTAvailabilitySnapshot {
    MQTTAvailabilityGroupStats groups[GRP_COUNT];
};

/**
 * Initialize the MQTT publisher.
 * Must be called AFTER ConfigManager and WiFi are ready.
 */
void mqtt_publisher_init(MQTTManager* mgr, const Settings* cfg);

/** Call from loop() - MQTTManager keepalive + reconnect. */
void mqtt_loop();

/** True if broker is currently connected. */
bool mqtt_connected();

/**
 * Decoder callback - accumulate a decoded register value.
 * JSON mode : always accumulates into the per-group buffer; publication is
 *             triggered by mqtt_tick() based on tier intervals.
 * Individual mode: publishes immediately, gated by 0.5% change detection.
 */
void mqtt_publish_value(const char* name, float value,
                        const char* unit, uint8_t slave_addr,
                        RegGroup group, uint8_t source_id,
                        uint16_t reg_addr, uint8_t reg_words);

/**
 * Tiered flush loop - call every loop() iteration.
 *
 *  - Per-tier timer: when elapsed >= tier_interval_s, flush all groups
 *    assigned to that tier. LOW-tier groups are published with retained=true.
 *  - Per-group availability watchdog: if a group's last-seen time exceeds
 *    a stale threshold (4x tier interval with a global 120s floor; priority
 *    group also enforces at least 120s), publish "offline" to
 *    <base>/<key>/available (retained). Reset to "online" on next successful
 *    group flush.
 *  - Always-on device diagnostics: publish HA discovery + MQTT state topics
 *    for cpu temp, free heap, and LittleFS free percent (60 s cadence and
 *    immediate snapshot on MQTT reconnect).
 */
void mqtt_tick();

/**
 * Returns a JSON string of all last-seen decoded values.
 * Format: {"subtopic/register@source#slave": {"v": 3.14, "u": "W", "slave": 1, "group": "meter", "src": "H41-33"}, ...}
 * Used by /api/values to populate dashboard cards.
 */
String mqtt_get_last_values_json();

/**
 * Returns a JSON string of cached values that are currently routed to the
 * manual priority group only (GRP_PRIORITY_MANUAL).
 * Used by /api/priority_values for high-frequency monitoring page updates.
 */
String mqtt_get_priority_values_json();

/** Snapshot per-group availability watchdog state for monitoring diagnostics. */
void mqtt_get_availability_snapshot(MQTTAvailabilitySnapshot* out);
