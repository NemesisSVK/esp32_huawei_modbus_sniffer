#pragma once
#include "reg_groups.h"
#include "MQTTManager.h"
#include "ConfigManager.h"
#include <stdint.h>

/**
 * Initialize the MQTT publisher.
 * Must be called AFTER ConfigManager and WiFi are ready.
 */
void mqtt_publisher_init(MQTTManager* mgr, const Settings* cfg);

/** Call from loop() — MQTTManager keepalive + reconnect. */
void mqtt_loop();

/** True if broker is currently connected. */
bool mqtt_connected();

/**
 * Decoder callback — accumulate a decoded register value.
 * JSON mode : always accumulates into the per-group buffer; publication is
 *             triggered by mqtt_tick() based on tier intervals.
 * Individual mode: publishes immediately, gated by 0.5% change detection.
 */
void mqtt_publish_value(const char* name, float value,
                        const char* unit, uint8_t slave_addr,
                        RegGroup group, uint8_t source_id,
                        uint16_t reg_addr, uint8_t reg_words);

/**
 * Tiered flush loop — call every loop() iteration.
 *
 *  • Per-tier timer: when elapsed >= tier_interval_s, flush all groups
 *    assigned to that tier. LOW-tier groups are published with retained=true.
 *  • Per-group availability watchdog: if a group's last-seen time exceeds
 *    3 × its tier interval, publishes "offline" to <base>/<key>/available
 *    (retained). Resets to "online" on the next successful group flush.
 */
void mqtt_tick();

/**
 * Returns a JSON string of all last-seen decoded values.
 * Format: {"subtopic/register@source#slave": {"v": 3.14, "u": "W", "slave": 1, "group": "meter", "src": "H41-33"}, ...}
 * Used by /api/values to populate dashboard cards.
 */
String mqtt_get_last_values_json();
