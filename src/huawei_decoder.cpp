#include "huawei_decoder.h"
#include "LiveValueStore.h"
#include "web_ui.h"
#include "reg_groups.h"
#include "config.h"
#include "UnifiedLogger.h"
#include "RawFrameStreamer.h"
#include <Arduino.h>
#include <string.h>

// ============================================================
// Register descriptor with group tag
// ============================================================
typedef enum : uint8_t { U16, I16, U32, I32, I32ABS, STRING } ValType;
// STRING: register is ASCII text — used only for group detection; value decode is skipped.

typedef struct {
    uint16_t    addr;
    uint8_t     words;
    ValType     type;
    uint16_t    scale;
    const char* unit;
    const char* name;
    RegGroup    group;     // ← which logical group this register belongs to
} RegDesc;

// ============================================================
// Full register table — group IDs from reg_groups.h
// ============================================================
static const RegDesc KNOWN_REGS[] = {
    // ---- GRP_METER (37100–37138) ----
    { 37100, 1, U16,     1, "",     "meter_status",           GRP_METER },
    { 37101, 2, I32,    10, "V",    "grid_a_voltage",         GRP_METER },
    { 37103, 2, I32,    10, "V",    "grid_b_voltage",         GRP_METER },
    { 37105, 2, I32,    10, "V",    "grid_c_voltage",         GRP_METER },
    { 37107, 2, I32,   100, "A",    "grid_a_current",         GRP_METER },
    { 37109, 2, I32,   100, "A",    "grid_b_current",         GRP_METER },
    { 37111, 2, I32,   100, "A",    "grid_c_current",         GRP_METER },
    { 37113, 2, I32,     1, "W",    "meter_active_power",     GRP_METER },
    { 37115, 2, I32,     1, "var",  "meter_reactive_power",   GRP_METER },
    { 37117, 1, I16,  1000, "",     "meter_power_factor",     GRP_METER },
    { 37118, 1, I16,   100, "Hz",   "meter_frequency",        GRP_METER },
    { 37119, 2, I32ABS,100, "kWh",  "grid_exported_energy",   GRP_METER },
    { 37121, 2, I32,   100, "kWh",  "grid_imported_energy",   GRP_METER },
    { 37123, 2, I32,   100, "kvarh","grid_reactive_energy",   GRP_METER },
    { 37125, 1, U16,     1, "",     "meter_type",             GRP_METER },
    { 37126, 2, I32,    10, "V",    "grid_ab_voltage",        GRP_METER },
    { 37128, 2, I32,    10, "V",    "grid_bc_voltage",        GRP_METER },
    { 37130, 2, I32,    10, "V",    "grid_ca_voltage",        GRP_METER },
    { 37132, 2, I32,     1, "W",    "grid_a_power",           GRP_METER },
    { 37134, 2, I32,     1, "W",    "grid_b_power",           GRP_METER },
    { 37136, 2, I32,     1, "W",    "grid_c_power",           GRP_METER },
    { 37138, 1, U16,     1, "",     "meter_type_check",       GRP_METER },

    // ---- GRP_METER (FC0x41 fast-path confirmed power mirrors) ----
    { 16300, 2, I32,     1, "W",    "meter_active_power_fast", GRP_METER },
    { 16305, 1, I16,     1, "W",    "grid_a_power_fast",       GRP_METER },
    { 16307, 1, I16,     1, "W",    "grid_b_power_fast",       GRP_METER },
    { 16309, 1, I16,     1, "W",    "grid_c_power_fast",       GRP_METER },
    { 16312, 1, I16,     1, "var",  "meter_reactive_power_fast", GRP_METER },
    // ---- GRP_INVERTER_AC (32064–32095 + related AC telemetry) ----
    { 32064, 2, I32,     1, "W",    "input_power",            GRP_INVERTER_AC },
    { 32066, 1, U16,    10, "V",    "line_voltage_ab",        GRP_INVERTER_AC },
    { 32067, 1, U16,    10, "V",    "line_voltage_bc",        GRP_INVERTER_AC },
    { 32068, 1, U16,    10, "V",    "line_voltage_ca",        GRP_INVERTER_AC },
    { 32069, 1, U16,    10, "V",    "phase_a_voltage",        GRP_INVERTER_AC },
    { 32070, 1, U16,    10, "V",    "phase_b_voltage",        GRP_INVERTER_AC },
    { 32071, 1, U16,    10, "V",    "phase_c_voltage",        GRP_INVERTER_AC },
    { 32072, 2, I32,  1000, "A",    "phase_a_current",        GRP_INVERTER_AC },
    { 32074, 2, I32,  1000, "A",    "phase_b_current",        GRP_INVERTER_AC },
    { 32076, 2, I32,  1000, "A",    "phase_c_current",        GRP_INVERTER_AC },
    { 32078, 2, I32,     1, "W",    "peak_active_power",      GRP_INVERTER_AC },
    { 32080, 2, I32,     1, "W",    "active_power",           GRP_INVERTER_AC },
    { 32082, 2, I32,     1, "var",  "reactive_power",         GRP_INVERTER_AC },
    { 32084, 1, I16,  1000, "",     "power_factor",           GRP_INVERTER_AC },
    { 32085, 1, U16,   100, "Hz",   "grid_frequency",         GRP_INVERTER_AC },
    { 32086, 1, U16,   100, "%",    "efficiency",             GRP_INVERTER_AC },
    { 32087, 1, I16,    10, "degC", "internal_temperature",   GRP_INVERTER_AC },
    { 32088, 1, U16,  1000, "MOhm", "insulation_resistance",  GRP_INVERTER_AC },
    { 32095, 2, I32,     1, "W",    "active_power_fast",      GRP_INVERTER_AC },
    { 42056, 2, U32,     1, "W",    "mppt_predicted_power",   GRP_INVERTER_AC },

    // ---- GRP_INVERTER_STATUS (32000–32174) ----
    { 32000, 1, U16,     1, "",     "state_1",                GRP_INVERTER_STATUS },
    { 32002, 1, U16,     1, "",     "state_2",                GRP_INVERTER_STATUS },
    { 32003, 2, U32,     1, "",     "state_3",                GRP_INVERTER_STATUS },
    { 32008, 1, U16,     1, "",     "alarm_1",                GRP_INVERTER_STATUS },
    { 32009, 1, U16,     1, "",     "alarm_2",                GRP_INVERTER_STATUS },
    { 32010, 1, U16,     1, "",     "alarm_3",                GRP_INVERTER_STATUS },
    { 32089, 1, U16,     1, "",     "device_status",          GRP_INVERTER_STATUS },
    { 32090, 1, U16,     1, "",     "fault_code",             GRP_INVERTER_STATUS },
    { 32091, 2, U32,     1, "s",    "startup_time",           GRP_INVERTER_STATUS },
    { 32093, 2, U32,     1, "s",    "shutdown_time",          GRP_INVERTER_STATUS },
    { 32172, 2, U32,     1, "",     "latest_active_alarm_sn", GRP_INVERTER_STATUS },
    { 32174, 2, U32,     1, "",     "latest_hist_alarm_sn",   GRP_INVERTER_STATUS },
    { 42045, 1, U16,     1, "",     "off_grid_mode",          GRP_INVERTER_STATUS },

    // ---- GRP_INVERTER_ENERGY (32106–32230) ----
    { 32106, 2, U32,   100, "kWh",  "accumulated_yield",      GRP_INVERTER_ENERGY },
    { 32108, 2, U32,   100, "kWh",  "total_dc_input_power",   GRP_INVERTER_ENERGY },
    { 32112, 2, U32,   100, "kWh",  "hourly_yield",           GRP_INVERTER_ENERGY },
    { 32114, 2, U32,   100, "kWh",  "daily_yield",            GRP_INVERTER_ENERGY },
    { 32116, 2, U32,   100, "kWh",  "monthly_yield",          GRP_INVERTER_ENERGY },
    { 32118, 2, U32,   100, "kWh",  "yearly_yield",           GRP_INVERTER_ENERGY },
    { 32212, 2, U32,   100, "kWh",  "mppt1_dc_yield",         GRP_INVERTER_ENERGY },
    { 32214, 2, U32,   100, "kWh",  "mppt2_dc_yield",         GRP_INVERTER_ENERGY },
    { 32216, 2, U32,   100, "kWh",  "mppt3_dc_yield",         GRP_INVERTER_ENERGY },
    { 32218, 2, U32,   100, "kWh",  "mppt4_dc_yield",         GRP_INVERTER_ENERGY },
    { 32220, 2, U32,   100, "kWh",  "mppt5_dc_yield",         GRP_INVERTER_ENERGY },
    { 32222, 2, U32,   100, "kWh",  "mppt6_dc_yield",         GRP_INVERTER_ENERGY },
    { 32224, 2, U32,   100, "kWh",  "mppt7_dc_yield",         GRP_INVERTER_ENERGY },
    { 32226, 2, U32,   100, "kWh",  "mppt8_dc_yield",         GRP_INVERTER_ENERGY },
    { 32228, 2, U32,   100, "kWh",  "mppt9_dc_yield",         GRP_INVERTER_ENERGY },
    { 32230, 2, U32,   100, "kWh",  "mppt10_dc_yield",        GRP_INVERTER_ENERGY },

    // ---- GRP_INVERTER_INFO (30000–37201) ----
    // STRING entries: decoded for group detection only — value publish is skipped.
    // Numeric entries are fully decoded and published.
    { 30000, 15, STRING,  1, "",     "model_name",             GRP_INVERTER_INFO },
    { 30015, 10, STRING,  1, "",     "serial_number",          GRP_INVERTER_INFO },
    { 30025, 10, STRING,  1, "",     "pn",                     GRP_INVERTER_INFO },
    { 30035, 15, STRING,  1, "",     "firmware_version",       GRP_INVERTER_INFO },
    { 30050, 15, STRING,  1, "",     "software_version",       GRP_INVERTER_INFO },
    { 30068,  2, U32,     1, "",     "protocol_version",       GRP_INVERTER_INFO },
    { 30070,  1, U16,     1, "",     "model_id",               GRP_INVERTER_INFO },
    { 30071,  1, U16,     1, "",     "nb_pv_strings",          GRP_INVERTER_INFO },
    { 30072,  1, U16,     1, "",     "nb_mpp_tracks",          GRP_INVERTER_INFO },
    { 30073,  2, U32,     1, "W",    "rated_power",            GRP_INVERTER_INFO },
    { 30075,  2, U32,     1, "W",    "p_max",                  GRP_INVERTER_INFO },
    { 30110,  1, U16,     1, "",     "sw_unique_id",           GRP_INVERTER_INFO },
    // Reverse-engineering watchlist (unmapped/proprietary telemetry, raw words)
    { 16000,  1, U16,     1, "",     "h41_16000_raw",          GRP_INVERTER_INFO },
    { 16001,  1, U16,     1, "",     "h41_16001_raw",          GRP_INVERTER_INFO },
    { 30207,  2, U32,     1, "",     "subdevice_support_flag", GRP_INVERTER_INFO },
    { 30209,  1, U16,     1, "",     "h41_30209_raw",          GRP_INVERTER_INFO },
    { 30210,  1, U16,     1, "",     "h41_30210_raw",          GRP_INVERTER_INFO },
    { 30283,  1, U16,     1, "",     "h41_30283_raw",          GRP_INVERTER_INFO },
    { 30284,  1, U16,     1, "",     "h41_30284_raw",          GRP_INVERTER_INFO },
    { 30285,  1, U16,     1, "",     "h41_30285_raw",          GRP_INVERTER_INFO },
    { 30286,  1, U16,     1, "",     "h41_30286_raw",          GRP_INVERTER_INFO },
    { 35304,  1, U16,     1, "",     "h41_35304_raw",          GRP_INVERTER_INFO },
    { 35306,  1, U16,     1, "",     "h41_35306_raw",          GRP_INVERTER_INFO },
    { 35307,  1, U16,     1, "",     "h41_35307_raw",          GRP_INVERTER_INFO },
    { 40000,  1, U16,     1, "",     "h41_40000_raw",          GRP_INVERTER_INFO },
    { 40001,  1, U16,     1, "",     "h41_40001_raw",          GRP_INVERTER_INFO },
    { 42017,  1, U16,     1, "",     "h41_42017_raw",          GRP_INVERTER_INFO },
    { 42018,  1, U16,     1, "",     "h41_42018_raw",          GRP_INVERTER_INFO },
    { 31000, 15, STRING,  1, "",     "hardware_version",       GRP_INVERTER_INFO },
    { 31015, 10, STRING,  1, "",     "monitoring_board_sn",    GRP_INVERTER_INFO },
    { 31025, 15, STRING,  1, "",     "monitoring_sw_version",  GRP_INVERTER_INFO },
    { 31040, 15, STRING,  1, "",     "master_dsp_version",     GRP_INVERTER_INFO },
    { 31055, 15, STRING,  1, "",     "slave_dsp_version",      GRP_INVERTER_INFO },
    { 37200,  1, U16,     1, "",     "nb_optimizers",          GRP_INVERTER_INFO },
    { 37201,  1, U16,     1, "",     "nb_online_optimizers",   GRP_INVERTER_INFO },

    // ---- GRP_PV_STRINGS (32016–32063) ----
    { 32016, 1, I16,    10, "V",    "pv01_voltage",           GRP_PV_STRINGS },
    { 32017, 1, I16,   100, "A",    "pv01_current",           GRP_PV_STRINGS },
    { 32018, 1, I16,    10, "V",    "pv02_voltage",           GRP_PV_STRINGS },
    { 32019, 1, I16,   100, "A",    "pv02_current",           GRP_PV_STRINGS },
    { 32020, 1, I16,    10, "V",    "pv03_voltage",           GRP_PV_STRINGS },
    { 32021, 1, I16,   100, "A",    "pv03_current",           GRP_PV_STRINGS },
    { 32022, 1, I16,    10, "V",    "pv04_voltage",           GRP_PV_STRINGS },
    { 32023, 1, I16,   100, "A",    "pv04_current",           GRP_PV_STRINGS },
    { 32024, 1, I16,    10, "V",    "pv05_voltage",           GRP_PV_STRINGS },
    { 32025, 1, I16,   100, "A",    "pv05_current",           GRP_PV_STRINGS },
    { 32026, 1, I16,    10, "V",    "pv06_voltage",           GRP_PV_STRINGS },
    { 32027, 1, I16,   100, "A",    "pv06_current",           GRP_PV_STRINGS },
    { 32028, 1, I16,    10, "V",    "pv07_voltage",           GRP_PV_STRINGS },
    { 32029, 1, I16,   100, "A",    "pv07_current",           GRP_PV_STRINGS },
    { 32030, 1, I16,    10, "V",    "pv08_voltage",           GRP_PV_STRINGS },
    { 32031, 1, I16,   100, "A",    "pv08_current",           GRP_PV_STRINGS },
    { 32032, 1, I16,    10, "V",    "pv09_voltage",           GRP_PV_STRINGS },
    { 32033, 1, I16,   100, "A",    "pv09_current",           GRP_PV_STRINGS },
    { 32034, 1, I16,    10, "V",    "pv10_voltage",           GRP_PV_STRINGS },
    { 32035, 1, I16,   100, "A",    "pv10_current",           GRP_PV_STRINGS },
    { 32036, 1, I16,    10, "V",    "pv11_voltage",           GRP_PV_STRINGS },
    { 32037, 1, I16,   100, "A",    "pv11_current",           GRP_PV_STRINGS },
    { 32038, 1, I16,    10, "V",    "pv12_voltage",           GRP_PV_STRINGS },
    { 32039, 1, I16,   100, "A",    "pv12_current",           GRP_PV_STRINGS },
    { 32040, 1, I16,    10, "V",    "pv13_voltage",           GRP_PV_STRINGS },
    { 32041, 1, I16,   100, "A",    "pv13_current",           GRP_PV_STRINGS },
    { 32042, 1, I16,    10, "V",    "pv14_voltage",           GRP_PV_STRINGS },
    { 32043, 1, I16,   100, "A",    "pv14_current",           GRP_PV_STRINGS },
    { 32044, 1, I16,    10, "V",    "pv15_voltage",           GRP_PV_STRINGS },
    { 32045, 1, I16,   100, "A",    "pv15_current",           GRP_PV_STRINGS },
    { 32046, 1, I16,    10, "V",    "pv16_voltage",           GRP_PV_STRINGS },
    { 32047, 1, I16,   100, "A",    "pv16_current",           GRP_PV_STRINGS },
    { 32048, 1, I16,    10, "V",    "pv17_voltage",           GRP_PV_STRINGS },
    { 32049, 1, I16,   100, "A",    "pv17_current",           GRP_PV_STRINGS },
    { 32050, 1, I16,    10, "V",    "pv18_voltage",           GRP_PV_STRINGS },
    { 32051, 1, I16,   100, "A",    "pv18_current",           GRP_PV_STRINGS },
    { 32052, 1, I16,    10, "V",    "pv19_voltage",           GRP_PV_STRINGS },
    { 32053, 1, I16,   100, "A",    "pv19_current",           GRP_PV_STRINGS },
    { 32054, 1, I16,    10, "V",    "pv20_voltage",           GRP_PV_STRINGS },
    { 32055, 1, I16,   100, "A",    "pv20_current",           GRP_PV_STRINGS },
    { 32056, 1, I16,    10, "V",    "pv21_voltage",           GRP_PV_STRINGS },
    { 32057, 1, I16,   100, "A",    "pv21_current",           GRP_PV_STRINGS },
    { 32058, 1, I16,    10, "V",    "pv22_voltage",           GRP_PV_STRINGS },
    { 32059, 1, I16,   100, "A",    "pv22_current",           GRP_PV_STRINGS },
    { 32060, 1, I16,    10, "V",    "pv23_voltage",           GRP_PV_STRINGS },
    { 32061, 1, I16,   100, "A",    "pv23_current",           GRP_PV_STRINGS },
    { 32062, 1, I16,    10, "V",    "pv24_voltage",           GRP_PV_STRINGS },
    { 32063, 1, I16,   100, "A",    "pv24_current",           GRP_PV_STRINGS },

    // ---- GRP_BATTERY aggregate (37758–37786) ----
    { 37758, 2, U32,     1, "Wh",   "battery_rated_capacity", GRP_BATTERY },
    { 37760, 1, U16,    10, "%",    "battery_soc",            GRP_BATTERY },
    { 37762, 1, U16,     1, "",     "battery_status",         GRP_BATTERY },
    { 37763, 1, U16,    10, "V",    "battery_bus_voltage",    GRP_BATTERY },
    { 37764, 1, I16,    10, "A",    "battery_bus_current",    GRP_BATTERY },
    { 37765, 2, I32,     1, "W",    "battery_power",          GRP_BATTERY },
    { 37780, 2, U32,   100, "kWh",  "battery_total_charge",   GRP_BATTERY },
    { 37782, 2, U32,   100, "kWh",  "battery_total_discharge",GRP_BATTERY },
    { 37784, 2, U32,   100, "kWh",  "battery_day_charge",     GRP_BATTERY },
    { 37786, 2, U32,   100, "kWh",  "battery_day_discharge",  GRP_BATTERY },
    { 37829, 1, U16,     1, "",     "h41_37829_raw",          GRP_BATTERY },
    { 37830, 1, U16,     1, "",     "h41_37830_raw",          GRP_BATTERY },
    { 37831, 1, U16,     1, "",     "h41_37831_raw",          GRP_BATTERY },
    { 37918, 1, U16,     1, "",     "h41_37918_raw",          GRP_BATTERY },
    { 37919, 1, U16,     1, "",     "h41_37919_raw",          GRP_BATTERY },
    { 37928, 1, U16,     1, "",     "h41_37928_raw",          GRP_BATTERY },
    { 37926, 1, U16,     1, "",     "battery_soh_calib_status", GRP_BATTERY },
    { 37927, 1, U16,     1, "",     "battery_soh_calib_soc_low", GRP_BATTERY },
    { 37518, 1, U16,     1, "",     "h41_37518_raw",          GRP_BATTERY },
    { 37519, 1, U16,     1, "",     "h41_37519_raw",          GRP_BATTERY },

    // ---- GRP_BATTERY_UNIT1 (37000–37068) ----
    { 37000, 1, U16,     1, "",     "u1_status",              GRP_BATTERY_UNIT1 },
    { 37001, 2, I32,     1, "W",    "u1_charge_power",        GRP_BATTERY_UNIT1 },
    { 37003, 1, U16,    10, "V",    "u1_bus_voltage",         GRP_BATTERY_UNIT1 },
    { 37004, 1, U16,    10, "%",    "u1_soc",                 GRP_BATTERY_UNIT1 },
    { 37006, 1, U16,     1, "",     "u1_working_mode_b",      GRP_BATTERY_UNIT1 },
    { 37007, 2, U32,     1, "W",    "u1_rated_charge_pwr",    GRP_BATTERY_UNIT1 },
    { 37009, 2, U32,     1, "W",    "u1_rated_discharge_pwr", GRP_BATTERY_UNIT1 },
    { 37014, 1, U16,     1, "",     "u1_fault_id",            GRP_BATTERY_UNIT1 },
    { 37015, 2, U32,   100, "kWh",  "u1_day_charge",          GRP_BATTERY_UNIT1 },
    { 37017, 2, U32,   100, "kWh",  "u1_day_discharge",       GRP_BATTERY_UNIT1 },
    { 37021, 1, I16,    10, "A",    "u1_bus_current",         GRP_BATTERY_UNIT1 },
    { 37022, 1, I16,    10, "degC", "u1_temperature",         GRP_BATTERY_UNIT1 },
    { 37025, 1, U16,     1, "min",  "u1_remaining_time",      GRP_BATTERY_UNIT1 },
    { 37066, 2, U32,   100, "kWh",  "u1_total_charge",        GRP_BATTERY_UNIT1 },
    { 37068, 2, U32,   100, "kWh",  "u1_total_discharge",     GRP_BATTERY_UNIT1 },

    // ---- GRP_BATTERY_UNIT2 (37700–37755) ----
    { 37738, 1, U16,    10, "%",    "u2_soc",                 GRP_BATTERY_UNIT2 },
    { 37741, 1, U16,     1, "",     "u2_status",              GRP_BATTERY_UNIT2 },
    { 37743, 2, I32,     1, "W",    "u2_charge_power",        GRP_BATTERY_UNIT2 },
    { 37746, 2, U32,   100, "kWh",  "u2_day_charge",          GRP_BATTERY_UNIT2 },
    { 37748, 2, U32,   100, "kWh",  "u2_day_discharge",       GRP_BATTERY_UNIT2 },
    { 37750, 1, U16,    10, "V",    "u2_bus_voltage",         GRP_BATTERY_UNIT2 },
    { 37751, 1, I16,    10, "A",    "u2_bus_current",         GRP_BATTERY_UNIT2 },
    { 37752, 1, I16,    10, "degC", "u2_temperature",         GRP_BATTERY_UNIT2 },
    { 37753, 2, U32,   100, "kWh",  "u2_total_charge",        GRP_BATTERY_UNIT2 },
    { 37755, 2, U32,   100, "kWh",  "u2_total_discharge",     GRP_BATTERY_UNIT2 },

    // ---- GRP_BATTERY_PACKS (38200–38463) — Unit 1 & 2, Packs 1–3 ----
    // Unit 1 Pack 1
    { 38228, 1, U16,     1, "",     "u1p1_working_status",    GRP_BATTERY_PACKS },
    { 38229, 1, U16,    10, "%",    "u1p1_soc",               GRP_BATTERY_PACKS },
    { 38233, 2, I32,     1, "W",    "u1p1_power",             GRP_BATTERY_PACKS },
    { 38235, 1, U16,    10, "V",    "u1p1_voltage",           GRP_BATTERY_PACKS },
    { 38236, 1, I16,    10, "A",    "u1p1_current",           GRP_BATTERY_PACKS },
    { 38238, 2, U32,   100, "kWh",  "u1p1_total_charge",      GRP_BATTERY_PACKS },
    { 38240, 2, U32,   100, "kWh",  "u1p1_total_discharge",   GRP_BATTERY_PACKS },
    // Unit 1 Pack 2
    { 38270, 1, U16,     1, "",     "u1p2_working_status",    GRP_BATTERY_PACKS },
    { 38271, 1, U16,    10, "%",    "u1p2_soc",               GRP_BATTERY_PACKS },
    { 38275, 2, I32,     1, "W",    "u1p2_power",             GRP_BATTERY_PACKS },
    { 38277, 1, U16,    10, "V",    "u1p2_voltage",           GRP_BATTERY_PACKS },
    { 38278, 1, I16,    10, "A",    "u1p2_current",           GRP_BATTERY_PACKS },
    // Unit 1 Pack 3
    { 38312, 1, U16,     1, "",     "u1p3_working_status",    GRP_BATTERY_PACKS },
    { 38313, 1, U16,    10, "%",    "u1p3_soc",               GRP_BATTERY_PACKS },
    { 38317, 2, I32,     1, "W",    "u1p3_power",             GRP_BATTERY_PACKS },
    { 38319, 1, U16,    10, "V",    "u1p3_voltage",           GRP_BATTERY_PACKS },
    { 38320, 1, I16,    10, "A",    "u1p3_current",           GRP_BATTERY_PACKS },
    // Unit 2 Pack 1
    { 38354, 1, U16,     1, "",     "u2p1_working_status",    GRP_BATTERY_PACKS },
    { 38355, 1, U16,    10, "%",    "u2p1_soc",               GRP_BATTERY_PACKS },
    { 38359, 2, I32,     1, "W",    "u2p1_power",             GRP_BATTERY_PACKS },
    { 38361, 1, U16,    10, "V",    "u2p1_voltage",           GRP_BATTERY_PACKS },
    { 38362, 1, I16,    10, "A",    "u2p1_current",           GRP_BATTERY_PACKS },
    { 38364, 2, U32,   100, "kWh",  "u2p1_total_charge",      GRP_BATTERY_PACKS },
    { 38366, 2, U32,   100, "kWh",  "u2p1_total_discharge",   GRP_BATTERY_PACKS },
    // Unit 2 Pack 2
    { 38396, 1, U16,     1, "",     "u2p2_working_status",    GRP_BATTERY_PACKS },
    { 38397, 1, U16,    10, "%",    "u2p2_soc",               GRP_BATTERY_PACKS },
    { 38401, 2, I32,     1, "W",    "u2p2_power",             GRP_BATTERY_PACKS },
    { 38403, 1, U16,    10, "V",    "u2p2_voltage",           GRP_BATTERY_PACKS },
    { 38404, 1, I16,    10, "A",    "u2p2_current",           GRP_BATTERY_PACKS },
    // Unit 2 Pack 3
    { 38438, 1, U16,     1, "",     "u2p3_working_status",    GRP_BATTERY_PACKS },
    { 38439, 1, U16,    10, "%",    "u2p3_soc",               GRP_BATTERY_PACKS },
    { 38443, 2, I32,     1, "W",    "u2p3_power",             GRP_BATTERY_PACKS },
    { 38445, 1, U16,    10, "V",    "u2p3_voltage",           GRP_BATTERY_PACKS },
    { 38446, 1, I16,    10, "A",    "u2p3_current",           GRP_BATTERY_PACKS },
    { 38448, 2, U32,   100, "kWh",  "u2p3_total_charge",      GRP_BATTERY_PACKS },
    { 38450, 2, U32,   100, "kWh",  "u2p3_total_discharge",   GRP_BATTERY_PACKS },
    // Pack temperatures (all units, all packs)
    { 38452, 1, I16,    10, "degC", "u1p1_max_temp",          GRP_BATTERY_PACKS },
    { 38453, 1, I16,    10, "degC", "u1p1_min_temp",          GRP_BATTERY_PACKS },
    { 38454, 1, I16,    10, "degC", "u1p2_max_temp",          GRP_BATTERY_PACKS },
    { 38455, 1, I16,    10, "degC", "u1p2_min_temp",          GRP_BATTERY_PACKS },
    { 38456, 1, I16,    10, "degC", "u1p3_max_temp",          GRP_BATTERY_PACKS },
    { 38457, 1, I16,    10, "degC", "u1p3_min_temp",          GRP_BATTERY_PACKS },
    { 38458, 1, I16,    10, "degC", "u2p1_max_temp",          GRP_BATTERY_PACKS },
    { 38459, 1, I16,    10, "degC", "u2p1_min_temp",          GRP_BATTERY_PACKS },
    { 38460, 1, I16,    10, "degC", "u2p2_max_temp",          GRP_BATTERY_PACKS },
    { 38461, 1, I16,    10, "degC", "u2p2_min_temp",          GRP_BATTERY_PACKS },
    { 38462, 1, I16,    10, "degC", "u2p3_max_temp",          GRP_BATTERY_PACKS },
    { 38463, 1, I16,    10, "degC", "u2p3_min_temp",          GRP_BATTERY_PACKS },

    // ---- GRP_BATTERY_SETTINGS (47000–47675) ----
    { 47000, 1, U16,     1, "",     "u1_product_model",       GRP_BATTERY_SETTINGS },
    { 47004, 1, I16,     1, "",     "working_mode_a",         GRP_BATTERY_SETTINGS },
    { 47027, 1, U16,     1, "",     "tou_price_enabled",      GRP_BATTERY_SETTINGS },
    { 47075, 2, U32,     1, "W",    "max_charge_power",       GRP_BATTERY_SETTINGS },
    { 47077, 2, U32,     1, "W",    "max_discharge_power",    GRP_BATTERY_SETTINGS },
    { 47079, 2, I32,     1, "W",    "grid_tied_pwr_limit",    GRP_BATTERY_SETTINGS },
    { 47081, 1, U16,    10, "%",    "charge_cutoff_soc",      GRP_BATTERY_SETTINGS },
    { 47082, 1, U16,    10, "%",    "discharge_cutoff_soc",   GRP_BATTERY_SETTINGS },
    { 47086, 1, U16,     1, "",     "working_mode",           GRP_BATTERY_SETTINGS },
    { 47087, 1, U16,     1, "",     "charge_from_grid",       GRP_BATTERY_SETTINGS },
    { 47088, 1, U16,    10, "%",    "grid_charge_cutoff_soc", GRP_BATTERY_SETTINGS },
    { 47089, 1, U16,     1, "",     "u2_product_model",       GRP_BATTERY_SETTINGS },
    { 47100, 1, U16,     1, "",     "forcible_mode",          GRP_BATTERY_SETTINGS },
    { 47101, 1, U16,    10, "%",    "forcible_soc",           GRP_BATTERY_SETTINGS },
    { 47102, 1, U16,    10, "%",    "backup_power_soc",       GRP_BATTERY_SETTINGS },
    { 47242, 2, U32,     1, "W",    "charge_from_grid_power", GRP_BATTERY_SETTINGS },
    { 47415, 1, U16,     1, "",     "active_pwr_ctrl_mode",   GRP_BATTERY_SETTINGS },
    { 47416, 2, I32,     1, "W",    "max_feed_grid_power_w",  GRP_BATTERY_SETTINGS },
    { 47418, 1, I16,    10, "%",    "max_feed_grid_pct",      GRP_BATTERY_SETTINGS },
    { 47675, 2, I32,     1, "W",    "default_max_feed_in_pwr",GRP_BATTERY_SETTINGS },

    // ---- GRP_SDONGLE (37498–37516) ----
    { 37498, 2, U32,  1000, "kW",   "sdongle_pv_power",       GRP_SDONGLE },
    { 37500, 2, U32,  1000, "kW",   "sdongle_load_power",     GRP_SDONGLE },
    { 37502, 2, I32,  1000, "kW",   "sdongle_grid_power",     GRP_SDONGLE },
    { 37504, 2, I32,  1000, "kW",   "sdongle_battery_power",  GRP_SDONGLE },
    { 37516, 2, I32,  1000, "kW",   "sdongle_total_power",    GRP_SDONGLE },
};

#define KNOWN_REG_COUNT (sizeof(KNOWN_REGS) / sizeof(KNOWN_REGS[0]))

// ============================================================
// Group state: detection (s_seen) is runtime-only in web_ui.
// Enabled/tier config reads from Settings::Publish (config.json).
// huawei_decoder delegates to group_is_enabled / group_mark_seen.
// ============================================================
static DecodedValueCallback s_cb = nullptr;
static bool                 s_raw_dump = false;

static const uint8_t FC_HUAWEI_EXT = 0x41;

enum RawCaptureProfile : uint8_t {
    RAW_CAPTURE_UNKNOWN_H41 = 0,
    RAW_CAPTURE_COMPARE_POWER,
    RAW_CAPTURE_RESEARCH_INVERTER_PHASE,
    RAW_CAPTURE_ALL_FRAMES,
};

static RawCaptureProfile s_raw_capture_profile = RAW_CAPTURE_UNKNOWN_H41;

#define MAX_CAPTURE_PENDING_REQUESTS 8

typedef struct {
    uint8_t  slave_addr;
    uint8_t  function_code;
    uint16_t start_addr;
    uint16_t reg_count;
    uint32_t timestamp_ms;
    bool     valid;
} CapturePendingReq;

static CapturePendingReq s_capture_pending[MAX_CAPTURE_PENDING_REQUESTS];
static uint32_t s_raw_frame_seq = 0;

static RawCaptureProfile parse_capture_profile(const char* profile) {
    if (!profile) return RAW_CAPTURE_UNKNOWN_H41;
    if (strcmp(profile, "compare_power") == 0) return RAW_CAPTURE_COMPARE_POWER;
    if (strcmp(profile, "research_inverter_phase") == 0) return RAW_CAPTURE_RESEARCH_INVERTER_PHASE;
    if (strcmp(profile, "all_frames") == 0) return RAW_CAPTURE_ALL_FRAMES;
    return RAW_CAPTURE_UNKNOWN_H41;
}

void huawei_decoder_init(DecodedValueCallback cb) {
    s_cb = cb;
    // group enabled state is loaded from config.json by ConfigManager
}

void huawei_decoder_set_raw_dump(bool en) { s_raw_dump = en; }

void huawei_decoder_set_raw_capture_profile(const char* profile) {
    s_raw_capture_profile = parse_capture_profile(profile);
    memset(s_capture_pending, 0, sizeof(s_capture_pending));
}

// These remain in the public API for compatibility but delegate to web_ui
void huawei_decoder_set_group_enabled(RegGroup g, bool en) {
    group_set_enabled(g, en);
}

bool huawei_decoder_group_seen(RegGroup g) {
    return group_is_seen(g);
}

bool huawei_decoder_group_enabled(RegGroup g) {
    return group_is_enabled(g);
}

// ============================================================
// Raw frame capture + dump (for debug.raw_frame_dump setting)
// ============================================================

typedef struct {
    uint16_t start_addr;
    uint16_t end_addr; // inclusive
} CaptureRange;

static bool overlap_range(uint16_t start, uint16_t count,
                          uint16_t r_start, uint16_t r_end) {
    if (count == 0) return false;
    const uint32_t req_start = start;
    const uint32_t req_end = req_start + (uint32_t)count - 1U;
    return !(req_end < r_start || req_start > r_end);
}

static bool request_in_ranges(uint16_t start, uint16_t count,
                              const CaptureRange* ranges, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (overlap_range(start, count, ranges[i].start_addr, ranges[i].end_addr)) {
            return true;
        }
    }
    return false;
}

static bool request_matches_profile(const ModbusFrame* f) {
    if (!f || f->type != FRAME_REQUEST) return false;
    if (f->function_code != FC_READ_HOLDING_REGS && f->function_code != FC_READ_INPUT_REGS) return false;

    static const CaptureRange ranges_compare_power[] = {
        { 32064, 32095 }, // inverter AC / phase values
        { 37100, 37138 }, // meter + per-phase meter power
    };
    static const CaptureRange ranges_research_inverter_phase[] = {
        { 32016, 32095 }, // inverter production path: PV strings + inverter AC (W/V/A)
        { 42056, 42057 }, // mppt_predicted_power
    };

    if (s_raw_capture_profile == RAW_CAPTURE_COMPARE_POWER) {
        return request_in_ranges(f->req_start_addr, f->req_reg_count,
                                 ranges_compare_power,
                                 sizeof(ranges_compare_power) / sizeof(ranges_compare_power[0]));
    }
    if (s_raw_capture_profile == RAW_CAPTURE_RESEARCH_INVERTER_PHASE) {
        return request_in_ranges(f->req_start_addr, f->req_reg_count,
                                 ranges_research_inverter_phase,
                                 sizeof(ranges_research_inverter_phase) / sizeof(ranges_research_inverter_phase[0]));
    }
    return false;
}

static void capture_store_request(const ModbusFrame* f) {
    int oldest = 0;
    uint32_t oldest_ts = UINT32_MAX;
    for (int i = 0; i < MAX_CAPTURE_PENDING_REQUESTS; i++) {
        if (!s_capture_pending[i].valid) { oldest = i; break; }
        if (s_capture_pending[i].timestamp_ms < oldest_ts) {
            oldest_ts = s_capture_pending[i].timestamp_ms;
            oldest = i;
        }
    }
    s_capture_pending[oldest] = {
        f->slave_addr,
        f->function_code,
        f->req_start_addr,
        f->req_reg_count,
        (uint32_t)millis(),
        true
    };
}

static bool capture_match_response(const ModbusFrame* f) {
    if (!f || f->type != FRAME_RESPONSE) return false;
    if (f->function_code != FC_READ_HOLDING_REGS && f->function_code != FC_READ_INPUT_REGS) return false;

    const uint32_t now = millis();
    int best = -1;
    uint32_t best_ts = 0;
    for (int i = 0; i < MAX_CAPTURE_PENDING_REQUESTS; i++) {
        if (!s_capture_pending[i].valid) continue;
        if ((now - s_capture_pending[i].timestamp_ms) > 10000U) {
            s_capture_pending[i].valid = false;
            continue;
        }
        if (s_capture_pending[i].slave_addr != f->slave_addr) continue;
        if (s_capture_pending[i].function_code != f->function_code) continue;
        if (s_capture_pending[i].timestamp_ms >= best_ts) {
            best_ts = s_capture_pending[i].timestamp_ms;
            best = i;
        }
    }
    if (best < 0) return false;
    s_capture_pending[best].valid = false;
    return true;
}

static bool should_capture_raw_frame(const ModbusFrame* f) {
    if (!f || !f->raw || f->raw_len == 0) return false;

    if (s_raw_capture_profile == RAW_CAPTURE_ALL_FRAMES) {
        return true;
    }

    if (s_raw_capture_profile == RAW_CAPTURE_UNKNOWN_H41) {
        return (f->type == FRAME_UNKNOWN && f->function_code == FC_HUAWEI_EXT);
    }

    // compare profile: include proprietary Huawei FC 0x41 frames
    if (s_raw_capture_profile == RAW_CAPTURE_COMPARE_POWER &&
        f->type == FRAME_UNKNOWN && f->function_code == FC_HUAWEI_EXT) {
        return true;
    }
    // research profile: include all unknown-function frames (not only FC 0x41)
    if (s_raw_capture_profile == RAW_CAPTURE_RESEARCH_INVERTER_PHASE &&
        f->type == FRAME_UNKNOWN) {
        return true;
    }

    // and include selected FC03/FC04 request-response pairs
    if (request_matches_profile(f)) {
        capture_store_request(f);
        return true;
    }
    if (capture_match_response(f)) {
        return true;
    }

    return false;
}

static void log_raw_frame(const ModbusFrame* f) {
    if (!should_capture_raw_frame(f)) return;

    raw_frame_streamer_enqueue(f);
    if (raw_frame_streamer_is_enabled() && !raw_frame_streamer_serial_mirror()) return;

    const char* t = (f->type == FRAME_REQUEST)   ? "REQ" :
                    (f->type == FRAME_RESPONSE)  ? "RSP" :
                    (f->type == FRAME_EXCEPTION) ? "EXC" : "UNK";
    const uint8_t sub = (f->raw_len > 2) ? f->raw[2] : 0x00;
    const uint32_t seq = ++s_raw_frame_seq;

    // Chunk long frames to avoid logger line clipping and keep full payload reconstructable.
    const uint16_t bytes_per_chunk = 48;
    const uint16_t chunk_count = (f->raw_len + bytes_per_chunk - 1U) / bytes_per_chunk;
    for (uint16_t ci = 0; ci < chunk_count; ci++) {
        const uint16_t off = ci * bytes_per_chunk;
        const uint16_t end = min((uint16_t)(off + bytes_per_chunk), f->raw_len);

        char hexbuf[48 * 3 + 1];
        int pos = 0;
        for (uint16_t i = off; i < end; i++)
            pos += snprintf(hexbuf + pos, (int)sizeof(hexbuf) - pos, "%02X ", f->raw[i]);
        if (pos > 0) hexbuf[pos - 1] = '\0';  // trim trailing space
        else hexbuf[0] = '\0';

        UnifiedLogger::verbose("[SNIF] FRAME RAW#%lu %s slave=0x%02X fc=0x%02X sub=0x%02X len=%u chunk=%u/%u off=%u: %s\n",
                               (unsigned long)seq,
                               t,
                               f->slave_addr,
                               f->function_code,
                               sub,
                               f->raw_len,
                               (unsigned)(ci + 1U),
                               (unsigned)chunk_count,
                               (unsigned)off,
                               hexbuf);
    }
}

// ============================================================
// Pending request tracker
// ============================================================
#define MAX_PENDING_REQUESTS 4

typedef struct {
    uint8_t  slave_addr;
    uint16_t start_addr;
    uint16_t reg_count;
    uint32_t timestamp_ms;
    bool     valid;
} PendingReq;

static PendingReq s_pending[MAX_PENDING_REQUESTS];

static void store_request(const ModbusFrame* f) {
    int oldest = 0;
    uint32_t oldest_ts = UINT32_MAX;
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (!s_pending[i].valid) { oldest = i; break; }
        if (s_pending[i].timestamp_ms < oldest_ts) {
            oldest_ts = s_pending[i].timestamp_ms;
            oldest = i;
        }
    }
    s_pending[oldest] = { f->slave_addr, f->req_start_addr,
                          f->req_reg_count, (uint32_t)millis(), true };
    UnifiedLogger::verbose("[DECODER] REQ  slave=0x%02X fc=0x%02X start=%u cnt=%u\n",
                           f->slave_addr, f->function_code,
                           f->req_start_addr, f->req_reg_count);
}

static bool find_request(uint8_t addr, PendingReq* out) {
    int best = -1; uint32_t best_ts = 0;
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (s_pending[i].valid && s_pending[i].slave_addr == addr
            && s_pending[i].timestamp_ms >= best_ts) {
            best = i; best_ts = s_pending[i].timestamp_ms;
        }
    }
    if (best < 0) return false;
    *out = s_pending[best];
    s_pending[best].valid = false;
    return true;
}

// ============================================================
// Huawei proprietary FC=0x41 list-framed request/response path
// ============================================================
static const uint32_t H41_FNV1A_INIT = 2166136261u;
static const uint32_t H41_FNV1A_PRIME = 16777619u;

#define MAX_PENDING_H41_REQUESTS 4

typedef struct {
    uint8_t  slave_addr;
    uint8_t  sub_cmd;
    uint16_t item_count;
    uint32_t list_hash;
    uint32_t timestamp_ms;
    bool     valid;
} PendingHuaweiReq;

static PendingHuaweiReq s_pending_h41[MAX_PENDING_H41_REQUESTS];

static uint16_t read_be_u16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t read_be_u32(const uint8_t* p) {
    return ((uint32_t)read_be_u16(p) << 16) | read_be_u16(p + 2);
}

static uint32_t h41_hash_step(uint32_t hash, uint8_t b) {
    return (hash ^ b) * H41_FNV1A_PRIME;
}

static bool h41_parse_header(const ModbusFrame* f,
                             uint8_t* sub_cmd,
                             uint16_t* item_count,
                             const uint8_t** payload,
                             const uint8_t** payload_end) {
    if (!f || !f->raw || f->raw_len < 8) return false;
    if (f->function_code != FC_HUAWEI_EXT) return false;

    const uint8_t payload_len = f->raw[3];
    if ((uint16_t)(payload_len + 6U) != f->raw_len) return false;

    *sub_cmd = f->raw[2];
    *item_count = read_be_u16(&f->raw[4]);
    *payload = &f->raw[6];
    *payload_end = &f->raw[f->raw_len - 2];  // exclude CRC
    return true;
}

static void store_h41_request(const ModbusFrame* f, uint8_t sub_cmd, uint16_t item_count, uint32_t list_hash) {
    int oldest = 0;
    uint32_t oldest_ts = UINT32_MAX;
    for (int i = 0; i < MAX_PENDING_H41_REQUESTS; i++) {
        if (!s_pending_h41[i].valid) { oldest = i; break; }
        if (s_pending_h41[i].timestamp_ms < oldest_ts) {
            oldest_ts = s_pending_h41[i].timestamp_ms;
            oldest = i;
        }
    }
    s_pending_h41[oldest] = { f->slave_addr, sub_cmd, item_count, list_hash, (uint32_t)millis(), true };
}

static bool find_h41_request(uint8_t slave_addr, uint8_t sub_cmd, uint16_t item_count, uint32_t list_hash) {
    int best = -1;
    uint32_t best_ts = 0;
    for (int i = 0; i < MAX_PENDING_H41_REQUESTS; i++) {
        const PendingHuaweiReq* pr = &s_pending_h41[i];
        if (!pr->valid) continue;
        if (pr->slave_addr != slave_addr || pr->sub_cmd != sub_cmd) continue;
        if (pr->item_count != item_count || pr->list_hash != list_hash) continue;
        if (pr->timestamp_ms >= best_ts) { best = i; best_ts = pr->timestamp_ms; }
    }
    if (best < 0) return false;
    s_pending_h41[best].valid = false;
    return true;
}

static bool parse_h41_request_list(const ModbusFrame* f, uint8_t* sub_cmd, uint16_t* item_count, uint32_t* list_hash) {
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    if (!h41_parse_header(f, sub_cmd, item_count, &p, &end)) return false;

    const uint16_t payload_len = f->raw[3];
    if (payload_len != (uint16_t)(2U + (*item_count * 3U))) return false;
    if ((uint16_t)(end - p) != (uint16_t)(*item_count * 3U)) return false;

    uint32_t hash = H41_FNV1A_INIT;
    for (uint16_t i = 0; i < *item_count; i++) {
        hash = h41_hash_step(hash, p[0]);
        hash = h41_hash_step(hash, p[1]);
        hash = h41_hash_step(hash, p[2]);
        p += 3;
    }
    *list_hash = hash;
    return true;
}

static bool decode_h41_entry(uint8_t slave_addr, uint16_t start_addr, uint8_t words,
                             const uint8_t* data, uint8_t source_id, int* out_decoded) {
    bool any = false;
    uint8_t known_word_flags[256] = {0};
    for (uint16_t ri = 0; ri < KNOWN_REG_COUNT; ri++) {
        const RegDesc* rd = &KNOWN_REGS[ri];

        if (rd->addr < start_addr) continue;
        const uint16_t offset = rd->addr - start_addr;
        if ((uint16_t)(offset + rd->words) > (uint16_t)words) continue;
        for (uint16_t w = 0; w < rd->words && (offset + w) < words; w++) {
            known_word_flags[offset + w] = 1;
        }

        RegGroup g = rd->group;
        if (!group_is_seen(g)) {
            UnifiedLogger::info("[DECODER] auto-detected group '%s'\n", GROUP_INFO[g].label);
            group_mark_seen(g);
        }

        if (rd->type == STRING) continue;
        if (!group_is_enabled(g) || !s_cb) continue;

        const uint8_t* rp = data + (offset * 2U);
        float value = 0.0f;
        switch (rd->type) {
            case U16: {
                value = (float)read_be_u16(rp) / rd->scale;
                break;
            }
            case I16: {
                value = (float)(int16_t)read_be_u16(rp) / rd->scale;
                break;
            }
            case U32: {
                value = (float)read_be_u32(rp) / rd->scale;
                break;
            }
            case I32: {
                value = (float)(int32_t)read_be_u32(rp) / rd->scale;
                break;
            }
            case I32ABS: {
                value = (float)abs((int32_t)read_be_u32(rp)) / rd->scale;
                break;
            }
            case STRING: {
                continue;
            }
        }
        s_cb(rd->name, value, rd->unit, slave_addr, g, source_id, rd->addr, rd->words);
        (*out_decoded)++;
        any = true;
    }
    for (uint16_t wi = 0; wi < words; wi++) {
        if (known_word_flags[wi]) continue;
        const uint16_t raw = read_be_u16(data + wi * 2U);
        live_value_store_publish_unknown_u16((uint16_t)(start_addr + wi), 1,
                                             raw, slave_addr, source_id);
    }
    return any;
}

static bool decode_h41_response(const ModbusFrame* f) {
    uint8_t sub_cmd = 0;
    uint16_t item_count = 0;
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    if (!h41_parse_header(f, &sub_cmd, &item_count, &p, &end)) return false;

    uint32_t list_hash = H41_FNV1A_INIT;
    int decoded = 0;
    const uint8_t source_id = (sub_cmd == 0x33) ? SRC_H41_SUB33 : SRC_H41_OTHER;
    for (uint16_t i = 0; i < item_count; i++) {
        if ((end - p) < 3) return false;

        const uint8_t ah = p[0];
        const uint8_t al = p[1];
        const uint8_t words = p[2];
        const uint16_t addr = ((uint16_t)ah << 8) | al;

        list_hash = h41_hash_step(list_hash, ah);
        list_hash = h41_hash_step(list_hash, al);
        list_hash = h41_hash_step(list_hash, words);

        p += 3;
        const uint16_t data_bytes = (uint16_t)words * 2U;
        if ((end - p) < data_bytes) return false;

        decode_h41_entry(f->slave_addr, addr, words, p, source_id, &decoded);
        p += data_bytes;
    }
    if (p != end) return false;

    const bool matched = find_h41_request(f->slave_addr, sub_cmd, item_count, list_hash);
    UnifiedLogger::verbose("[DECODER] H41 RSP slave=0x%02X sub=0x%02X items=%u decoded=%d matched=%s\n",
                           f->slave_addr, sub_cmd, item_count, decoded, matched ? "yes" : "no");
    return true;
}

static bool handle_h41_unknown(const ModbusFrame* f) {
    if (!f || f->function_code != FC_HUAWEI_EXT) return false;

    uint8_t sub_cmd = 0;
    uint16_t item_count = 0;
    uint32_t list_hash = 0;
    if (parse_h41_request_list(f, &sub_cmd, &item_count, &list_hash)) {
        store_h41_request(f, sub_cmd, item_count, list_hash);
        UnifiedLogger::verbose("[DECODER] H41 REQ slave=0x%02X sub=0x%02X items=%u\n",
                               f->slave_addr, sub_cmd, item_count);
        return true;
    }

    return decode_h41_response(f);
}

// ============================================================
// Decode a response given the matched request context
// ============================================================
static void decode_response(const ModbusFrame* f, const PendingReq* req) {
    uint16_t block_start = req->start_addr;
    int n_decoded = 0;
    uint8_t known_word_flags[128] = {0};

    const uint8_t source_id = (f->function_code == 0x04) ? SRC_FC04 : SRC_FC03;
    for (uint16_t ri = 0; ri < KNOWN_REG_COUNT; ri++) {
        const RegDesc* rd = &KNOWN_REGS[ri];

        if (rd->addr < block_start) continue;
        uint16_t offset = rd->addr - block_start;
        if (offset + rd->words - 1 >= req->reg_count) continue;
        if (offset + rd->words > f->rsp_reg_count) continue;
        for (uint16_t w = 0; w < rd->words && (offset + w) < f->rsp_reg_count; w++) {
            known_word_flags[offset + w] = 1;
        }

        RegGroup g = rd->group;

        // Auto-detect: first time we see a register from this group.
        // group_mark_seen() is idempotent — safe to call every time.
        if (!group_is_seen(g)) {
            UnifiedLogger::info("[DECODER] auto-detected group '%s'\n",
                               GROUP_INFO[g].label);
            group_mark_seen(g);  // sets runtime detection flag
        }

        // Skip publish if group disabled by user
        if (!group_is_enabled(g)) continue;

        if (!s_cb) continue;

        float value = 0.0f;
        switch (rd->type) {
            case U16:    value = (float)modbus_get_u16(f, offset) / rd->scale; break;
            case I16:    value = (float)modbus_get_i16(f, offset) / rd->scale; break;
            case U32:    value = (float)modbus_get_u32(f, offset) / rd->scale; break;
            case I32:    value = (float)modbus_get_i32(f, offset) / rd->scale; break;
            case I32ABS: value = (float)abs(modbus_get_i32(f, offset)) / rd->scale; break;
            case STRING: continue;  // group detected above; value publish skipped for strings
        }
        s_cb(rd->name, value, rd->unit, f->slave_addr, g, source_id, rd->addr, rd->words);
        n_decoded++;
    }
    for (uint16_t wi = 0; wi < f->rsp_reg_count; wi++) {
        if (known_word_flags[wi]) continue;
        const uint16_t raw = modbus_get_u16(f, wi);
        live_value_store_publish_unknown_u16((uint16_t)(block_start + wi), 1,
                                             raw, f->slave_addr, source_id);
    }
    UnifiedLogger::verbose("[DECODER] RSP  slave=0x%02X start=%u cnt=%u \u2192 %d reg(s) decoded\n",
                           f->slave_addr, req->start_addr, req->reg_count, n_decoded);
}

// ============================================================
// Cold-start fallback: match response by register count alone
// ============================================================
typedef struct { uint16_t start; uint16_t count; const char* desc; } KnownBlock;
static const KnownBlock KNOWN_BLOCKS[] = {
    // ── Confirmed by live VERBOSE logs (this dongle's actual scan cycle) ──────────
    { 32000,  1, "inverter_state1"   },   // 32000:       state_1 (GRP_INVERTER_STATUS)
    { 32016,  4, "pv_strings_02"     },   // 32016–32019: PV01+PV02 voltage & current
    { 32064, 24, "inverter_ac_24"    },   // 32064–32087: full AC output block (17 decoded)
    { 32106, 10, "inverter_energy10" },   // 32106–32115: accumulated/dc/hourly/daily yield
    { 37113, 25, "meter_25"          },   // 37113–37137: meter power+energy+phase (14 decoded)
    { 40000,  2, "optimizer_trigger" },   // 40000–40001: SDongle proprietary (0 decoded, starts PLC)

    // ── Occasionally seen / alternative block sizes ───────────────────────────────
    { 37100, 39, "meter_full"        },   // 37100–37138: full meter block (some FW versions)
    { 32064, 32, "inverter_ac_32"    },   // 32064–32095: with active_power_fast
    { 32064, 18, "inverter_ac_short" },   // 32064–32081: short variant
    { 32106, 14, "inverter_energy14" },   // 32106–32119: extended energy totals
    { 37760, 26, "battery_combined"  },   // 37760–37785: aggregated battery
    { 37000, 69, "battery_unit1"     },   // 37000–37068: unit 1 full
    { 32016, 48, "pv_strings_all"    },   // 32016–32063: all 24 strings
    { 32080,  6, "inverter_power"    },   // 32080–32085: power+PF+Hz subset
    { 30000, 15, "model_name"        },   // 30000–30014: model name string
    { 30015, 10, "serial_number"     },   // 30015–30024: serial number string
    { 31000, 70, "hardware_info"     },   // 31000–31069: hw+monitor+dsp versions
    { 37200,  2, "optimizer_counts"  },   // 37200–37201: nb_optimizers + nb_online_optimizers
};
#define KNOWN_BLOCK_COUNT (sizeof(KNOWN_BLOCKS)/sizeof(KNOWN_BLOCKS[0]))

static bool try_fallback_decode(const ModbusFrame* f) {
    for (uint8_t bi = 0; bi < KNOWN_BLOCK_COUNT; bi++) {
        if (KNOWN_BLOCKS[bi].count == f->rsp_reg_count) {
            UnifiedLogger::info("[DECODER] cold-start: matched '%s' (start=%u cnt=%u)\n",
                               KNOWN_BLOCKS[bi].desc,
                               KNOWN_BLOCKS[bi].start, KNOWN_BLOCKS[bi].count);
            PendingReq fake = { f->slave_addr, KNOWN_BLOCKS[bi].start,
                                KNOWN_BLOCKS[bi].count, 0, true };
            decode_response(f, &fake);
            return true;
        }
    }
    return false;
}

// ============================================================
// Public feed / expire
// ============================================================
void huawei_decoder_feed(const ModbusFrame* frame) {
    if (s_raw_dump) log_raw_frame(frame);
    if (frame->type == FRAME_REQUEST) {
        store_request(frame);
        return;
    }
    if (frame->type == FRAME_RESPONSE) {
        PendingReq req;
        if (find_request(frame->slave_addr, &req)) {
            decode_response(frame, &req);
        } else {
            if (!try_fallback_decode(frame)) {
                UnifiedLogger::info("[DECODER] unknown block: slave=0x%02X regs=%u (no pending REQ — start addr unknown)\n",
                                   frame->slave_addr, frame->rsp_reg_count);
            }
        }
    }
    if (frame->type == FRAME_UNKNOWN) {
        (void)handle_h41_unknown(frame);
    }
    if (frame->type == FRAME_EXCEPTION) {
        UnifiedLogger::warning("[DECODER] exception slave=0x%02X fc=0x%02X code=0x%02X\n",
                              frame->slave_addr, frame->function_code, frame->exception_code);
    }
}

void huawei_decoder_expire_pending(uint32_t timeout_ms) {
    uint32_t now = millis();
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++)
        if (s_pending[i].valid && (now - s_pending[i].timestamp_ms) > timeout_ms)
            s_pending[i].valid = false;
    for (int i = 0; i < MAX_PENDING_H41_REQUESTS; i++)
        if (s_pending_h41[i].valid && (now - s_pending_h41[i].timestamp_ms) > timeout_ms)
            s_pending_h41[i].valid = false;
}
