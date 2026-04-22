#pragma once
#include "reg_groups.h"
#include <Arduino.h>
#include <stdint.h>

/**
 * Initialize (or reset) the live state cache.
 * Safe to call on boot and after config reloads.
 */
void live_value_store_init();

/**
 * Upsert a known decoded value into Live Modbus state.
 */
void live_value_store_publish_known(const char* name, float value,
                                    const char* unit, uint8_t slave_addr,
                                    RegGroup group, uint8_t source_id,
                                    uint16_t reg_addr, uint8_t reg_words);

/**
 * Upsert an unknown decoded register word into Live Modbus state.
 */
void live_value_store_publish_unknown_u16(uint16_t reg_addr, uint8_t reg_words,
                                          uint16_t raw_u16, uint8_t slave_addr,
                                          uint8_t source_id);

/**
 * Returns live state JSON for the Live Modbus page.
 * since_seq=0 => full snapshot, otherwise delta entries only.
 */
String live_value_store_get_json(uint32_t since_seq);
