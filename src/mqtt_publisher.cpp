#include "mqtt_publisher.h"
#include "config.h"
#include "reg_groups.h"
#include "UnifiedLogger.h"
#include <ArduinoJson.h>
#include <WiFi.h>
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

// ============================================================
// Device announcement (published once per (re)connect)
// ============================================================
static bool s_device_announced = false;

// Rate-limiter for HA discovery bursts: publish at most this many
// discovery configs per mqtt_tick() call to avoid flooding the broker.
static constexpr int HA_DISCOVERY_PER_TICK = 5;
static constexpr uint8_t SRC_FC03_ID      = 1;
static constexpr uint8_t SRC_FC04_ID      = 2;
static constexpr uint8_t SRC_H41_SUB33_ID = 3;
static constexpr uint8_t SRC_H41_OTHER_ID = 4;

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

static bool is_unconfirmed_register(const char* name) {
    if (!name) return false;
    if (strcmp(name, "off_grid_mode") == 0) return true;
    return strncmp(name, "h41_", 4) == 0;
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
// Topic:   homeassistant/sensor/<client_id>_<name>/config  (retained)
// Payload: HA sensor discovery JSON (state_topic, value_template, unit,
//          unique_id, availability_topic, device block).
static void mqtt_publish_ha_discovery(int idx) {
    if (!s_mgr || !s_cfg) return;
    const ValCache& c = s_cache[idx];
    if (!c.valid || c.grp_idx >= GRP_COUNT) return;

    // Keep HA entity model stable: only one discovery entry per logical register name.
    for (int j = 0; j < idx; j++) {
        if (!s_cache[j].valid) continue;
        if (strcmp(s_cache[j].name, c.name) == 0) return;
    }

    const RegGroupInfo& gi = GROUP_INFO[c.grp_idx];

    // Discovery topic
    char disc_topic[128];
    snprintf(disc_topic, sizeof(disc_topic),
             "homeassistant/sensor/%s_%s/config",
             s_cfg->mqtt.client_id.c_str(), c.name);

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
    char unique_id[80];
    snprintf(unique_id, sizeof(unique_id),
             "%s_%s", s_cfg->mqtt.client_id.c_str(), c.name);

    // value_template to extract this register from the group JSON
    char val_tpl[72];
    snprintf(val_tpl, sizeof(val_tpl), "{{ value_json.%s }}", c.name);

    JsonDocument doc;
    doc["name"]              = c.name;
    doc["state_topic"]       = state_topic;
    doc["value_template"]    = val_tpl;
    doc["unique_id"]         = unique_id;
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
        s_avail_offline[g] = false;
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
    memset(s_last_seen_ms,     0, sizeof(s_last_seen_ms));
    memset(s_avail_offline,    0, sizeof(s_avail_offline));
    s_cache_count      = 0;
    s_device_announced = false;
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
        for (int i = 0; i < s_cache_count; i++)
            s_cache[i].ha_discovered = false;
    }
    // Announce device info on first connect (and every reconnect)
    if (connected && !s_device_announced && s_cfg) {
        mqtt_announce_device();
        s_device_announced = true;
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

    // Silently drop values for disabled groups â€” nothing touches the cache
    // or the buffer so the group stays invisible to everything.
    if (!s_cfg->publish.group_enabled[group]) return;

    // 1. Update the value cache (dashboard API source â€” always kept current).
    update_cache(name, value, unit, slave_addr, (uint8_t)group, source_id,
                 reg_addr, reg_words);

    // 2. Accumulate into the per-group JSON buffer.
    //    mqtt_tick() decides WHEN to flush based on tier intervals.
    GroupBuffer* buf = get_group_buf(group);
    if (buf) buf->doc[name] = value;

    // 3. Mark this group as recently active for the availability watchdog.
    uint32_t now = millis();
    if (now >= s_last_seen_ms[group])
        s_last_seen_ms[group] = now;
    s_avail_offline[group] = false;
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

    // ---- Per-group availability watchdog ----
    // If a group goes silent for > 3Ã— its tier interval â†’ publish "offline".
    // Only applies to enabled groups that have previously been seen.
    if (!s_mgr->isConnected()) return;

    for (int g = 0; g < GRP_COUNT; g++) {
        if (!s_cfg->publish.group_enabled[g]) continue; // disabled = silent
        if (s_last_seen_ms[g] == 0) continue;           // never seen = no availability
        if (s_avail_offline[g])     continue;           // already offline

        uint8_t  tier        = s_cfg->publish.group_tier[g];
        if (tier >= TIER_COUNT) continue;
        uint32_t watchdog_ms = (uint32_t)s_cfg->publish.tier_interval_s[tier] * 4000;
        if (watchdog_ms == 0) continue;

        if ((now - s_last_seen_ms[g]) >= watchdog_ms) {
            char avail[96];
            snprintf(avail, sizeof(avail), "%s/%s/available",
                     s_cfg->mqtt.base_topic.c_str(), GROUP_INFO[g].key);
            s_mgr->publish(avail, "offline", /*retained*/true, /*qos*/0);
            s_avail_offline[g] = true;
            UnifiedLogger::info("[MQTT] group '%s' \u2192 offline (last seen %us ago, thresh %us)\n",
                                GROUP_INFO[g].label,
                                (unsigned)((now - s_last_seen_ms[g]) / 1000),
                                (unsigned)(watchdog_ms / 1000));
        }
    }
}

String mqtt_get_last_values_json() {
    JsonDocument doc;
    bool metrics = s_stats && s_cfg && s_cfg->debug.sensor_refresh_metrics;
    uint32_t nowMs = millis();
    for (int i = 0; i < s_cache_count; i++) {
        if (!s_cache[i].valid) continue;
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
