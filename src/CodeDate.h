#ifndef CODE_DATE_H
#define CODE_DATE_H

// Update this only when you make code changes (not when flashing/building).
// Format: YYYYMMDD-HHMM (24 hour, UTC)
//
// Change log (newest first):
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
#define CODE_DATE "20260422-2040"

#endif // CODE_DATE_H
