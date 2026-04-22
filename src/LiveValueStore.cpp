#include "LiveValueStore.h"
#include <ArduinoJson.h>
#include <string.h>

namespace {

enum LiveBucket : uint8_t {
    LIVE_BUCKET_INVERTER = 0,
    LIVE_BUCKET_BATTERY  = 1,
    LIVE_BUCKET_METER    = 2,
    LIVE_BUCKET_OTHER    = 3,
};

struct LiveCache {
    char     id[40];
    char     name[48];
    char     unit[8];
    char     value_str[24];
    float    value;
    uint32_t last_seen_ms;
    uint32_t last_update_ms;
    uint32_t min_interval_ms;
    uint32_t max_interval_ms;
    uint64_t interval_sum_ms;
    uint32_t interval_count;
    uint32_t update_seq;
    uint16_t reg_addr;
    uint8_t  reg_words;
    uint8_t  slave_addr;
    uint8_t  source_id;
    uint8_t  bucket;
    bool     valid;
    bool     known;
    bool     has_numeric;
    bool     tag_new;
    bool     tag_unk;
};

static constexpr uint8_t SRC_FC03_ID      = 1;
static constexpr uint8_t SRC_FC04_ID      = 2;
static constexpr uint8_t SRC_H41_SUB33_ID = 3;
static constexpr uint8_t SRC_H41_OTHER_ID = 4;

static constexpr int MAX_LIVE_CACHED_VALS = 1024;
static LiveCache* s_live_cache = nullptr;
static uint16_t s_live_cache_count = 0;
static uint32_t s_live_update_seq = 0;

static const char* source_tag(uint8_t source_id) {
    switch (source_id) {
        case SRC_FC03_ID:      return "FC03";
        case SRC_FC04_ID:      return "FC04";
        case SRC_H41_SUB33_ID: return "H41-33";
        case SRC_H41_OTHER_ID: return "H41-X";
        default:               return "UNK";
    }
}

static const char* source_icon(uint8_t source_id) {
    switch (source_id) {
        case SRC_FC03_ID:
        case SRC_FC04_ID:      return "DOC";
        case SRC_H41_SUB33_ID:
        case SRC_H41_OTHER_ID: return "REV";
        default:               return "UNK";
    }
}

static bool is_unconfirmed_register(const char* name) {
    if (!name) return false;
    if (strcmp(name, "off_grid_mode") == 0) return true;
    return strncmp(name, "h41_", 4) == 0;
}

static const char* live_bucket_key(uint8_t bucket) {
    switch (bucket) {
        case LIVE_BUCKET_INVERTER: return "inverter";
        case LIVE_BUCKET_BATTERY:  return "battery";
        case LIVE_BUCKET_METER:    return "meter";
        default:                   return "other";
    }
}

static uint8_t map_group_to_live_bucket(RegGroup group) {
    switch (group) {
        case GRP_METER:
            return LIVE_BUCKET_METER;

        case GRP_BATTERY:
        case GRP_BATTERY_UNIT1:
        case GRP_BATTERY_UNIT2:
        case GRP_BATTERY_PACKS:
        case GRP_BATTERY_SETTINGS:
            return LIVE_BUCKET_BATTERY;

        case GRP_INVERTER_AC:
        case GRP_INVERTER_STATUS:
        case GRP_INVERTER_ENERGY:
        case GRP_INVERTER_INFO:
        case GRP_PV_STRINGS:
        case GRP_SDONGLE:
            return LIVE_BUCKET_INVERTER;
        default:
            return LIVE_BUCKET_OTHER;
    }
}

static uint8_t infer_bucket_from_reg(uint16_t reg_addr) {
    if ((reg_addr >= 37000 && reg_addr <= 38999) ||
        (reg_addr >= 47000 && reg_addr <= 47999)) {
        return LIVE_BUCKET_BATTERY;
    }
    if ((reg_addr >= 37100 && reg_addr <= 37199) ||
        (reg_addr >= 16300 && reg_addr <= 16399)) {
        return LIVE_BUCKET_METER;
    }
    if ((reg_addr >= 30000 && reg_addr <= 36999) ||
        (reg_addr >= 40000 && reg_addr <= 42999)) {
        return LIVE_BUCKET_INVERTER;
    }
    return LIVE_BUCKET_OTHER;
}

static void make_live_id(char* out, size_t out_sz,
                         uint8_t source_id, uint8_t slave_addr,
                         uint16_t reg_addr, uint8_t reg_words) {
    snprintf(out, out_sz, "%u#%u@%u:%u",
             (unsigned)source_id, (unsigned)slave_addr,
             (unsigned)reg_addr, (unsigned)reg_words);
}

static int find_live_idx(const char* id) {
    if (!s_live_cache) return -1;
    for (int i = 0; i < s_live_cache_count; i++) {
        if (!s_live_cache[i].valid) continue;
        if (strcmp(s_live_cache[i].id, id) == 0) return i;
    }
    return -1;
}

static void live_update_intervals(LiveCache& c, uint32_t now_ms) {
    if (c.last_update_ms > 0 && now_ms > c.last_update_ms) {
        uint32_t dt = now_ms - c.last_update_ms;
        if (c.min_interval_ms == 0 || dt < c.min_interval_ms) c.min_interval_ms = dt;
        if (dt > c.max_interval_ms) c.max_interval_ms = dt;
        c.interval_sum_ms += dt;
        c.interval_count++;
    }
    c.last_update_ms = now_ms;
}

static LiveCache* alloc_live_entry() {
    if (!s_live_cache) return nullptr;
    if (s_live_cache_count < MAX_LIVE_CACHED_VALS) {
        return &s_live_cache[s_live_cache_count++];
    }
    int oldest_idx = -1;
    uint32_t oldest_ms = UINT32_MAX;
    for (int i = 0; i < s_live_cache_count; i++) {
        if (!s_live_cache[i].valid) continue;
        if (s_live_cache[i].last_seen_ms < oldest_ms) {
            oldest_ms = s_live_cache[i].last_seen_ms;
            oldest_idx = i;
        }
    }
    if (oldest_idx < 0) return nullptr;
    return &s_live_cache[oldest_idx];
}

} // namespace

void live_value_store_init() {
    if (!s_live_cache) {
        s_live_cache = (LiveCache*)heap_caps_calloc(
            MAX_LIVE_CACHED_VALS, sizeof(LiveCache),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        memset(s_live_cache, 0, sizeof(LiveCache) * MAX_LIVE_CACHED_VALS);
    }
    s_live_cache_count = 0;
    s_live_update_seq = 0;
}

void live_value_store_publish_known(const char* name, float value,
                                    const char* unit, uint8_t slave_addr,
                                    RegGroup group, uint8_t source_id,
                                    uint16_t reg_addr, uint8_t reg_words) {
    if (!s_live_cache) return;

    char id[40];
    make_live_id(id, sizeof(id), source_id, slave_addr, reg_addr, reg_words);
    const uint32_t now = millis();
    const int idx = find_live_idx(id);
    LiveCache* c = (idx >= 0) ? &s_live_cache[idx] : alloc_live_entry();
    if (!c) return;
    if (idx < 0) {
        memset(c, 0, sizeof(*c));
        strncpy(c->id, id, sizeof(c->id) - 1);
    }
    live_update_intervals(*c, now);
    strncpy(c->name, name ? name : "unknown", sizeof(c->name) - 1);
    c->name[sizeof(c->name) - 1] = '\0';
    memset(c->unit, 0, sizeof(c->unit));
    if (unit) strncpy(c->unit, unit, sizeof(c->unit) - 1);
    c->value = value;
    c->has_numeric = true;
    c->value_str[0] = '\0';
    c->last_seen_ms = now;
    c->reg_addr = reg_addr;
    c->reg_words = reg_words;
    c->slave_addr = slave_addr;
    c->source_id = source_id;
    c->bucket = map_group_to_live_bucket(group);
    c->known = true;
    c->tag_new = is_unconfirmed_register(name);
    c->tag_unk = false;
    c->valid = true;
    c->update_seq = ++s_live_update_seq;
}

void live_value_store_publish_unknown_u16(uint16_t reg_addr, uint8_t reg_words,
                                          uint16_t raw_u16, uint8_t slave_addr,
                                          uint8_t source_id) {
    if (!s_live_cache) return;

    char id[40];
    make_live_id(id, sizeof(id), source_id, slave_addr, reg_addr, reg_words);
    const uint32_t now = millis();
    const int idx = find_live_idx(id);
    LiveCache* c = (idx >= 0) ? &s_live_cache[idx] : alloc_live_entry();
    if (!c) return;
    if (idx < 0) {
        memset(c, 0, sizeof(*c));
        strncpy(c->id, id, sizeof(c->id) - 1);
    }
    live_update_intervals(*c, now);
    snprintf(c->name, sizeof(c->name), "unk_%u_raw", (unsigned)reg_addr);
    c->unit[0] = '\0';
    c->value = (float)raw_u16;
    c->has_numeric = true;
    snprintf(c->value_str, sizeof(c->value_str), "%u", (unsigned)raw_u16);
    c->last_seen_ms = now;
    c->reg_addr = reg_addr;
    c->reg_words = reg_words;
    c->slave_addr = slave_addr;
    c->source_id = source_id;
    c->bucket = infer_bucket_from_reg(reg_addr);
    c->known = false;
    c->tag_new = false;
    c->tag_unk = true;
    c->valid = true;
    c->update_seq = ++s_live_update_seq;
}

String live_value_store_get_json(uint32_t since_seq) {
    JsonDocument doc;
    JsonArray items = doc["items"].to<JsonArray>();
    const uint32_t now_ms = millis();

    if (!s_live_cache) {
        doc["latest_seq"] = s_live_update_seq;
        String out_empty;
        serializeJson(doc, out_empty);
        return out_empty;
    }

    for (int i = 0; i < s_live_cache_count; i++) {
        const LiveCache& c = s_live_cache[i];
        if (!c.valid) continue;
        if (c.update_seq <= since_seq) continue;

        JsonObject o = items.add<JsonObject>();
        o["id"] = c.id;
        o["name"] = c.name;
        o["group"] = live_bucket_key(c.bucket);
        o["known"] = c.known;
        o["src"] = source_tag(c.source_id);
        o["src_icon"] = source_icon(c.source_id);
        o["slave"] = c.slave_addr;
        o["reg"] = c.reg_addr;
        o["reg_words"] = c.reg_words;
        if (c.reg_words > 1) o["reg_end"] = (uint16_t)(c.reg_addr + c.reg_words - 1U);
        if (c.tag_new) o["vtag"] = "NEW";
        if (c.tag_unk) o["vtag"] = "UNK";

        if (c.has_numeric) o["v"] = c.value;
        if (c.value_str[0] != '\0') o["v_str"] = c.value_str;
        if (c.unit[0] != '\0') o["u"] = c.unit;

        if (c.last_seen_ms > 0 && now_ms >= c.last_seen_ms) {
            o["age_s"] = (uint32_t)((now_ms - c.last_seen_ms) / 1000UL);
        }
        if (c.interval_count > 0) {
            o["min_ms"] = c.min_interval_ms;
            o["max_ms"] = c.max_interval_ms;
            o["avg_ms"] = (uint32_t)(c.interval_sum_ms / c.interval_count);
        }
        o["seq"] = c.update_seq;
    }

    doc["latest_seq"] = s_live_update_seq;
    String out;
    serializeJson(doc, out);
    return out;
}
