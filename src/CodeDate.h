#ifndef CODE_DATE_H
#define CODE_DATE_H

// Update this only when you make code changes (not when flashing/building).
// Format: YYYYMMDD-HHMM (24 hour, UTC)
//
// Change log (newest first):
// - 20260428-2007 UTC: Normalize Connectivity card typography by removing MQTT broker/client inline font downscaling so MQTT and WiFi rows render at the same size.
// - 20260428-1954 UTC: Unify Monitoring typography with shared page styles by switching rows to common stat-* classes and removing card-local font-size overrides.
// - 20260428-1947 UTC: Refine Monitoring Connectivity card with in-card MQTT subsection header and spacing for clearer WiFi vs MQTT grouping.
// - 20260428-1938 UTC: Reorganize Monitoring layout by merging WiFi+MQTT status into one Connectivity card and moving MQTT Diagnostics card into the first row.
// - 20260428-1929 UTC: Add MQTT diagnostics telemetry to monitoring API/UI (connection/publish counters, last-event ages, and per-group availability watchdog transition stats) for faster blip/root-cause triage.
// - 20260428-1907 UTC: Apply global MQTT availability stale-time floor (120s) across all groups while retaining cross-core race guards to prevent false offline blips.
// - 20260428-1858 UTC: Harden MQTT availability watchdog against cross-core races and enforce strict 120s timeout for manual priority group before publishing offline.
// - 20260427-0742 UTC: Harden runtime settings-save path with transactional apply, strict type checks, and range/format validation (ports, Modbus slave, pins, auth prerequisites, IP whitelist, raw stream, publish tiers/manual selectors) before persisting.
// - 20260426-2157 UTC: Add always-on MQTT diagnostics publish path (HA discovery + periodic cpu temp/free heap/LittleFS free-percent state topics with reconnect snapshot).
// - 20260426-2132 UTC: Fix Monitoring memory metrics to use ESP heap/psram APIs (internal heap + largest block), move temperature to System Details as Chip Temp, and remove redundant Target/Build rows.
// - 20260426-2101 UTC: Reorganize Monitoring page into HA-manager-style grouped cards (WiFi, MQTT, Sniffer, Memory Usage, System Details, LittleFS) with 15s polling and expanded monitoring API payload fields.
// - 20260426-1858 UTC: Manual priority-group picker now lists runtime-observed selectors first, inserts a catalog-only separator row, and shows runtime vs catalog selector counts in hint text.
// - 20260426-1843 UTC: Remove deprecated config_store shim files and retire legacy /api/register_names fallback path in Settings (catalog-only register picker API).
// - 20260426-1831 UTC: Remove legacy raw capture profiles `compare_power` and `research_inverter_phase`; keep only `unknown_h41` and `all_frames` across decoder filtering, settings UI, and config validation.
// - 20260426-1819 UTC: Streamline Settings layout by consolidating WiFi/Network/Security, MQTT/Device Info, and RS485/Pins into grouped cards with clear subsection headers.
// - 20260426-1806 UTC: Add client-side export beautifier fallback so Settings export always downloads formatted JSON even if backend response is minified.
// - 20260426-1107 UTC: Make config export beautifier deterministic from `ConfigManager` (stable key order, pretty JSON, CRLF line endings, trailing newline) to match project-style formatting.
// - 20260426-1019 UTC: Streamline Settings config backup by removing JSON import UI flow and exporting pretty-printed `config.json` from `/api/config/export`.
// - 20260426-0940 UTC: Streamline manual-group selector picker to show only observed-real source options per register (with sane fc03/h41 defaults when unseen), reducing duplicate impossible source choices.
// - 20260426-0900 UTC: Make manual priority-group selectors source-aware end-to-end (UI picker emits `source:register`, frontend validation is source/register aware, and backend error text reflects selector format).
// - 20260425-2201 UTC: Add group-aware register metadata API and show `group :: register` labels in manual priority-group picker while saving canonical register names.
// - 20260425-2151 UTC: Remove manual-group legacy alias normalization, enforce strict inverter-prefixed register names, and align docs/catalog examples with the new naming.
// - 20260425-1215 UTC: Prefix inverter collision registers (`inverter_active_power[_fast]`, `inverter_reactive_power`, `inverter_power_factor`) and migrate manual-group legacy aliases.
// - 20260425-1209 UTC: Normalize telemetry naming (group-aware cleanup for meter/battery/inverter labels) while keeping collision-prone meter power names unchanged.
// - 20260425-1152 UTC: Force HA default entity IDs to `sensor.clientid_group_register` and include natural group in priority naming (`priority_<group>_<register>`).
// - 20260425-1142 UTC: Revise HA discovery naming to `clientid_group_register` IDs and `group_register` names for clear per-group entity separation in Home Assistant.
// - 20260425-1133 UTC: Change manual priority-group Home Assistant entity key format from suffix to prefix (`priority_<register>`).
// - 20260425-1102 UTC: Add dedicated Priority Monitor page (/priority) with 1s refresh and new /api/priority_values endpoint for manual priority-group live tracking.
// - 20260425-1015 UTC: Make manual priority-group Home Assistant entities distinct by publishing discovery IDs with `_priority` suffix.
// - 20260425-0942 UTC: Add configurable manual priority publish group (dedicated topic, tier selection, register-name validation, and Settings UI picker).
// - 20260425-0900 UTC: Streamline Settings group configuration by showing only groups with active decode mappings and ordering them by operational priority.
// - 20260425-0848 UTC: Reduce MQTT telemetry noise by pruning low-value decode registers (inverter info, deep battery/unit/pack/settings, SDongle summary fields, and verbose inverter status alarms/history).
// - 20260424-1001 UTC: Finalize direct-meter FC03 inferred voltage and cumulative split counters by removing NEW-tag treatment for this modbus channel.
// - 20260424-0948 UTC: Rename provisional DTSU FC03 2114/2122 signals to meter_equiv_phase_voltage and meter_equiv_line_voltage while keeping NEW-tag validation status.
// - 20260424-0930 UTC: Add provisional direct-meter cumulative energy split counters (2158/2160/2162/2164/2168/2170/2172/2176/2178/2180) and mark them NEW for live validation.
// - 20260424-0857 UTC: Confirm DTSU direct-channel energy totals by mapping FC03 float registers 2166/2174/2222 to imported/exported/reactive energy totals.
// - 20260424-0821 UTC: Add provisional direct-meter telemetry decodes for FC03 float registers 2114/2122/2222 and mark them as NEW-tagged candidates on Home/Live.
// - 20260424-0736 UTC: Add direct DTSU666-H FC03 float decode support (F32 type, registers 2102-2156, and fallback blocks for direct meter-channel traffic).
// - 20260423-0847 UTC: Remove inverter/battery watchlist `h41_*_raw` KNOWN_REGS entries so unknown raw reverse-engineering values no longer appear on the Home page.
// - 20260422-2040 UTC: Separate Live Modbus state storage into dedicated LiveValueStore module and wire decoder output fanout in main, removing live-cache coupling from mqtt_publisher.
// - 20260422-1948 UTC: Add Live Modbus page with 4-column in-place state view and delta API, plus decoder-side unknown register extraction for FC03/FC04 and H41 list frames.
// - 20260422-1912 UTC: Add raw watchlist decodes for selected moving unmapped registers, propagate register address metadata through decoder->cache->API, and render source register badges (Rxxxx[/Rxxxx-yyyy]) on Home tiles.
// - 20260422-1131 UTC: Add raw capture profile all_frames (no filter), extend monitoring with raw-stream reconnect diagnostics, and harden collector against stale TCP sessions via timeouts.
// - 20260422-1040 UTC: Promote FC0x41 addr 16312 to confirmed meter_reactive_power_fast in Grid Meter group and remove NEW provisional tag for this signal.
// - 20260422-0959 UTC: Add provisional FC0x41 raw telemetry register h41_16312_raw (addr 16312) in Grid Meter group and mark it as NEW for homepage monitoring/correlation.
// - 20260421-1443 UTC: Broaden research_inverter_phase capture profile to include all UNKNOWN frames plus inverter production FC03/FC04 ranges (32016-32095, 42056).
// - 20260421-1425 UTC: Promote confirmed FC0x41 power mirrors into Grid Meter group with final *_fast naming and remove provisional NEW tag for those four registers.
// - 20260421-0735 UTC: Add profile-based raw capture mode (unknown_h41/compare_power/research_inverter_phase) with UI selector and FC03/FC04 request-response filtering for comparison captures.
// - 20260420-1956 UTC: Add dedicated NEW value tag for unconfirmed reverse-engineered registers and render it on Home page badges/legend.
// - 20260420-1951 UTC: Reverse-engineer and decode P1 registers 16300/16304 (H41 inferred active power channels) and 42045 (off_grid_mode), and update register catalog priorities/status.
// - 20260420-1918 UTC: Simplify Home page status bar to frames + refresh cadence and add inline tag/source legend for DOC/REV/UNK and FC03/FC04/H41 meanings.
// - 20260420-1906 UTC: Fix home-page refresh metrics visibility for fast-updating values by exposing min/avg/max in milliseconds and rendering sub-second intervals.
// - 20260420-1847 UTC: Keep per-source/per-slave decoded cache entries for identical registers and show source + slave badges on the homepage register list.
// - 20260418-1912 UTC: Add raw frame TCP streaming exporter (PSRAM queue + collector settings/UI + monitoring counters) and include a Python collector script for long Wi-Fi captures.
// - 20260417-2155 UTC: Repurpose raw_frame_dump to capture full unknown Huawei FC 0x41 frames only (chunked output for reconstruction) and update debug help text.
// - 20260417-2038 UTC: Add passive decode support for Huawei FC 0x41 list-framed traffic (request/response correlation + entry decode), and extend KNOWN_REGS with observed canonical addresses from off-integration logs.
// - 20260417-2004 UTC: Introduce HA-manager-style CodeDate discipline for the sniffer project (new CodeDate.h, build-time validation in build_validate.py, and monitoring API/UI exposure of CODE_DATE and BUILD_TIMESTAMP).
#define CODE_DATE "20260428-2007"

#endif // CODE_DATE_H
