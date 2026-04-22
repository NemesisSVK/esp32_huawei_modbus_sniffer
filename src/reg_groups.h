#pragma once
#include <stdint.h>

// ============================================================
// Register Groups — derived from the Huawei Solar HA integration
// groupings in sensor.py and the library's registers.py sections.
//
// Groups are detected automatically when registers from that group
// first appear in sniffed traffic. Detected groups are auto-enabled
// and shown in the web UI. Groups never seen remain hidden.
// ============================================================

// ============================================================
// Publish priority tiers — assigned per register group.
// Controls how often the group's values are flushed to MQTT.
// Tier intervals are configurable in config.json (publish.tiers).
// ============================================================
typedef enum : uint8_t {
    TIER_HIGH   = 0,   // default 10 s  — real-time control data (meter)
    TIER_MEDIUM = 1,   // default 30 s  — live inverter/battery state
    TIER_LOW    = 2,   // default 60 s  — statistics, config, diagnostics (retained)
    TIER_COUNT  = 3
} PublishTier;

typedef enum : uint8_t {
    // ---- Grid meter (METER_REGISTERS in registers.py) ----
    // → sensor.py: SINGLE_PHASE_METER_ENTITY_DESCRIPTIONS
    //              THREE_PHASE_METER_ENTITY_DESCRIPTIONS
    GRP_METER = 0,          // regs 37100–37138: grid V/I/P, energy, frequency

    // ---- Inverter AC output (main REGISTERS block) ----
    // → sensor.py: INVERTER_SENSOR_DESCRIPTIONS (power/voltage/current subset)
    GRP_INVERTER_AC,        // regs 32064–32095: active/reactive power, V, I, PF, Hz, temp

    // ---- Inverter status & alarms ----
    // → sensor.py: INVERTER_SENSOR_DESCRIPTIONS (state/alarm/status subset)
    GRP_INVERTER_STATUS,    // regs 32000–32174: state bitfields, alarms, startup/shutdown times

    // ---- Inverter energy totals ----
    // → sensor.py: INVERTER_SENSOR_DESCRIPTIONS (energy subset)
    GRP_INVERTER_ENERGY,    // regs 32106–32230: daily/monthly/yearly kWh, MPPT yield

    // ---- Inverter device info (read-once strings + numeric info) ----
    // → sensor.py: EntityCategory.DIAGNOSTIC entries
    GRP_INVERTER_INFO,      // regs 30000–37201: model, serial, firmware, hw version, optimizer count

    // ---- PV strings DC input ----
    // → sensor.py: no direct HA entity, raw from PV_REGISTERS in library
    GRP_PV_STRINGS,         // regs 32016–32063: PV01–PV24 voltage & current

    // ---- Battery combined aggregate ----
    // → sensor.py: BATTERIES_SENSOR_DESCRIPTIONS
    GRP_BATTERY,            // regs 37758–37786: SOC, status, charge/discharge power

    // ---- Battery Unit 1 detail ----
    // → sensor.py: BATTERY_TEMPLATE_SENSOR_DESCRIPTIONS (battery_1_key entries)
    GRP_BATTERY_UNIT1,      // regs 37000–37068: Unit 1 V/I/temp/SOC/power/energy

    // ---- Battery Unit 2 detail ----
    // → sensor.py: BATTERY_TEMPLATE_SENSOR_DESCRIPTIONS (battery_2_key entries)
    GRP_BATTERY_UNIT2,      // regs 37700–37755: Unit 2 V/I/temp/SOC/power/energy

    // ---- Battery pack detail (Unit 1 & 2, packs 1–3) ----
    // → sensor.py: not surfaced individually, but useful for diagnostics
    GRP_BATTERY_PACKS,      // regs 38200–38463: per-pack SOC, V, I, temperature

    // ---- Battery settings (writeable config registers) ----
    // → sensor.py: EntityCategory.CONFIG entries for storage
    GRP_BATTERY_SETTINGS,   // regs 47000–47675: working mode, charge limits, TOU

    // ---- SDongle aggregate ----
    // → sensor.py: not directly, but SDongle exposes totals
    GRP_SDONGLE,            // regs 37498–37516: total PV, load, grid, battery power

    // ---- Sentinel — must be last ----
    GRP_COUNT
} RegGroup;

// Human-readable names, descriptions, and publish tier defaults for the web UI
typedef struct {
    RegGroup     id;
    const char*  key;          // short key for JSON / NVS / config
    const char*  label;        // UI display name
    const char*  description;  // what data it contains
    const char*  mqtt_subtopic; // published under huawei_solar/<subtopic>
    PublishTier  default_tier; // default publish tier (overridable in config.json)
} RegGroupInfo;

static const RegGroupInfo GROUP_INFO[GRP_COUNT] = {
    { GRP_METER,            "meter",           "Grid Meter",
      "Grid voltage, current, active/reactive power, energy import/export, frequency",
      "meter",    TIER_HIGH   },
    { GRP_INVERTER_AC,      "inverter_ac",     "Inverter AC Output",
      "Active power, phase voltages/currents, power factor, frequency, efficiency, temperature",
      "inverter", TIER_MEDIUM },
    { GRP_INVERTER_STATUS,  "inverter_status", "Inverter Status & Alarms",
      "Device status, running state bitfields, alarm codes, fault code, startup/shutdown times",
      "inverter", TIER_LOW    },
    { GRP_INVERTER_ENERGY,  "inverter_energy", "Inverter Energy Totals",
      "Daily / monthly / yearly yield energy, MPPT cumulative DC yield",
      "inverter", TIER_LOW    },
    { GRP_INVERTER_INFO,    "inverter_info",   "Inverter Device Info",
      "Model name, serial number, firmware/hardware versions, optimizer count (read once at startup)",
      "inverter", TIER_LOW    },
    { GRP_PV_STRINGS,       "pv_strings",      "PV Strings (DC input)",
      "Per-string DC voltage and current for each MPPT input (PV01-PV24)",
      "inverter", TIER_LOW    },
    { GRP_BATTERY,          "battery",         "Battery (Aggregate)",
      "Combined SOC, running status, bus voltage/current, charge/discharge power, daily energy",
      "battery",  TIER_MEDIUM },
    { GRP_BATTERY_UNIT1,    "battery_u1",      "Battery Unit 1",
      "Unit 1 state of capacity, charge power, bus V/I, temperature, total charge/discharge",
      "battery",  TIER_MEDIUM },
    { GRP_BATTERY_UNIT2,    "battery_u2",      "Battery Unit 2",
      "Unit 2 state of capacity, charge power, bus V/I, temperature, total charge/discharge",
      "battery",  TIER_MEDIUM },
    { GRP_BATTERY_PACKS,    "battery_packs",   "Battery Pack Details",
      "Per-pack (Unit 1 & 2, packs 1-3) SOC, voltage, current, temperature, working status",
      "battery",  TIER_LOW    },
    { GRP_BATTERY_SETTINGS, "battery_cfg",     "Battery Settings",
      "Working mode, charge/discharge limits, grid charge cutoff, feed-in power limits",
      "battery",  TIER_LOW    },
    { GRP_SDONGLE,          "sdongle",         "SDongle Aggregates",
      "Total PV input power, load power, grid power, battery power across all inverters",
      "sdongle",  TIER_MEDIUM },
};
