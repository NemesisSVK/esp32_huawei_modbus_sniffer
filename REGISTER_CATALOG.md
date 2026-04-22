# REGISTER_CATALOG.md
Last-Updated-UTC: 2026-04-22 10:40

Dedicated cumulative register catalog for this project.
- `decoded`: implemented in `src/huawei_decoder.cpp` (`KNOWN_REGS`) and published through group routing.
- `observed_not_decoded`: seen in CRC-valid FC `0x41` traffic but not present in `KNOWN_REGS`.
- Sources merged: sniffer logs (`!CLEAN!sniffer-log*`, `sniffer-log*`), canonical `_supporting-projects/huawei-solar-lib-3.0.0/src/huawei_solar/registers.py`, and Huawei PDF `SUN2000MA V200R024C00SPC106 Modbus Interface Definitions(V3.0).pdf`.
- Home-page category model: `/api/values` uses `group = GROUP_INFO[].mqtt_subtopic` from `src/reg_groups.h`.

## 0) Home Page Grouping & Source Legend

### 0.1 Source tags shown on Home page

| UI tag | Meaning | Typical frame source |
|---|---|---|
| `DOC` | Canonical/officially mapped register decode path | FC `0x03`, FC `0x04` |
| `REV` | Reverse-engineered proprietary decode path | FC `0x41` (`H41-33`, `H41-X`) |
| `UNK` | Source not classified yet | fallback/unknown |

### 0.2 Group mapping used by Home page (`group` field)

| enum group (`KNOWN_REGS`) | home group key (`/api/values`) | Home page label | decoded count |
|---|---|---|---:|
| `GRP_METER` | `meter` | Grid Meter | 27 |
| `GRP_INVERTER_AC` | `inverter_ac` | Inverter AC Output | 20 |
| `GRP_INVERTER_STATUS` | `inverter_status` | Inverter Status & Alarms | 13 |
| `GRP_INVERTER_ENERGY` | `inverter_energy` | Inverter Energy Totals | 16 |
| `GRP_INVERTER_INFO` | `inverter_info` | Inverter Device Info | 20 |
| `GRP_PV_STRINGS` | `pv_strings` | PV Strings (DC input) | 48 |
| `GRP_BATTERY` | `battery` | Battery (Aggregate) | 12 |
| `GRP_BATTERY_UNIT1` | `battery_u1` | Battery Unit 1 | 15 |
| `GRP_BATTERY_UNIT2` | `battery_u2` | Battery Unit 2 | 10 |
| `GRP_BATTERY_PACKS` | `battery_packs` | Battery Pack Details | 48 |
| `GRP_BATTERY_SETTINGS` | `battery_cfg` | Battery Settings | 20 |
| `GRP_SDONGLE` | `sdongle` | SDongle Aggregates | 5 |

## 1) Decoded Register Inventory (KNOWN_REGS)
- Total decoded entries: 254
- Grouping note: table `group` column is enum-style (`GRP_*`); use section `0.2` for Home-page category key/label mapping.

| addr | words | type | scale | unit | name | group | status |
|---:|---:|---|---:|---|---|---|---|
| 16300 | 2 | I32 | 1 | W | `meter_active_power_fast` | `GRP_METER` | decoded (confirmed from FC `0x41` compare-power captures) |
| 16305 | 1 | I16 | 1 | W | `grid_a_power_fast` | `GRP_METER` | decoded (confirmed from FC `0x41` compare-power captures) |
| 16307 | 1 | I16 | 1 | W | `grid_b_power_fast` | `GRP_METER` | decoded (confirmed from FC `0x41` compare-power captures) |
| 16309 | 1 | I16 | 1 | W | `grid_c_power_fast` | `GRP_METER` | decoded (confirmed from FC `0x41` compare-power captures) |
| 16312 | 1 | I16 | 1 | var | `meter_reactive_power_fast` | `GRP_METER` | decoded (confirmed from FC `0x41` vs FC03 `37115` correlation) |
| 30000 | 15 | STRING | 1 | - | `model_name` | `GRP_INVERTER_INFO` | decoded |
| 30015 | 10 | STRING | 1 | - | `serial_number` | `GRP_INVERTER_INFO` | decoded |
| 30025 | 10 | STRING | 1 | - | `pn` | `GRP_INVERTER_INFO` | decoded |
| 30035 | 15 | STRING | 1 | - | `firmware_version` | `GRP_INVERTER_INFO` | decoded |
| 30050 | 15 | STRING | 1 | - | `software_version` | `GRP_INVERTER_INFO` | decoded |
| 30068 | 2 | U32 | 1 | - | `protocol_version` | `GRP_INVERTER_INFO` | decoded |
| 30070 | 1 | U16 | 1 | - | `model_id` | `GRP_INVERTER_INFO` | decoded |
| 30071 | 1 | U16 | 1 | - | `nb_pv_strings` | `GRP_INVERTER_INFO` | decoded |
| 30072 | 1 | U16 | 1 | - | `nb_mpp_tracks` | `GRP_INVERTER_INFO` | decoded |
| 30073 | 2 | U32 | 1 | W | `rated_power` | `GRP_INVERTER_INFO` | decoded |
| 30075 | 2 | U32 | 1 | W | `p_max` | `GRP_INVERTER_INFO` | decoded |
| 30110 | 1 | U16 | 1 | - | `sw_unique_id` | `GRP_INVERTER_INFO` | decoded |
| 30207 | 2 | U32 | 1 | - | `subdevice_support_flag` | `GRP_INVERTER_INFO` | decoded |
| 31000 | 15 | STRING | 1 | - | `hardware_version` | `GRP_INVERTER_INFO` | decoded |
| 31015 | 10 | STRING | 1 | - | `monitoring_board_sn` | `GRP_INVERTER_INFO` | decoded |
| 31025 | 15 | STRING | 1 | - | `monitoring_sw_version` | `GRP_INVERTER_INFO` | decoded |
| 31040 | 15 | STRING | 1 | - | `master_dsp_version` | `GRP_INVERTER_INFO` | decoded |
| 31055 | 15 | STRING | 1 | - | `slave_dsp_version` | `GRP_INVERTER_INFO` | decoded |
| 32000 | 1 | U16 | 1 | - | `state_1` | `GRP_INVERTER_STATUS` | decoded |
| 32002 | 1 | U16 | 1 | - | `state_2` | `GRP_INVERTER_STATUS` | decoded |
| 32003 | 2 | U32 | 1 | - | `state_3` | `GRP_INVERTER_STATUS` | decoded |
| 32008 | 1 | U16 | 1 | - | `alarm_1` | `GRP_INVERTER_STATUS` | decoded |
| 32009 | 1 | U16 | 1 | - | `alarm_2` | `GRP_INVERTER_STATUS` | decoded |
| 32010 | 1 | U16 | 1 | - | `alarm_3` | `GRP_INVERTER_STATUS` | decoded |
| 32016 | 1 | I16 | 10 | V | `pv01_voltage` | `GRP_PV_STRINGS` | decoded |
| 32017 | 1 | I16 | 100 | A | `pv01_current` | `GRP_PV_STRINGS` | decoded |
| 32018 | 1 | I16 | 10 | V | `pv02_voltage` | `GRP_PV_STRINGS` | decoded |
| 32019 | 1 | I16 | 100 | A | `pv02_current` | `GRP_PV_STRINGS` | decoded |
| 32020 | 1 | I16 | 10 | V | `pv03_voltage` | `GRP_PV_STRINGS` | decoded |
| 32021 | 1 | I16 | 100 | A | `pv03_current` | `GRP_PV_STRINGS` | decoded |
| 32022 | 1 | I16 | 10 | V | `pv04_voltage` | `GRP_PV_STRINGS` | decoded |
| 32023 | 1 | I16 | 100 | A | `pv04_current` | `GRP_PV_STRINGS` | decoded |
| 32024 | 1 | I16 | 10 | V | `pv05_voltage` | `GRP_PV_STRINGS` | decoded |
| 32025 | 1 | I16 | 100 | A | `pv05_current` | `GRP_PV_STRINGS` | decoded |
| 32026 | 1 | I16 | 10 | V | `pv06_voltage` | `GRP_PV_STRINGS` | decoded |
| 32027 | 1 | I16 | 100 | A | `pv06_current` | `GRP_PV_STRINGS` | decoded |
| 32028 | 1 | I16 | 10 | V | `pv07_voltage` | `GRP_PV_STRINGS` | decoded |
| 32029 | 1 | I16 | 100 | A | `pv07_current` | `GRP_PV_STRINGS` | decoded |
| 32030 | 1 | I16 | 10 | V | `pv08_voltage` | `GRP_PV_STRINGS` | decoded |
| 32031 | 1 | I16 | 100 | A | `pv08_current` | `GRP_PV_STRINGS` | decoded |
| 32032 | 1 | I16 | 10 | V | `pv09_voltage` | `GRP_PV_STRINGS` | decoded |
| 32033 | 1 | I16 | 100 | A | `pv09_current` | `GRP_PV_STRINGS` | decoded |
| 32034 | 1 | I16 | 10 | V | `pv10_voltage` | `GRP_PV_STRINGS` | decoded |
| 32035 | 1 | I16 | 100 | A | `pv10_current` | `GRP_PV_STRINGS` | decoded |
| 32036 | 1 | I16 | 10 | V | `pv11_voltage` | `GRP_PV_STRINGS` | decoded |
| 32037 | 1 | I16 | 100 | A | `pv11_current` | `GRP_PV_STRINGS` | decoded |
| 32038 | 1 | I16 | 10 | V | `pv12_voltage` | `GRP_PV_STRINGS` | decoded |
| 32039 | 1 | I16 | 100 | A | `pv12_current` | `GRP_PV_STRINGS` | decoded |
| 32040 | 1 | I16 | 10 | V | `pv13_voltage` | `GRP_PV_STRINGS` | decoded |
| 32041 | 1 | I16 | 100 | A | `pv13_current` | `GRP_PV_STRINGS` | decoded |
| 32042 | 1 | I16 | 10 | V | `pv14_voltage` | `GRP_PV_STRINGS` | decoded |
| 32043 | 1 | I16 | 100 | A | `pv14_current` | `GRP_PV_STRINGS` | decoded |
| 32044 | 1 | I16 | 10 | V | `pv15_voltage` | `GRP_PV_STRINGS` | decoded |
| 32045 | 1 | I16 | 100 | A | `pv15_current` | `GRP_PV_STRINGS` | decoded |
| 32046 | 1 | I16 | 10 | V | `pv16_voltage` | `GRP_PV_STRINGS` | decoded |
| 32047 | 1 | I16 | 100 | A | `pv16_current` | `GRP_PV_STRINGS` | decoded |
| 32048 | 1 | I16 | 10 | V | `pv17_voltage` | `GRP_PV_STRINGS` | decoded |
| 32049 | 1 | I16 | 100 | A | `pv17_current` | `GRP_PV_STRINGS` | decoded |
| 32050 | 1 | I16 | 10 | V | `pv18_voltage` | `GRP_PV_STRINGS` | decoded |
| 32051 | 1 | I16 | 100 | A | `pv18_current` | `GRP_PV_STRINGS` | decoded |
| 32052 | 1 | I16 | 10 | V | `pv19_voltage` | `GRP_PV_STRINGS` | decoded |
| 32053 | 1 | I16 | 100 | A | `pv19_current` | `GRP_PV_STRINGS` | decoded |
| 32054 | 1 | I16 | 10 | V | `pv20_voltage` | `GRP_PV_STRINGS` | decoded |
| 32055 | 1 | I16 | 100 | A | `pv20_current` | `GRP_PV_STRINGS` | decoded |
| 32056 | 1 | I16 | 10 | V | `pv21_voltage` | `GRP_PV_STRINGS` | decoded |
| 32057 | 1 | I16 | 100 | A | `pv21_current` | `GRP_PV_STRINGS` | decoded |
| 32058 | 1 | I16 | 10 | V | `pv22_voltage` | `GRP_PV_STRINGS` | decoded |
| 32059 | 1 | I16 | 100 | A | `pv22_current` | `GRP_PV_STRINGS` | decoded |
| 32060 | 1 | I16 | 10 | V | `pv23_voltage` | `GRP_PV_STRINGS` | decoded |
| 32061 | 1 | I16 | 100 | A | `pv23_current` | `GRP_PV_STRINGS` | decoded |
| 32062 | 1 | I16 | 10 | V | `pv24_voltage` | `GRP_PV_STRINGS` | decoded |
| 32063 | 1 | I16 | 100 | A | `pv24_current` | `GRP_PV_STRINGS` | decoded |
| 32064 | 2 | I32 | 1 | W | `input_power` | `GRP_INVERTER_AC` | decoded |
| 32066 | 1 | U16 | 10 | V | `line_voltage_ab` | `GRP_INVERTER_AC` | decoded |
| 32067 | 1 | U16 | 10 | V | `line_voltage_bc` | `GRP_INVERTER_AC` | decoded |
| 32068 | 1 | U16 | 10 | V | `line_voltage_ca` | `GRP_INVERTER_AC` | decoded |
| 32069 | 1 | U16 | 10 | V | `phase_a_voltage` | `GRP_INVERTER_AC` | decoded |
| 32070 | 1 | U16 | 10 | V | `phase_b_voltage` | `GRP_INVERTER_AC` | decoded |
| 32071 | 1 | U16 | 10 | V | `phase_c_voltage` | `GRP_INVERTER_AC` | decoded |
| 32072 | 2 | I32 | 1000 | A | `phase_a_current` | `GRP_INVERTER_AC` | decoded |
| 32074 | 2 | I32 | 1000 | A | `phase_b_current` | `GRP_INVERTER_AC` | decoded |
| 32076 | 2 | I32 | 1000 | A | `phase_c_current` | `GRP_INVERTER_AC` | decoded |
| 32078 | 2 | I32 | 1 | W | `peak_active_power` | `GRP_INVERTER_AC` | decoded |
| 32080 | 2 | I32 | 1 | W | `active_power` | `GRP_INVERTER_AC` | decoded |
| 32082 | 2 | I32 | 1 | var | `reactive_power` | `GRP_INVERTER_AC` | decoded |
| 32084 | 1 | I16 | 1000 | - | `power_factor` | `GRP_INVERTER_AC` | decoded |
| 32085 | 1 | U16 | 100 | Hz | `grid_frequency` | `GRP_INVERTER_AC` | decoded |
| 32086 | 1 | U16 | 100 | % | `efficiency` | `GRP_INVERTER_AC` | decoded |
| 32087 | 1 | I16 | 10 | degC | `internal_temperature` | `GRP_INVERTER_AC` | decoded |
| 32088 | 1 | U16 | 1000 | MOhm | `insulation_resistance` | `GRP_INVERTER_AC` | decoded |
| 32089 | 1 | U16 | 1 | - | `device_status` | `GRP_INVERTER_STATUS` | decoded |
| 32090 | 1 | U16 | 1 | - | `fault_code` | `GRP_INVERTER_STATUS` | decoded |
| 32091 | 2 | U32 | 1 | s | `startup_time` | `GRP_INVERTER_STATUS` | decoded |
| 32093 | 2 | U32 | 1 | s | `shutdown_time` | `GRP_INVERTER_STATUS` | decoded |
| 32095 | 2 | I32 | 1 | W | `active_power_fast` | `GRP_INVERTER_AC` | decoded |
| 32106 | 2 | U32 | 100 | kWh | `accumulated_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32108 | 2 | U32 | 100 | kWh | `total_dc_input_power` | `GRP_INVERTER_ENERGY` | decoded |
| 32112 | 2 | U32 | 100 | kWh | `hourly_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32114 | 2 | U32 | 100 | kWh | `daily_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32116 | 2 | U32 | 100 | kWh | `monthly_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32118 | 2 | U32 | 100 | kWh | `yearly_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32172 | 2 | U32 | 1 | - | `latest_active_alarm_sn` | `GRP_INVERTER_STATUS` | decoded |
| 32174 | 2 | U32 | 1 | - | `latest_hist_alarm_sn` | `GRP_INVERTER_STATUS` | decoded |
| 32212 | 2 | U32 | 100 | kWh | `mppt1_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32214 | 2 | U32 | 100 | kWh | `mppt2_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32216 | 2 | U32 | 100 | kWh | `mppt3_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32218 | 2 | U32 | 100 | kWh | `mppt4_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32220 | 2 | U32 | 100 | kWh | `mppt5_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32222 | 2 | U32 | 100 | kWh | `mppt6_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32224 | 2 | U32 | 100 | kWh | `mppt7_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32226 | 2 | U32 | 100 | kWh | `mppt8_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32228 | 2 | U32 | 100 | kWh | `mppt9_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 32230 | 2 | U32 | 100 | kWh | `mppt10_dc_yield` | `GRP_INVERTER_ENERGY` | decoded |
| 37000 | 1 | U16 | 1 | - | `u1_status` | `GRP_BATTERY_UNIT1` | decoded |
| 37001 | 2 | I32 | 1 | W | `u1_charge_power` | `GRP_BATTERY_UNIT1` | decoded |
| 37003 | 1 | U16 | 10 | V | `u1_bus_voltage` | `GRP_BATTERY_UNIT1` | decoded |
| 37004 | 1 | U16 | 10 | % | `u1_soc` | `GRP_BATTERY_UNIT1` | decoded |
| 37006 | 1 | U16 | 1 | - | `u1_working_mode_b` | `GRP_BATTERY_UNIT1` | decoded |
| 37007 | 2 | U32 | 1 | W | `u1_rated_charge_pwr` | `GRP_BATTERY_UNIT1` | decoded |
| 37009 | 2 | U32 | 1 | W | `u1_rated_discharge_pwr` | `GRP_BATTERY_UNIT1` | decoded |
| 37014 | 1 | U16 | 1 | - | `u1_fault_id` | `GRP_BATTERY_UNIT1` | decoded |
| 37015 | 2 | U32 | 100 | kWh | `u1_day_charge` | `GRP_BATTERY_UNIT1` | decoded |
| 37017 | 2 | U32 | 100 | kWh | `u1_day_discharge` | `GRP_BATTERY_UNIT1` | decoded |
| 37021 | 1 | I16 | 10 | A | `u1_bus_current` | `GRP_BATTERY_UNIT1` | decoded |
| 37022 | 1 | I16 | 10 | degC | `u1_temperature` | `GRP_BATTERY_UNIT1` | decoded |
| 37025 | 1 | U16 | 1 | min | `u1_remaining_time` | `GRP_BATTERY_UNIT1` | decoded |
| 37066 | 2 | U32 | 100 | kWh | `u1_total_charge` | `GRP_BATTERY_UNIT1` | decoded |
| 37068 | 2 | U32 | 100 | kWh | `u1_total_discharge` | `GRP_BATTERY_UNIT1` | decoded |
| 37100 | 1 | U16 | 1 | - | `meter_status` | `GRP_METER` | decoded |
| 37101 | 2 | I32 | 10 | V | `grid_a_voltage` | `GRP_METER` | decoded |
| 37103 | 2 | I32 | 10 | V | `grid_b_voltage` | `GRP_METER` | decoded |
| 37105 | 2 | I32 | 10 | V | `grid_c_voltage` | `GRP_METER` | decoded |
| 37107 | 2 | I32 | 100 | A | `grid_a_current` | `GRP_METER` | decoded |
| 37109 | 2 | I32 | 100 | A | `grid_b_current` | `GRP_METER` | decoded |
| 37111 | 2 | I32 | 100 | A | `grid_c_current` | `GRP_METER` | decoded |
| 37113 | 2 | I32 | 1 | W | `meter_active_power` | `GRP_METER` | decoded |
| 37115 | 2 | I32 | 1 | var | `meter_reactive_power` | `GRP_METER` | decoded |
| 37117 | 1 | I16 | 1000 | - | `meter_power_factor` | `GRP_METER` | decoded |
| 37118 | 1 | I16 | 100 | Hz | `meter_frequency` | `GRP_METER` | decoded |
| 37119 | 2 | I32ABS | 100 | kWh | `grid_exported_energy` | `GRP_METER` | decoded |
| 37121 | 2 | I32 | 100 | kWh | `grid_imported_energy` | `GRP_METER` | decoded |
| 37123 | 2 | I32 | 100 | kvarh | `grid_reactive_energy` | `GRP_METER` | decoded |
| 37125 | 1 | U16 | 1 | - | `meter_type` | `GRP_METER` | decoded |
| 37126 | 2 | I32 | 10 | V | `grid_ab_voltage` | `GRP_METER` | decoded |
| 37128 | 2 | I32 | 10 | V | `grid_bc_voltage` | `GRP_METER` | decoded |
| 37130 | 2 | I32 | 10 | V | `grid_ca_voltage` | `GRP_METER` | decoded |
| 37132 | 2 | I32 | 1 | W | `grid_a_power` | `GRP_METER` | decoded |
| 37134 | 2 | I32 | 1 | W | `grid_b_power` | `GRP_METER` | decoded |
| 37136 | 2 | I32 | 1 | W | `grid_c_power` | `GRP_METER` | decoded |
| 37138 | 1 | U16 | 1 | - | `meter_type_check` | `GRP_METER` | decoded |
| 37200 | 1 | U16 | 1 | - | `nb_optimizers` | `GRP_INVERTER_INFO` | decoded |
| 37201 | 1 | U16 | 1 | - | `nb_online_optimizers` | `GRP_INVERTER_INFO` | decoded |
| 37498 | 2 | U32 | 1000 | kW | `sdongle_pv_power` | `GRP_SDONGLE` | decoded |
| 37500 | 2 | U32 | 1000 | kW | `sdongle_load_power` | `GRP_SDONGLE` | decoded |
| 37502 | 2 | I32 | 1000 | kW | `sdongle_grid_power` | `GRP_SDONGLE` | decoded |
| 37504 | 2 | I32 | 1000 | kW | `sdongle_battery_power` | `GRP_SDONGLE` | decoded |
| 37516 | 2 | I32 | 1000 | kW | `sdongle_total_power` | `GRP_SDONGLE` | decoded |
| 37738 | 1 | U16 | 10 | % | `u2_soc` | `GRP_BATTERY_UNIT2` | decoded |
| 37741 | 1 | U16 | 1 | - | `u2_status` | `GRP_BATTERY_UNIT2` | decoded |
| 37743 | 2 | I32 | 1 | W | `u2_charge_power` | `GRP_BATTERY_UNIT2` | decoded |
| 37746 | 2 | U32 | 100 | kWh | `u2_day_charge` | `GRP_BATTERY_UNIT2` | decoded |
| 37748 | 2 | U32 | 100 | kWh | `u2_day_discharge` | `GRP_BATTERY_UNIT2` | decoded |
| 37750 | 1 | U16 | 10 | V | `u2_bus_voltage` | `GRP_BATTERY_UNIT2` | decoded |
| 37751 | 1 | I16 | 10 | A | `u2_bus_current` | `GRP_BATTERY_UNIT2` | decoded |
| 37752 | 1 | I16 | 10 | degC | `u2_temperature` | `GRP_BATTERY_UNIT2` | decoded |
| 37753 | 2 | U32 | 100 | kWh | `u2_total_charge` | `GRP_BATTERY_UNIT2` | decoded |
| 37755 | 2 | U32 | 100 | kWh | `u2_total_discharge` | `GRP_BATTERY_UNIT2` | decoded |
| 37758 | 2 | U32 | 1 | Wh | `battery_rated_capacity` | `GRP_BATTERY` | decoded |
| 37760 | 1 | U16 | 10 | % | `battery_soc` | `GRP_BATTERY` | decoded |
| 37762 | 1 | U16 | 1 | - | `battery_status` | `GRP_BATTERY` | decoded |
| 37763 | 1 | U16 | 10 | V | `battery_bus_voltage` | `GRP_BATTERY` | decoded |
| 37764 | 1 | I16 | 10 | A | `battery_bus_current` | `GRP_BATTERY` | decoded |
| 37765 | 2 | I32 | 1 | W | `battery_power` | `GRP_BATTERY` | decoded |
| 37780 | 2 | U32 | 100 | kWh | `battery_total_charge` | `GRP_BATTERY` | decoded |
| 37782 | 2 | U32 | 100 | kWh | `battery_total_discharge` | `GRP_BATTERY` | decoded |
| 37784 | 2 | U32 | 100 | kWh | `battery_day_charge` | `GRP_BATTERY` | decoded |
| 37786 | 2 | U32 | 100 | kWh | `battery_day_discharge` | `GRP_BATTERY` | decoded |
| 37926 | 1 | U16 | 1 | - | `battery_soh_calib_status` | `GRP_BATTERY` | decoded |
| 37927 | 1 | U16 | 1 | - | `battery_soh_calib_soc_low` | `GRP_BATTERY` | decoded |
| 38228 | 1 | U16 | 1 | - | `u1p1_working_status` | `GRP_BATTERY_PACKS` | decoded |
| 38229 | 1 | U16 | 10 | % | `u1p1_soc` | `GRP_BATTERY_PACKS` | decoded |
| 38233 | 2 | I32 | 1 | W | `u1p1_power` | `GRP_BATTERY_PACKS` | decoded |
| 38235 | 1 | U16 | 10 | V | `u1p1_voltage` | `GRP_BATTERY_PACKS` | decoded |
| 38236 | 1 | I16 | 10 | A | `u1p1_current` | `GRP_BATTERY_PACKS` | decoded |
| 38238 | 2 | U32 | 100 | kWh | `u1p1_total_charge` | `GRP_BATTERY_PACKS` | decoded |
| 38240 | 2 | U32 | 100 | kWh | `u1p1_total_discharge` | `GRP_BATTERY_PACKS` | decoded |
| 38270 | 1 | U16 | 1 | - | `u1p2_working_status` | `GRP_BATTERY_PACKS` | decoded |
| 38271 | 1 | U16 | 10 | % | `u1p2_soc` | `GRP_BATTERY_PACKS` | decoded |
| 38275 | 2 | I32 | 1 | W | `u1p2_power` | `GRP_BATTERY_PACKS` | decoded |
| 38277 | 1 | U16 | 10 | V | `u1p2_voltage` | `GRP_BATTERY_PACKS` | decoded |
| 38278 | 1 | I16 | 10 | A | `u1p2_current` | `GRP_BATTERY_PACKS` | decoded |
| 38312 | 1 | U16 | 1 | - | `u1p3_working_status` | `GRP_BATTERY_PACKS` | decoded |
| 38313 | 1 | U16 | 10 | % | `u1p3_soc` | `GRP_BATTERY_PACKS` | decoded |
| 38317 | 2 | I32 | 1 | W | `u1p3_power` | `GRP_BATTERY_PACKS` | decoded |
| 38319 | 1 | U16 | 10 | V | `u1p3_voltage` | `GRP_BATTERY_PACKS` | decoded |
| 38320 | 1 | I16 | 10 | A | `u1p3_current` | `GRP_BATTERY_PACKS` | decoded |
| 38354 | 1 | U16 | 1 | - | `u2p1_working_status` | `GRP_BATTERY_PACKS` | decoded |
| 38355 | 1 | U16 | 10 | % | `u2p1_soc` | `GRP_BATTERY_PACKS` | decoded |
| 38359 | 2 | I32 | 1 | W | `u2p1_power` | `GRP_BATTERY_PACKS` | decoded |
| 38361 | 1 | U16 | 10 | V | `u2p1_voltage` | `GRP_BATTERY_PACKS` | decoded |
| 38362 | 1 | I16 | 10 | A | `u2p1_current` | `GRP_BATTERY_PACKS` | decoded |
| 38364 | 2 | U32 | 100 | kWh | `u2p1_total_charge` | `GRP_BATTERY_PACKS` | decoded |
| 38366 | 2 | U32 | 100 | kWh | `u2p1_total_discharge` | `GRP_BATTERY_PACKS` | decoded |
| 38396 | 1 | U16 | 1 | - | `u2p2_working_status` | `GRP_BATTERY_PACKS` | decoded |
| 38397 | 1 | U16 | 10 | % | `u2p2_soc` | `GRP_BATTERY_PACKS` | decoded |
| 38401 | 2 | I32 | 1 | W | `u2p2_power` | `GRP_BATTERY_PACKS` | decoded |
| 38403 | 1 | U16 | 10 | V | `u2p2_voltage` | `GRP_BATTERY_PACKS` | decoded |
| 38404 | 1 | I16 | 10 | A | `u2p2_current` | `GRP_BATTERY_PACKS` | decoded |
| 38438 | 1 | U16 | 1 | - | `u2p3_working_status` | `GRP_BATTERY_PACKS` | decoded |
| 38439 | 1 | U16 | 10 | % | `u2p3_soc` | `GRP_BATTERY_PACKS` | decoded |
| 38443 | 2 | I32 | 1 | W | `u2p3_power` | `GRP_BATTERY_PACKS` | decoded |
| 38445 | 1 | U16 | 10 | V | `u2p3_voltage` | `GRP_BATTERY_PACKS` | decoded |
| 38446 | 1 | I16 | 10 | A | `u2p3_current` | `GRP_BATTERY_PACKS` | decoded |
| 38448 | 2 | U32 | 100 | kWh | `u2p3_total_charge` | `GRP_BATTERY_PACKS` | decoded |
| 38450 | 2 | U32 | 100 | kWh | `u2p3_total_discharge` | `GRP_BATTERY_PACKS` | decoded |
| 38452 | 1 | I16 | 10 | degC | `u1p1_max_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38453 | 1 | I16 | 10 | degC | `u1p1_min_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38454 | 1 | I16 | 10 | degC | `u1p2_max_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38455 | 1 | I16 | 10 | degC | `u1p2_min_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38456 | 1 | I16 | 10 | degC | `u1p3_max_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38457 | 1 | I16 | 10 | degC | `u1p3_min_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38458 | 1 | I16 | 10 | degC | `u2p1_max_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38459 | 1 | I16 | 10 | degC | `u2p1_min_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38460 | 1 | I16 | 10 | degC | `u2p2_max_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38461 | 1 | I16 | 10 | degC | `u2p2_min_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38462 | 1 | I16 | 10 | degC | `u2p3_max_temp` | `GRP_BATTERY_PACKS` | decoded |
| 38463 | 1 | I16 | 10 | degC | `u2p3_min_temp` | `GRP_BATTERY_PACKS` | decoded |
| 42045 | 1 | U16 | 1 | - | `off_grid_mode` | `GRP_INVERTER_STATUS` | decoded |
| 42056 | 2 | U32 | 1 | W | `mppt_predicted_power` | `GRP_INVERTER_AC` | decoded |
| 47000 | 1 | U16 | 1 | - | `u1_product_model` | `GRP_BATTERY_SETTINGS` | decoded |
| 47004 | 1 | I16 | 1 | - | `working_mode_a` | `GRP_BATTERY_SETTINGS` | decoded |
| 47027 | 1 | U16 | 1 | - | `tou_price_enabled` | `GRP_BATTERY_SETTINGS` | decoded |
| 47075 | 2 | U32 | 1 | W | `max_charge_power` | `GRP_BATTERY_SETTINGS` | decoded |
| 47077 | 2 | U32 | 1 | W | `max_discharge_power` | `GRP_BATTERY_SETTINGS` | decoded |
| 47079 | 2 | I32 | 1 | W | `grid_tied_pwr_limit` | `GRP_BATTERY_SETTINGS` | decoded |
| 47081 | 1 | U16 | 10 | % | `charge_cutoff_soc` | `GRP_BATTERY_SETTINGS` | decoded |
| 47082 | 1 | U16 | 10 | % | `discharge_cutoff_soc` | `GRP_BATTERY_SETTINGS` | decoded |
| 47086 | 1 | U16 | 1 | - | `working_mode` | `GRP_BATTERY_SETTINGS` | decoded |
| 47087 | 1 | U16 | 1 | - | `charge_from_grid` | `GRP_BATTERY_SETTINGS` | decoded |
| 47088 | 1 | U16 | 10 | % | `grid_charge_cutoff_soc` | `GRP_BATTERY_SETTINGS` | decoded |
| 47089 | 1 | U16 | 1 | - | `u2_product_model` | `GRP_BATTERY_SETTINGS` | decoded |
| 47100 | 1 | U16 | 1 | - | `forcible_mode` | `GRP_BATTERY_SETTINGS` | decoded |
| 47101 | 1 | U16 | 10 | % | `forcible_soc` | `GRP_BATTERY_SETTINGS` | decoded |
| 47102 | 1 | U16 | 10 | % | `backup_power_soc` | `GRP_BATTERY_SETTINGS` | decoded |
| 47242 | 2 | U32 | 1 | W | `charge_from_grid_power` | `GRP_BATTERY_SETTINGS` | decoded |
| 47415 | 1 | U16 | 1 | - | `active_pwr_ctrl_mode` | `GRP_BATTERY_SETTINGS` | decoded |
| 47416 | 2 | I32 | 1 | W | `max_feed_grid_power_w` | `GRP_BATTERY_SETTINGS` | decoded |
| 47418 | 1 | I16 | 10 | % | `max_feed_grid_pct` | `GRP_BATTERY_SETTINGS` | decoded |
| 47675 | 2 | I32 | 1 | W | `default_max_feed_in_pwr` | `GRP_BATTERY_SETTINGS` | decoded |

## 2) Observed FC 0x41 / 0x33 Addresses Not Decoded Yet
- Total distinct FC `0x41` unknown frames in logs: 14173
- Truncated unknown frames (cannot fully parse): 9203
- Non-truncated FC `0x41` frames parsed: 4964 (req=4857, rsp=71, other=36)
- Distinct addresses in FC `0x41/0x33`: 61
- Distinct FC `0x41/0x33` addresses not in decoder: 37

| addr | words (req) | req_count | rsp_count | in_registers_py | V3 PDF mapping | classification |
|---:|---:|---:|---:|---|---|---|
| 10000 | 7 | 279 | 0 | no | - | `proprietary_or_internal_offset` |
| 10102 | 6 | 279 | 0 | no | - | `proprietary_or_internal_offset` |
| 10200 | 8 | 279 | 0 | no | - | `proprietary_or_internal_offset` |
| 10214 | 14 | 279 | 0 | no | - | `proprietary_or_internal_offset` |
| 10230 | 2 | 279 | 0 | no | - | `proprietary_or_internal_offset` |
| 10300 | 2 | 279 | 0 | no | - | `proprietary_or_internal_offset` |
| 30089 | 15 | 24 | 0 | no | Offering name (p11) | `documented_v3_pdf_not_decoded` |
| 30136 | 30 | 24 | 0 | no | Software package name (p13) | `documented_v3_pdf_not_decoded` |
| 30204 | 2 | 24 | 0 | no | - | `unmapped_unknown` |
| 30283 | 4 | 24 | 0 | no | - | `unmapped_unknown` |
| 30298 | 1 | 24 | 0 | no | - | `unmapped_unknown` |
| 30304 | 1 | 24 | 0 | no | Level-1 paramet er mask 5 (monitor ing) (p17) | `documented_v3_pdf_not_decoded` |
| 30341 | 2 | 24 | 0 | no | Level-1 paramet er mask 10 (monitor ing) (p19) | `documented_v3_pdf_not_decoded` |
| 30378 | 15 | 24 | 0 | no | - | `unmapped_unknown` |
| 30501 | 2 | 24 | 0 | no | - | `unmapped_unknown` |
| 30517 | 11 | 24 | 0 | no | - | `unmapped_unknown` |
| 31115 | 15 | 25 | 0 | yes | - | `canonical_py_not_decoded` |
| 32155 | 1 | 25 | 0 | no | Alarm clearance SN (p27) | `documented_v3_pdf_not_decoded` |
| 32300 | 24 | 25 | 0 | no | - | `unmapped_unknown` |
| 34000 | 2 | 25 | 0 | no | - | `unmapped_unknown` |
| 35094 | 1 | 25 | 0 | no | I-V curve scanning status (p35) | `documented_v3_pdf_not_decoded` |
| 35115 | 1 | 25 | 0 | no | Delayed activatio n status (p35) | `documented_v3_pdf_not_decoded` |
| 37518 | 2 | 2 | 0 | no | Inverter overall status (p37) | `documented_v3_pdf_not_decoded` |
| 37829 | 3 | 2 | 0 | no | - | `unmapped_unknown` |
| 37918 | 2 | 2 | 0 | no | - | `unmapped_unknown` |
| 37928 | 1 | 1 | 1 | no | SOH Calibra tion Enable the backup power SOC. (p81) | `documented_v3_pdf_not_decoded` |
| 40000 | 2 | 5 | 5 | yes | System time [local time] (p38) | `canonical_py_not_decoded` |
| 42017 | 2 | 71 | 1 | yes | Active power change gradient (p42) | `canonical_py_not_decoded` |
| 43037 | 1 | 25 | 0 | no | [RS485-2 ] Port mode (p67) | `documented_v3_pdf_not_decoded` |
| 43139 | 1 | 25 | 0 | no | - | `unmapped_unknown` |
| 43220 | 1 | 25 | 0 | no | - | `unmapped_unknown` |
| 43349 | 10 | 25 | 0 | no | Device name (p67) | `documented_v3_pdf_not_decoded` |
| 44001 | 1 | 25 | 0 | no | - | `unmapped_unknown` |
| 45255 | 5 | 25 | 0 | no | - | `unmapped_unknown` |
| 47002 | 1 | 6 | 0 | no | - | `unmapped_unknown` |
| 47303 | 1 | 6 | 0 | no | - | `unmapped_unknown` |
| 47781 | 1 | 648 | 0 | no | - | `unmapped_unknown` |

## 3) Observed FC 0x41 / 0x34 Addresses
- Distinct addresses in FC `0x41/0x34`: 3

| addr | words (rsp) | rsp_count | sample_raw_u16 | V3 PDF mapping | classification |
|---:|---:|---:|---:|---|---|
| 11100 | 1 | 24 | 65474 | - | `proprietary_or_internal_offset` |
| 15520 | 1 | 12 | 1 | - | `proprietary_or_internal_offset` |
| 19000 | 1 | 36 | 4 | - | `proprietary_or_internal_offset` |

## 4) Identified But Not Yet Decoded Backlog (Not On Home Page)

Recently promoted from this backlog into decoded set (2026-04-20):
- `16300` (`meter_active_power_fast`)
- `16304` component words (`grid_a_power_fast`/`grid_b_power_fast`/`grid_c_power_fast` at `16305/16307/16309`)
- `42045` (`off_grid_mode`)

Priority scale used here:
- `P1`: high implementation value now (no extra hardware dependency, likely visible impact)
- `P2`: useful but lower confidence or lower impact
- `P3`: low priority / metadata / installer details / constrained validation
- `P4`: defer until stronger evidence or better captures

Battery constraint:
- Battery-domain candidates are intentionally lowered for now because this system has no battery connected, so correctness cannot be fully verified from live behavior.

| priority | addr | words | candidate meaning | evidence snapshot | verification constraint | implementation note |
|---|---:|---:|---|---|---|---|
| `P2` | 42017 | 2 | active power change gradient | canonical + some responses (`rsp_count=1`) | none | likely `I32`; useful but lower certainty than 42045 |
| `P2` | 32155 | 1 | alarm clearance SN | documented in V3 PDF | none | status/alarm context improvement |
| `P2` | 37518 | 2 | inverter overall status | documented in V3 PDF (`req_count=2`) | none | low traffic but potentially valuable summary state |
| `P3` | 40000 | 2 | system time | canonical + responses (`rsp_count=5`) | none | intentionally low priority (not operationally critical) |
| `P3` | 30089 | 15 | offering name (string) | documented in V3 PDF | none | static metadata only |
| `P3` | 30136 | 30 | software package name (string) | documented in V3 PDF | none | static metadata only |
| `P3` | 43349 | 10 | device name (string) | documented in V3 PDF | none | low runtime value |
| `P3` | 43037 | 1 | RS485-2 port mode | documented in V3 PDF | none | installer/config detail, low immediate value |
| `P3` | 37928 | 1 | SOH calibration enable | documented in V3 PDF + one response | battery absent | defer until battery hardware is present |
| `P3` | 47002 | 1 | unmapped battery-related setting candidate | low-frequency unknown (`req_count=6`) | battery absent | keep in backlog, do not prioritize now |
| `P3` | 47303 | 1 | unmapped battery/control candidate | low-frequency unknown (`req_count=6`) | battery absent | keep in backlog, do not prioritize now |
| `P3` | 47781 | 1 | unmapped high-frequency flag candidate | high request count (`req_count=648`) but zero parsed responses in old logs | battery/system-mode context unknown | revisit with full-response daytime captures |
| `P4` | 32300/34000/45255 | mixed | unknown struct blocks | observed but semantically unclear | none | defer until scalar/enum backlog above is reduced |

## 5) Notes
- Addresses below `30000` (`10000`/`10102`/`16300`/etc.) are not in the published SUN2000 Modbus map sections in the V3.0 PDF and are treated as proprietary/internal FC `0x41` payload offsets; decoded `16300/16305/16307/16309/16312` are confirmed and exposed as `*_fast` meter-power mirrors.
- Some addresses are documented in the V3.0 PDF but absent from canonical `registers.py`; those are currently tagged `documented_v3_pdf_not_decoded`.
- This file is intended as the single working inventory for register reverse-engineering and decode backlog prioritization.

