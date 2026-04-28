#include "mqtt_publisher.h"
#include "huawei_decoder.h"
#include "config.h"
#include "reg_groups.h"
#include "UnifiedLogger.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESP.h>
#include <LittleFS.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif
#include <math.h>
#include <string.h>

// ============================================================
// Module-level references (set by mqtt_publisher_init)
// ============================================================
static MQTTManager*    s_mgr = nullptr;
static const Settings* s_cfg = nullptr;

// ============================================================
// Per-group JSON accumulator
// JSON is the only publish mode. Each enabled group accumulates
// decoded values here between tier ticks.
// Uses default (internal SRAM) allocator â€” GroupBuffer lifetime
// is managed as a static array initialized before PSRAM is ready.
// Pools are small (~few KB peak) and cleared after every tier tick.
// ============================================================
struct GroupBuffer {
    RegGroup     group;
    bool         active;
    JsonDocument doc;
};

static GroupBuffer s_bufs[GRP_COUNT];

// ============================================================
// Last-value cache â€” dashboard API source
// Placed in PSRAM (EXT_RAM_ATTR) â€” 512 x 64B = 32KB from the
// 8MB OPI PSRAM, leaving internal heap free for stack/OS.
// 512 slots covers ~230 current registers with headroom for growth.
// ============================================================
struct ValCache {
    char     name[48];
    char     unit[8];
    float    value;
    uint32_t last_seen_ms;   // millis() of last value update â€” always maintained
    uint16_t reg_addr;
    uint8_t  reg_words;
    uint8_t  slave_addr;
    uint8_t  grp_idx;
    uint8_t  source_id;
    bool     valid;
    bool     ha_discovered;  // true once HA discovery config has been published
};

#define MAX_CACHED_VALS 512
static EXT_RAM_ATTR ValCache s_cache[MAX_CACHED_VALS];
static uint16_t  s_cache_count = 0;

// ============================================================
// Per-sensor refresh interval statistics (PSRAM-backed)
// Tracks the time between consecutive updates for each register.
// Only accumulated when Settings::Debug::sensor_refresh_metrics is true.
// RS_RING_SZ timestamps cover >= 1 hour for any tier interval.
// ============================================================
#define RS_RING_SZ 120   // 120 timestamps â†’ 1 hr at 30 s intervals
struct RefreshStats {
    uint32_t min_interval;        // ms â€” smallest observed interval  (0 = unset)
    uint32_t max_interval;        // ms â€” largest  observed interval
    uint32_t ring[RS_RING_SZ];    // circular ring of millis() timestamps
    uint8_t  head;                // index where NEXT write goes
    uint8_t  count;               // valid entries capped at RS_RING_SZ
};
// RS_RING_SZ=120: sizeof(RefreshStats)â‰ˆ490 B; 512 sensors â†’ ~245 KB SPIRAM
// Allocated dynamically in mqtt_publisher_init() via heap_caps_calloc(MALLOC_CAP_SPIRAM)
// because EXT_RAM_ATTR silently falls back to dram0.bss on this toolchain version.
static RefreshStats* s_stats = nullptr;

// ============================================================
// Tier tick state â€” one last-flush timestamp per tier
// ============================================================
static uint32_t s_last_tier_tick_ms[TIER_COUNT];

// ============================================================
// Per-group availability tracking
//   s_last_seen_ms[g]  â€” millis() of last decoded value for group g
//                         0 = group has never produced data (no availability published)
//   s_avail_offline[g] â€” true once "offline" has been published; prevents
//                         repeated offline publishes while data remains absent
// ============================================================
static uint32_t s_last_seen_ms[GRP_COUNT];
static bool     s_avail_offline[GRP_COUNT];
static uint32_t s_avail_online_transitions[GRP_COUNT];
static uint32_t s_avail_offline_transitions[GRP_COUNT];
static uint32_t s_avail_last_online_ms[GRP_COUNT];
static uint32_t s_avail_last_offline_ms[GRP_COUNT];
#if defined(ARDUINO_ARCH_ESP32)
static portMUX_TYPE s_avail_lock = portMUX_INITIALIZER_UNLOCKED;
#endif
static constexpr uint32_t AVAILABILITY_WATCHDOG_MIN_MS = 120000UL; // 120s global floor
static constexpr uint32_t PRIORITY_MANUAL_WATCHDOG_MS = 120000UL; // 120s

// ============================================================
// Device announcement (published once per (re)connect)
// ============================================================
static bool s_device_announced = false;
static bool s_diag_discovered = false;
static uint32_t s_last_diag_publish_ms = 0;
static uint32_t s_last_diag_discovery_attempt_ms = 0;
static uint32_t s_diag_failure_log_ms = 0;
static constexpr uint32_t DIAG_PUBLISH_INTERVAL_MS = 60000UL; // fixed 60s diagnostics cadence
static constexpr uint32_t DIAG_DISCOVERY_RETRY_MS = 5000UL;   // retry discovery on transient publish failures

// Rate-limiter for HA discovery bursts: publish at most this many
// discovery configs per mqtt_tick() call to avoid flooding the broker.
static constexpr int HA_DISCOVERY_PER_TICK = 5;
static constexpr uint8_t SRC_FC03_ID      = 1;
static constexpr uint8_t SRC_FC04_ID      = 2;
static constexpr uint8_t SRC_H41_SUB33_ID = 3;
static constexpr uint8_t SRC_H41_OTHER_ID = 4;

static void to_ha_token(const char* in, char* out, size_t out_len);

struct DiagEntityDef {
    const char* key;
    const char* name;
    const char* unit;
    const char* device_class;
    const char* state_class;
};

static const DiagEntityDef DIAG_ENTITIES[] = {
    { "cpu_temp",             "CPU Temp",            "\xC2\xB0" "C", "temperature", "measurement" },
    { "memory_free_heap",     "Memory Free Heap",    "B",       nullptr,       "measurement" },
    { "littlefs_free_percent","LittleFS Free Space", "%",       nullptr,       "measurement" },
};

static const char* source_tag(uint8_t source_id) {
    switch (source_id) {
        case SRC_FC03_ID:      return "FC03";
        case SRC_FC04_ID:      return "FC04";
        case SRC_H41_SUB33_ID: return "H41-33";
        case SRC_H41_OTHER_ID: return "H41-X";
        default:            return "UNK";
    }
}

static const char* source_icon(uint8_t source_id) {
    switch (source_id) {
        case SRC_FC03_ID:
        case SRC_FC04_ID:      return "DOC";
        case SRC_H41_SUB33_ID:
        case SRC_H41_OTHER_ID: return "REV";
        default:            return "UNK";
    }
}

static inline void lock_avail_state() {
#if defined(ARDUINO_ARCH_ESP32)
    portENTER_CRITICAL(&s_avail_lock);
#endif
}

static inline void unlock_avail_state() {
#if defined(ARDUINO_ARCH_ESP32)
    portEXIT_CRITICAL(&s_avail_lock);
#endif
}

static uint32_t group_watchdog_ms(const Settings* cfg, int group_index) {
    if (!cfg || group_index < 0 || group_index >= GRP_COUNT) return 0;
    const uint8_t tier = cfg->publish.group_tier[group_index];
    if (tier >= TIER_COUNT) return 0;

    uint32_t watchdog_ms = (uint32_t)cfg->publish.tier_interval_s[tier] * 4000UL;
    if (watchdog_ms < AVAILABILITY_WATCHDOG_MIN_MS) {
        watchdog_ms = AVAILABILITY_WATCHDOG_MIN_MS;
    }
    if (group_index == (int)GRP_PRIORITY_MANUAL &&
        watchdog_ms < PRIORITY_MANUAL_WATCHDOG_MS) {
        watchdog_ms = PRIORITY_MANUAL_WATCHDOG_MS;
    }
    return watchdog_ms;
}

static bool should_log_diag_failure(uint32_t now) {
    if (s_diag_failure_log_ms == 0 || (now - s_diag_failure_log_ms) >= 60000UL) {
        s_diag_failure_log_ms = now;
        return true;
    }
    return false;
}

static bool mqtt_publish_diag_state_topic(const char* key, const String& payload) {
    if (!s_mgr || !s_cfg || !key) return false;
    char topic[112];
    snprintf(topic, sizeof(topic), "%s/diag/%s", s_cfg->mqtt.base_topic.c_str(), key);
    return s_mgr->publish(topic, payload.c_str(), /*retained*/false, /*qos*/0);
}

static bool mqtt_publish_diag_discovery_one(const DiagEntityDef& def) {
    if (!s_mgr || !s_cfg || !s_mgr->isConnected()) return false;

    char client_tok[64];
    to_ha_token(s_cfg->mqtt.client_id.c_str(), client_tok, sizeof(client_tok));

    char entity_full[160];
    snprintf(entity_full, sizeof(entity_full), "%s_diag_%s", client_tok, def.key);

    char config_topic[220];
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/sensor/%s/config", entity_full);

    char state_topic[112];
    snprintf(state_topic, sizeof(state_topic),
             "%s/diag/%s", s_cfg->mqtt.base_topic.c_str(), def.key);

    char availability_topic[96];
    snprintf(availability_topic, sizeof(availability_topic),
             "%s/status", s_cfg->mqtt.base_topic.c_str());

    char default_entity_id[180];
    snprintf(default_entity_id, sizeof(default_entity_id), "sensor.%s", entity_full);

    JsonDocument doc;
    doc["name"]              = def.name;
    doc["object_id"]         = entity_full;
    doc["state_topic"]       = state_topic;
    doc["value_template"]    = "{{ value_json.value }}";
    doc["unique_id"]         = entity_full;
    doc["default_entity_id"] = default_entity_id;
    doc["availability_topic"] = availability_topic;
    doc["force_update"]      = true;
    doc["entity_category"]   = "diagnostic";
    if (def.unit && def.unit[0] != '\0') {
        doc["unit_of_measurement"] = def.unit;
    }
    if (def.device_class && def.device_class[0] != '\0') {
        doc["device_class"] = def.device_class;
    }
    if (def.state_class && def.state_class[0] != '\0') {
        doc["state_class"] = def.state_class;
    }

    JsonObject dev = doc["device"].to<JsonObject>();
    JsonArray ids = dev["identifiers"].to<JsonArray>();
    ids.add(s_cfg->mqtt.client_id);
    dev["name"] = s_cfg->device_info.name;
    dev["manufacturer"] = s_cfg->device_info.manufacturer;
    dev["model"] = s_cfg->device_info.model;

    String payload;
    serializeJson(doc, payload);
    return s_mgr->publish(config_topic, payload.c_str(), /*retained*/true, /*qos*/0);
}

static bool mqtt_publish_diag_discovery() {
    if (!s_mgr || !s_cfg || !s_mgr->isConnected()) return false;

    bool all_ok = true;
    const uint32_t now = millis();
    for (size_t i = 0; i < (sizeof(DIAG_ENTITIES) / sizeof(DIAG_ENTITIES[0])); i++) {
        if (!mqtt_publish_diag_discovery_one(DIAG_ENTITIES[i])) {
            all_ok = false;
        }
    }
    if (!all_ok && should_log_diag_failure(now)) {
        UnifiedLogger::warning("[MQTT] diagnostics discovery publish incomplete\n");
    }
    return all_ok;
}

static bool mqtt_publish_diag_state_snapshot() {
    if (!s_mgr || !s_cfg || !s_mgr->isConnected()) return false;

    bool all_ok = true;
    const uint32_t now = millis();

    const float cpu_temp = temperatureRead();
    String cpu_payload;
    if (isnan(cpu_temp)) {
        cpu_payload = "{\"value\":null}";
    } else {
        cpu_payload = "{\"value\":" + String(cpu_temp, 1) + "}";
    }
    if (!mqtt_publish_diag_state_topic("cpu_temp", cpu_payload)) {
        all_ok = false;
    }

    const uint32_t free_heap = ESP.getFreeHeap();
    String heap_payload = "{\"value\":" + String(free_heap) + "}";
    if (!mqtt_publish_diag_state_topic("memory_free_heap", heap_payload)) {
        all_ok = false;
    }

    size_t total_bytes = LittleFS.totalBytes();
    if (total_bytes == 0) {
        if (LittleFS.begin(false)) {
            total_bytes = LittleFS.totalBytes();
        }
    }
    String fs_payload;
    if (total_bytes == 0) {
        fs_payload = "{\"value\":null}";
    } else {
        size_t used_bytes = LittleFS.usedBytes();
        if (used_bytes > total_bytes) used_bytes = total_bytes;
        const float free_percent =
            ((float)(total_bytes - used_bytes) * 100.0f) / (float)total_bytes;
        fs_payload = "{\"value\":" + String(free_percent, 1) + "}";
    }
    if (!mqtt_publish_diag_state_topic("littlefs_free_percent", fs_payload)) {
        all_ok = false;
    }

    if (!all_ok && should_log_diag_failure(now)) {
        UnifiedLogger::warning("[MQTT] diagnostics state publish incomplete\n");
    }
    return all_ok;
}

static bool is_unconfirmed_register(const char* name) {
    if (!name) return false;
    if (strcmp(name, "inverter_off_grid_mode") == 0) return true;
    return strncmp(name, "h41_", 4) == 0;
}

static bool manual_group_is_enabled() {
    if (!s_cfg) return false;
    return s_cfg->publish.manual_group.enabled &&
           !s_cfg->publish.manual_group.registers.empty();
}

static uint8_t manual_selector_source_to_id(String source) {
    source.trim();
    source.toLowerCase();
    source.replace("-", "_");
    if (source == "fc03")   return SRC_FC03_ID;
    if (source == "fc04")   return SRC_FC04_ID;
    if (source == "h41_33") return SRC_H41_SUB33_ID;
    if (source == "h41_x")  return SRC_H41_OTHER_ID;
    return 0;
}

static bool manual_group_matches_register(const char* name, uint8_t source_id) {
    if (!name || !manual_group_is_enabled()) return false;
    for (const String& raw : s_cfg->publish.manual_group.registers) {
        String selector = raw;
        selector.trim();
        if (selector.length() == 0) continue;

        const int sep = selector.indexOf(':');
        if (sep < 0) {
            if (selector == name) return true;
            continue;
        }
        if (sep == 0) continue;

        String src_token = selector.substring(0, sep);
        String reg_name = selector.substring(sep + 1);
        reg_name.trim();
        if (reg_name != name) continue;

        const uint8_t sel_src = manual_selector_source_to_id(src_token);
        if (sel_src != 0 && sel_src == source_id) return true;
    }
    return false;
}

static void to_ha_token(const char* in, char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    if (!in) { out[0] = '\0'; return; }
    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0' && oi + 1 < out_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[oi++] = (char)c;
        } else {
            out[oi++] = '_';
        }
    }
    out[oi] = '\0';
    // Collapse repeated underscores and trim trailing underscore.
    size_t wi = 0;
    bool last_us = false;
    for (size_t i = 0; out[i] != '\0'; i++) {
        char c = out[i];
        if (c == '_') {
            if (last_us) continue;
            last_us = true;
        } else {
            last_us = false;
        }
        out[wi++] = c;
    }
    while (wi > 0 && out[wi - 1] == '_') wi--;
    out[wi] = '\0';
}

static void build_ha_entity_key(const ValCache& c, char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    char grp_name[48];
    if (c.grp_idx == GRP_PRIORITY_MANUAL) {
        RegGroup natural = GRP_PRIORITY_MANUAL;
        if (huawei_decoder_get_register_group(c.name, &natural) && natural < GRP_COUNT) {
            snprintf(grp_name, sizeof(grp_name), "priority_%s", GROUP_INFO[(int)natural].mqtt_subtopic);
        } else {
            snprintf(grp_name, sizeof(grp_name), "priority");
        }
    } else {
        const char* grp = (c.grp_idx < GRP_COUNT) ? GROUP_INFO[c.grp_idx].mqtt_subtopic : "unknown";
        snprintf(grp_name, sizeof(grp_name), "%s", grp);
    }
    char grp_tok[32];
    char reg_tok[64];
    to_ha_token(grp_name, grp_tok, sizeof(grp_tok));
    to_ha_token(c.name, reg_tok, sizeof(reg_tok));
    snprintf(out, out_len, "%s_%s", grp_tok, reg_tok);
}

// ============================================================
// Internal helpers
// ============================================================

static GroupBuffer* get_group_buf(RegGroup g) {
    for (int i = 0; i < (int)GRP_COUNT; i++) {
        if (s_bufs[i].active && s_bufs[i].group == g) return &s_bufs[i];
    }
    for (int i = 0; i < (int)GRP_COUNT; i++) {
        if (!s_bufs[i].active) {
            s_bufs[i].active = true;
            s_bufs[i].group  = g;
            s_bufs[i].doc.clear();
            return &s_bufs[i];
        }
    }
    return nullptr;
}

// ============================================================
// Refresh-interval statistics helpers
// ============================================================

// Push a new timestamp and update min/max interval for cache entry [idx].
static void update_stats(int idx, uint32_t now) {
    RefreshStats& rs = s_stats[idx];
    // Compute interval from previous entry if any
    if (rs.count > 0) {
        int prev_pos = ((int)rs.head + RS_RING_SZ - 1) % RS_RING_SZ;
        uint32_t prev_ts = rs.ring[prev_pos];
        if (now > prev_ts) {
            uint32_t interval = now - prev_ts;
            if (rs.min_interval == 0 || interval < rs.min_interval)
                rs.min_interval = interval;
            if (interval > rs.max_interval)
                rs.max_interval = interval;
        }
    }
    // Push timestamp into ring
    rs.ring[rs.head] = now;
    rs.head = (uint8_t)((rs.head + 1) % RS_RING_SZ);
    if (rs.count < RS_RING_SZ) rs.count++;
}

// Compute the 1-hour rolling average update interval (seconds).
// Returns 0 if fewer than 2 samples exist within the last hour.
static float compute_avg_interval_s(const RefreshStats& rs) {
    if (rs.count < 2) return 0.0f;
    const uint32_t ONE_HOUR_MS = 3600000UL;
    uint32_t now    = millis();
    // Newest entry is at (head-1); ring is ordered oldestâ†’newest as we walk backward.
    int      newest_pos = ((int)rs.head + RS_RING_SZ - 1) % RS_RING_SZ;
    uint32_t newest     = rs.ring[newest_pos];
    if ((now - newest) > ONE_HOUR_MS) return 0.0f;  // most recent sample is stale
    int      valid      = 1;
    uint32_t oldest_in_hr = newest;
    for (int j = 1; j < (int)rs.count; j++) {
        int pos      = ((int)rs.head + RS_RING_SZ - 1 - j + RS_RING_SZ) % RS_RING_SZ;
        uint32_t ts  = rs.ring[pos];
        if ((newest - ts) <= ONE_HOUR_MS) {
            oldest_in_hr = ts;
            valid++;
        } else break;  // timestamps ordered oldestâ†’newest; can stop early
    }
    if (valid < 2 || newest == oldest_in_hr) return 0.0f;
    return ((float)(newest - oldest_in_hr) / (float)(valid - 1)) / 1000.0f;
}

// Update the value cache entry for a given register name.
// On first encounter: inserts a new entry.
// Calls update_stats() when sensor_refresh_metrics is enabled to track intervals.
static void update_cache(const char* name, float value,
                         const char* unit, uint8_t slave_addr,
                         uint8_t grp_idx, uint8_t source_id,
                         uint16_t reg_addr, uint8_t reg_words) {
    bool metrics = s_cfg && s_cfg->debug.sensor_refresh_metrics;
    uint32_t now = millis();   // always needed â€” drives last_seen_ms
    for (int i = 0; i < s_cache_count; i++) {
        if (!s_cache[i].valid) continue;
        if (strcmp(s_cache[i].name, name) != 0) continue;
        if (s_cache[i].source_id != source_id) continue;
        if (s_cache[i].slave_addr != slave_addr) continue;
        s_cache[i].value        = value;
        s_cache[i].slave_addr   = slave_addr;
        s_cache[i].grp_idx      = grp_idx;
        s_cache[i].reg_addr     = reg_addr;
        s_cache[i].reg_words    = reg_words;
        s_cache[i].last_seen_ms = now;
        if (unit) strncpy(s_cache[i].unit, unit, sizeof(s_cache[i].unit) - 1);
        if (metrics && s_stats) update_stats(i, now);
        return;
    }
    // First occurrence â€” insert new cache entry
    if (s_cache_count < MAX_CACHED_VALS) {
        int idx = s_cache_count++;
        ValCache& c = s_cache[idx];
        strncpy(c.name, name, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
        memset(c.unit, 0, sizeof(c.unit));
        if (unit) strncpy(c.unit, unit, sizeof(c.unit) - 1);
        c.value        = value;
        c.reg_addr     = reg_addr;
        c.reg_words    = reg_words;
        c.slave_addr   = slave_addr;
        c.grp_idx      = grp_idx;
        c.source_id    = source_id;
        c.valid        = true;
        c.last_seen_ms = now;
        if (metrics && s_stats) update_stats(idx, now);
    }
}

// ============================================================
// HA MQTT Auto-Discovery
// ============================================================

// Publish a single HA sensor discovery config for cache entry [idx].
// Topic:   homeassistant/sensor/<client_id>_<group>_<name>/config  (retained)
// Payload: HA sensor discovery JSON (state_topic, value_template, unit,
//          unique_id, availability_topic, device block).
static void mqtt_publish_ha_discovery(int idx) {
    if (!s_mgr || !s_cfg) return;
    const ValCache& c = s_cache[idx];
    if (!c.valid || c.grp_idx >= GRP_COUNT) return;

    char entity_key[96];
    build_ha_entity_key(c, entity_key, sizeof(entity_key));

    // Keep HA entity model stable: only one discovery entry per logical entity key.
    for (int j = 0; j < idx; j++) {
        if (!s_cache[j].valid) continue;
        char prev_key[96];
        build_ha_entity_key(s_cache[j], prev_key, sizeof(prev_key));
        if (strcmp(prev_key, entity_key) == 0) return;
    }

    const RegGroupInfo& gi = GROUP_INFO[c.grp_idx];
    char client_tok[64];
    to_ha_token(s_cfg->mqtt.client_id.c_str(), client_tok, sizeof(client_tok));
    char entity_full[160];
    snprintf(entity_full, sizeof(entity_full), "%s_%s", client_tok, entity_key);

    // Discovery topic
    char disc_topic[220];
    snprintf(disc_topic, sizeof(disc_topic),
             "homeassistant/sensor/%s/config", entity_full);

    // State topic the sensor reads from
    char state_topic[80];
    snprintf(state_topic, sizeof(state_topic),
             "%s/%s", s_cfg->mqtt.base_topic.c_str(), gi.mqtt_subtopic);

    // Availability topic (matches what the availability watchdog publishes)
    char avail_topic[96];
    snprintf(avail_topic, sizeof(avail_topic),
             "%s/%s/available",
             s_cfg->mqtt.base_topic.c_str(), gi.key);

    // Unique ID that ties entity to HA device registry entry
    char unique_id[160];
    snprintf(unique_id, sizeof(unique_id), "%s", entity_full);
    char default_entity_id[180];
    snprintf(default_entity_id, sizeof(default_entity_id), "sensor.%s", entity_full);

    // value_template to extract this register from the group JSON
    char val_tpl[72];
    snprintf(val_tpl, sizeof(val_tpl), "{{ value_json.%s }}", c.name);

    JsonDocument doc;
    doc["name"]              = entity_key;
    doc["object_id"]         = entity_full;
    doc["state_topic"]       = state_topic;
    doc["value_template"]    = val_tpl;
    doc["unique_id"]         = unique_id;
    doc["default_entity_id"] = default_entity_id;
    doc["availability_topic"] = avail_topic;
    if (c.unit[0] != '\0')
        doc["unit_of_measurement"] = c.unit;

    // Device block â€” groups all sensors under one HA device
    JsonObject dev = doc["device"].to<JsonObject>();
    JsonArray  ids = dev["identifiers"].to<JsonArray>();
    ids.add(s_cfg->mqtt.client_id);
    dev["name"]         = s_cfg->device_info.name;
    dev["manufacturer"] = s_cfg->device_info.manufacturer;
    dev["model"]        = s_cfg->device_info.model;

    String payload;
    serializeJson(doc, payload);
    s_mgr->publish(disc_topic, payload.c_str(), /*retained*/true, /*qos*/0);
    UnifiedLogger::verbose("[MQTT] HA discovery â†’ %s\n", disc_topic);
}

// Publish device info to <base_topic>/device (retained).
// Called once on every (re)connect so HA always has current IP/MAC after a restart.
static void mqtt_announce_device() {
    if (!s_mgr || !s_cfg) return;

    char topic[80];
    snprintf(topic, sizeof(topic), "%s/device", s_cfg->mqtt.base_topic.c_str());

    JsonDocument doc;
    doc["name"]         = s_cfg->device_info.name;
    doc["manufacturer"] = s_cfg->device_info.manufacturer;
    doc["model"]        = s_cfg->device_info.model;
    doc["client_id"]    = s_cfg->mqtt.client_id;
    doc["ip"]           = WiFi.localIP().toString();
    doc["mac"]          = WiFi.macAddress();

    String payload;
    serializeJson(doc, payload);
    s_mgr->publish(topic, payload.c_str(), /*retained*/true, /*qos*/0);
    UnifiedLogger::info("[MQTT] device announced â†’ %s\n", topic);
}

// Flush one group's JSON buffer to MQTT.
// LOW-tier groups are published retained; others are not.
// Publishes availability "online" (always retained) only when data was actually sent.
// Skipped entirely when the group is disabled (Settings::Publish::group_enabled[g] == false).
static void flush_group(RegGroup g, PublishTier tier) {
    if (!s_mgr || !s_mgr->isConnected() || !s_cfg) return;
    if (!s_cfg->publish.group_enabled[g]) return;  // group silenced â€” skip completely

    const char* subtopic = GROUP_INFO[g].mqtt_subtopic;
    const char* key      = GROUP_INFO[g].key;
    bool        retained = (tier == TIER_LOW);

    char topic[80];
    snprintf(topic, sizeof(topic), "%s/%s",
             s_cfg->mqtt.base_topic.c_str(), subtopic);

    GroupBuffer* buf = get_group_buf(g);
    if (buf && buf->doc.size() > 0) {
        String payload;
        serializeJson(buf->doc, payload);
        buf->doc.clear();  // free memory before publishing

        s_mgr->publish(topic, payload.c_str(), retained, /*qos*/0);
        UnifiedLogger::verbose("[MQTT] %s (%u B ret=%d)\n",
                               topic, (unsigned)payload.length(), (int)retained);

        // Availability "online" â€” only published when actual data was sent
        char avail[96];
        snprintf(avail, sizeof(avail), "%s/%s/available",
                 s_cfg->mqtt.base_topic.c_str(), key);
        s_mgr->publish(avail, "online", /*retained*/true, /*qos*/0);

        const uint32_t now = millis();
        lock_avail_state();
        if (s_avail_offline[g]) {
            s_avail_online_transitions[g]++;
            s_avail_last_online_ms[g] = now;
        } else if (s_avail_last_online_ms[g] == 0) {
            s_avail_last_online_ms[g] = now;
        }
        s_avail_offline[g] = false;
        unlock_avail_state();
    } else if (buf) {
        buf->doc.clear();
    }
}

// ============================================================
// Public API
// ============================================================

void mqtt_publisher_init(MQTTManager* mgr, const Settings* cfg) {
    s_mgr = mgr;
    s_cfg = cfg;
    // Reset GroupBuffer array safely â€” GroupBuffer contains JsonDocument (C++ object).
    // memset would corrupt its internal pointers; use explicit per-element reset instead.
    for (int i = 0; i < (int)GRP_COUNT; i++) {
        s_bufs[i].active = false;
        s_bufs[i].group  = (RegGroup)0;
        s_bufs[i].doc.clear();
    }
    memset(s_cache,            0, sizeof(s_cache));   // POD struct â€” safe to memset
    // Allocate (or re-zero) the refresh-stats array in SPIRAM.
    // heap_caps_calloc is used instead of EXT_RAM_ATTR because the static attribute
    // falls back to dram0.bss on this toolchain, overflowing the 320 KB DRAM segment.
    if (!s_stats) {
        s_stats = (RefreshStats*)heap_caps_calloc(
                      MAX_CACHED_VALS, sizeof(RefreshStats),
                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        memset(s_stats, 0, sizeof(RefreshStats) * MAX_CACHED_VALS);
    }
    memset(s_last_tier_tick_ms,0, sizeof(s_last_tier_tick_ms));
    lock_avail_state();
    memset(s_last_seen_ms,     0, sizeof(s_last_seen_ms));
    memset(s_avail_offline,    0, sizeof(s_avail_offline));
    memset(s_avail_online_transitions,  0, sizeof(s_avail_online_transitions));
    memset(s_avail_offline_transitions, 0, sizeof(s_avail_offline_transitions));
    memset(s_avail_last_online_ms,      0, sizeof(s_avail_last_online_ms));
    memset(s_avail_last_offline_ms,     0, sizeof(s_avail_last_offline_ms));
    unlock_avail_state();
    s_cache_count      = 0;
    s_device_announced = false;
    s_diag_discovered = false;
    s_last_diag_publish_ms = 0;
    s_last_diag_discovery_attempt_ms = 0;
    s_diag_failure_log_ms = 0;
}

void mqtt_loop() {
    if (!s_mgr) return;
    s_mgr->loop();

    bool connected = s_mgr->isConnected();
    if (WiFi.status() == WL_CONNECTED && !connected) {
        s_mgr->reconnect();
        // On disconnect: reset announce + discovery flags so everything
        // is re-published on the next connect (handles HA/broker restarts).
        s_device_announced = false;
        s_diag_discovered = false;
        s_last_diag_publish_ms = 0;
        s_last_diag_discovery_attempt_ms = 0;
        for (int i = 0; i < s_cache_count; i++)
            s_cache[i].ha_discovered = false;
    }
    // Announce device info on first connect (and every reconnect)
    if (connected && !s_device_announced && s_cfg) {
        mqtt_announce_device();
        s_device_announced = true;
    }

    if (connected && s_cfg) {
        const uint32_t now = millis();
        if (!s_diag_discovered &&
            (s_last_diag_discovery_attempt_ms == 0 ||
             (now - s_last_diag_discovery_attempt_ms) >= DIAG_DISCOVERY_RETRY_MS)) {
            s_last_diag_discovery_attempt_ms = now;
            s_diag_discovered = mqtt_publish_diag_discovery();
        }
        // Immediate diagnostics snapshot on first connected loop (also after reconnect).
        if (s_last_diag_publish_ms == 0) {
            mqtt_publish_diag_state_snapshot();
            s_last_diag_publish_ms = now;
        }
    }
}

bool mqtt_connected() {
    return s_mgr && s_mgr->isConnected();
}

void mqtt_publish_value(const char* name, float value,
                        const char* unit, uint8_t slave_addr,
                        RegGroup group, uint8_t source_id,
                        uint16_t reg_addr, uint8_t reg_words) {
    if (!s_cfg) return;

    RegGroup routed_group = group;
    if (manual_group_matches_register(name, source_id)) {
        routed_group = GRP_PRIORITY_MANUAL;
    }

    // Silently drop values for disabled groups â€” nothing touches the cache
    // or the buffer so the group stays invisible to everything.
    if (!s_cfg->publish.group_enabled[routed_group]) return;

    // 1. Update the value cache (dashboard API source â€” always kept current).
    update_cache(name, value, unit, slave_addr, (uint8_t)routed_group, source_id,
                 reg_addr, reg_words);

    // 2. Accumulate into the per-group JSON buffer.
    //    mqtt_tick() decides WHEN to flush based on tier intervals.
    GroupBuffer* buf = get_group_buf(routed_group);
    if (buf) buf->doc[name] = value;

    // 3. Mark this group as recently active for the availability watchdog.
    uint32_t now = millis();
    lock_avail_state();
    s_last_seen_ms[routed_group] = now;
    s_avail_offline[routed_group] = false;
    unlock_avail_state();
}

void mqtt_tick() {
    if (!s_mgr || !s_cfg) return;
    uint32_t now = millis();

    // ---- Per-tier flush ----
    // Each tier has an independent interval timer. When it fires, all groups
    // assigned to that tier are flushed. Disabled groups are skipped inside flush_group().
    for (int tier = 0; tier < TIER_COUNT; tier++) {
        uint32_t interval_ms = (uint32_t)s_cfg->publish.tier_interval_s[tier] * 1000;
        if (interval_ms == 0) continue;
        if ((now - s_last_tier_tick_ms[tier]) < interval_ms) continue;

        s_last_tier_tick_ms[tier] = now;

        if (!s_mgr->isConnected()) continue;

        for (int g = 0; g < GRP_COUNT; g++) {
            if (s_cfg->publish.group_tier[g] != (uint8_t)tier) continue;
            flush_group((RegGroup)g, (PublishTier)tier);
        }
    }

    // ---- HA Auto-Discovery pass ----
    // After tier-flush, scan for cache entries not yet announced to HA.
    // Rate-limited to HA_DISCOVERY_PER_TICK per call to avoid broker flooding.
    if (s_mgr->isConnected() && s_cfg) {
        int sent = 0;
        for (int i = 0; i < s_cache_count && sent < HA_DISCOVERY_PER_TICK; i++) {
            if (!s_cache[i].valid)          continue;
            if (s_cache[i].ha_discovered)   continue;
            if (!s_cfg->publish.group_enabled[s_cache[i].grp_idx]) continue;
            mqtt_publish_ha_discovery(i);
            s_cache[i].ha_discovered = true;
            sent++;
        }
    }

    if (s_mgr->isConnected() && s_cfg) {
        if (!s_diag_discovered &&
            (s_last_diag_discovery_attempt_ms == 0 ||
             (now - s_last_diag_discovery_attempt_ms) >= DIAG_DISCOVERY_RETRY_MS)) {
            s_last_diag_discovery_attempt_ms = now;
            s_diag_discovered = mqtt_publish_diag_discovery();
        }
        if (s_last_diag_publish_ms == 0 ||
            (now - s_last_diag_publish_ms) >= DIAG_PUBLISH_INTERVAL_MS) {
            mqtt_publish_diag_state_snapshot();
            s_last_diag_publish_ms = now;
        }
    }

    // ---- Per-group availability watchdog ----
    // If a group goes silent for > 3Ã— its tier interval â†’ publish "offline".
    // Only applies to enabled groups that have previously been seen.
    if (!s_mgr->isConnected()) return;

    for (int g = 0; g < GRP_COUNT; g++) {
        if (!s_cfg->publish.group_enabled[g]) continue; // disabled = silent
        uint32_t watchdog_ms = group_watchdog_ms(s_cfg, g);
        if (watchdog_ms == 0) continue;

        uint32_t last_seen_snapshot = 0;
        bool already_offline = false;
        lock_avail_state();
        last_seen_snapshot = s_last_seen_ms[g];
        already_offline = s_avail_offline[g];
        unlock_avail_state();

        if (last_seen_snapshot == 0) continue; // never seen = no availability
        if (already_offline) continue;         // already offline
        if ((now - last_seen_snapshot) < watchdog_ms) continue;

        // Cross-core race guard: re-check immediately before publishing offline.
        const uint32_t now2 = millis();
        uint32_t stale_age_ms = 0;
        bool should_publish_offline = false;
        lock_avail_state();
        if (!s_avail_offline[g]) {
            uint32_t last_seen_confirm = s_last_seen_ms[g];
            if (last_seen_confirm != 0 && (now2 - last_seen_confirm) >= watchdog_ms) {
                s_avail_offline[g] = true;
                s_avail_offline_transitions[g]++;
                s_avail_last_offline_ms[g] = now2;
                stale_age_ms = now2 - last_seen_confirm;
                should_publish_offline = true;
            }
        }
        unlock_avail_state();
        if (!should_publish_offline) continue;

        char avail[96];
        snprintf(avail, sizeof(avail), "%s/%s/available",
                 s_cfg->mqtt.base_topic.c_str(), GROUP_INFO[g].key);
        s_mgr->publish(avail, "offline", /*retained*/true, /*qos*/0);
        UnifiedLogger::info("[MQTT] group '%s' \u2192 offline (last seen %us ago, thresh %us)\n",
                            GROUP_INFO[g].label,
                            (unsigned)(stale_age_ms / 1000UL),
                            (unsigned)(watchdog_ms / 1000UL));
    }
}

static String mqtt_get_values_json_for_group(int group_filter) {
    JsonDocument doc;
    bool metrics = s_stats && s_cfg && s_cfg->debug.sensor_refresh_metrics;
    uint32_t nowMs = millis();
    for (int i = 0; i < s_cache_count; i++) {
        if (!s_cache[i].valid) continue;
        if (group_filter >= 0 && s_cache[i].grp_idx != (uint8_t)group_filter) continue;
        const char* grp = (s_cache[i].grp_idx < GRP_COUNT)
                          ? GROUP_INFO[s_cache[i].grp_idx].mqtt_subtopic
                          : "unknown";
        const char* src = source_tag(s_cache[i].source_id);
        char key[112];
        snprintf(key, sizeof(key), "%s/%s@%s#%u", grp, s_cache[i].name, src,
                 (unsigned)s_cache[i].slave_addr);
        doc[key]["v"]     = s_cache[i].value;
        doc[key]["u"]     = s_cache[i].unit;
        doc[key]["slave"] = s_cache[i].slave_addr;
        doc[key]["group"] = grp;
        doc[key]["name"]  = s_cache[i].name;
        doc[key]["src"]   = src;
        doc[key]["src_icon"] = source_icon(s_cache[i].source_id);
        doc[key]["reg"]   = s_cache[i].reg_addr;
        doc[key]["reg_words"] = s_cache[i].reg_words;
        if (s_cache[i].reg_words > 1) {
            doc[key]["reg_end"] = (uint16_t)(s_cache[i].reg_addr + s_cache[i].reg_words - 1U);
        }
        if (is_unconfirmed_register(s_cache[i].name))
            doc[key]["vtag"] = "NEW";
        // Always include staleness â€” independent of sensor_refresh_metrics
        if (s_cache[i].last_seen_ms > 0)
            doc[key]["age_s"] = (uint32_t)((nowMs - s_cache[i].last_seen_ms) / 1000UL);
        if (metrics) {
            const RefreshStats& rs = s_stats[i];
            if (rs.count >= 2 && rs.min_interval > 0) {
                float avg = compute_avg_interval_s(rs);
                const uint32_t min_ms = rs.min_interval;
                const uint32_t max_ms = rs.max_interval;
                const uint32_t avg_ms = (avg > 0.0f) ? (uint32_t)roundf(avg * 1000.0f) : 0;
                doc[key]["min_ms"] = min_ms;
                doc[key]["max_ms"] = max_ms;
                doc[key]["avg_ms"] = avg_ms;
                // Backward-compatible rounded second fields for existing consumers.
                doc[key]["min_s"] = (int)roundf((float)min_ms / 1000.0f);
                doc[key]["max_s"] = (int)roundf((float)max_ms / 1000.0f);
                doc[key]["avg_s"] = (int)roundf((float)avg_ms / 1000.0f);
            }
        }
    }
    String out;
    serializeJson(doc, out);
    return out;
}

String mqtt_get_last_values_json() {
    return mqtt_get_values_json_for_group(-1);
}

String mqtt_get_priority_values_json() {
    return mqtt_get_values_json_for_group((int)GRP_PRIORITY_MANUAL);
}

void mqtt_get_availability_snapshot(MQTTAvailabilitySnapshot* out) {
    if (!out) return;
    lock_avail_state();
    for (int g = 0; g < GRP_COUNT; g++) {
        out->groups[g].last_seen_ms = s_last_seen_ms[g];
        out->groups[g].last_online_ms = s_avail_last_online_ms[g];
        out->groups[g].last_offline_ms = s_avail_last_offline_ms[g];
        out->groups[g].online_transitions = s_avail_online_transitions[g];
        out->groups[g].offline_transitions = s_avail_offline_transitions[g];
        out->groups[g].offline = s_avail_offline[g];
    }
    unlock_avail_state();
}
