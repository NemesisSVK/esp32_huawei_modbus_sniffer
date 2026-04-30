# Register List by Bus (Auto-generated from src/huawei_decoder.cpp)

- Generated-UTC: 2026-04-30 19:40
- Source: `src/huawei_decoder.cpp` `KNOWN_REGS`
- Classification rule:
  - `direct_meter`: F32 registers in address range 2102..2222 (DTSU666 direct FC03 map).
  - `sdongle_uplink`: all other decoded registers (SUN2000 <-> SDongle/inverter-side traffic).

## DTSU666 Direct Meter Bus (tested)

- Total decoded registers: 41
- Notes: Observed on direct SUN2000 <-> DTSU666 meter channel captures (slave 0x0B FC03 float map).

| addr | words | type | scale | unit | name | group |
|---:|---:|---|---:|---|---|---|
| 2102 | 2 | F32 | 1 | A | `grid_a_current` | `GRP_METER` |
| 2104 | 2 | F32 | 1 | A | `grid_b_current` | `GRP_METER` |
| 2106 | 2 | F32 | 1 | A | `grid_c_current` | `GRP_METER` |
| 2108 | 2 | F32 | 1 | V | `grid_a_voltage` | `GRP_METER` |
| 2110 | 2 | F32 | 1 | V | `grid_b_voltage` | `GRP_METER` |
| 2112 | 2 | F32 | 1 | V | `grid_c_voltage` | `GRP_METER` |
| 2114 | 2 | F32 | 1 | V | `equivalent_phase_voltage` | `GRP_METER` |
| 2116 | 2 | F32 | 1 | V | `grid_ab_voltage` | `GRP_METER` |
| 2118 | 2 | F32 | 1 | V | `grid_bc_voltage` | `GRP_METER` |
| 2120 | 2 | F32 | 1 | V | `grid_ca_voltage` | `GRP_METER` |
| 2122 | 2 | F32 | 1 | V | `equivalent_line_voltage` | `GRP_METER` |
| 2124 | 2 | F32 | 1 | Hz | `frequency` | `GRP_METER` |
| 2126 | 2 | F32 | 1 | W | `meter_active_power` | `GRP_METER` |
| 2128 | 2 | F32 | 1 | W | `grid_a_power` | `GRP_METER` |
| 2130 | 2 | F32 | 1 | W | `grid_b_power` | `GRP_METER` |
| 2132 | 2 | F32 | 1 | W | `grid_c_power` | `GRP_METER` |
| 2134 | 2 | F32 | 1 | var | `meter_reactive_power` | `GRP_METER` |
| 2136 | 2 | F32 | 1 | var | `grid_a_reactive_power` | `GRP_METER` |
| 2138 | 2 | F32 | 1 | var | `grid_b_reactive_power` | `GRP_METER` |
| 2140 | 2 | F32 | 1 | var | `grid_c_reactive_power` | `GRP_METER` |
| 2142 | 2 | F32 | 1 | VA | `apparent_power` | `GRP_METER` |
| 2144 | 2 | F32 | 1 | VA | `grid_a_apparent_power` | `GRP_METER` |
| 2146 | 2 | F32 | 1 | VA | `grid_b_apparent_power` | `GRP_METER` |
| 2148 | 2 | F32 | 1 | VA | `grid_c_apparent_power` | `GRP_METER` |
| 2150 | 2 | F32 | 1 | - | `meter_power_factor` | `GRP_METER` |
| 2152 | 2 | F32 | 1 | - | `grid_a_power_factor` | `GRP_METER` |
| 2154 | 2 | F32 | 1 | - | `grid_b_power_factor` | `GRP_METER` |
| 2156 | 2 | F32 | 1 | - | `grid_c_power_factor` | `GRP_METER` |
| 2158 | 2 | F32 | 1 | kWh | `net_active_energy_total` | `GRP_METER` |
| 2160 | 2 | F32 | 1 | kWh | `net_active_energy_a` | `GRP_METER` |
| 2162 | 2 | F32 | 1 | kWh | `net_active_energy_b` | `GRP_METER` |
| 2164 | 2 | F32 | 1 | kWh | `net_active_energy_c` | `GRP_METER` |
| 2166 | 2 | F32 | 1 | kWh | `imported_energy_total` | `GRP_METER` |
| 2168 | 2 | F32 | 1 | kWh | `imported_energy_a_total` | `GRP_METER` |
| 2170 | 2 | F32 | 1 | kWh | `imported_energy_b_total` | `GRP_METER` |
| 2172 | 2 | F32 | 1 | kWh | `imported_energy_c_total` | `GRP_METER` |
| 2174 | 2 | F32 | 1 | kWh | `exported_energy_total` | `GRP_METER` |
| 2176 | 2 | F32 | 1 | kWh | `exported_energy_a_total` | `GRP_METER` |
| 2178 | 2 | F32 | 1 | kWh | `exported_energy_b_total` | `GRP_METER` |
| 2180 | 2 | F32 | 1 | kWh | `exported_energy_c_total` | `GRP_METER` |
| 2222 | 2 | F32 | 1 | kvarh | `reactive_energy_total` | `GRP_METER` |
