#pragma once
#include "modbus_rtu.h"
#include "reg_groups.h"
#include <stdint.h>

// ============================================================
// Huawei register decoder — auto-detects groups, filters publish
// ============================================================

// Callback: called for each successfully decoded register value.
// Includes the group it belongs to so the publisher can route correctly.
typedef void (*DecodedValueCallback)(const char* name, float value,
                                     const char* unit, uint8_t slave_addr,
                                     RegGroup group, uint8_t source_id,
                                     uint16_t reg_addr, uint8_t reg_words);

// Decode source identifiers used for source-aware caching/UI.
enum DecodeSource : uint8_t {
    SRC_FC03      = 1,
    SRC_FC04      = 2,
    SRC_H41_SUB33 = 3,
    SRC_H41_OTHER = 4,
};

/** Initialise the decoder. Must be called before feeding frames. */
void huawei_decoder_init(DecodedValueCallback cb);

/** Feed any valid Modbus frame (request or response). */
void huawei_decoder_feed(const ModbusFrame* frame);

/** Expire pending requests older than timeout_ms. Call from loop(). */
void huawei_decoder_expire_pending(uint32_t timeout_ms);

/** Enable or disable publishing for a group.
 *  Disabled groups are completely silent — no data, no availability topic.
 *  Persisted via Settings::Publish::group_enabled in config.json. */
void huawei_decoder_set_group_enabled(RegGroup g, bool enabled);

/** Enable or disable raw-frame capture (serial dump and/or raw stream export). */
void huawei_decoder_set_raw_dump(bool enabled);

/** Set raw capture profile: unknown_h41 | compare_power | research_inverter_phase. */
void huawei_decoder_set_raw_capture_profile(const char* profile);

/** Returns true if at least one register from this group has been decoded. */
bool huawei_decoder_group_seen(RegGroup g);

/** Returns true if the group is currently enabled for publishing. */
bool huawei_decoder_group_enabled(RegGroup g);
