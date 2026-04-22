# HISTORY.md
Last-Updated-UTC: 2026-04-22 20:40

## Purpose
Cold-context project evolution log. Keep milestones concise and action-relevant.

## Milestones
H-20260416-001 | AI handoff baseline created for modbus sniffer project | Range: 2026-04-16 | Notes: initial memory system and guardrails established
H-20260416-002 | Supporting references imported under `_supporting-projects/` | Range: 2026-04-16 | Notes: includes prior HA manager project and Huawei Solar integration/library sources for decode parity checks
H-20260416-003 | Memory validation automation added | Range: 2026-04-16 | Notes: introduced `scripts/validate_ai_memory.ps1` and made it part of required memory workflow
H-20260416-004 | MCP memory augmentation baseline added | Range: 2026-04-16 | Notes: seeded durable MCP graph for project invariants/contracts and formalized hybrid source-of-truth policy
H-20260416-005 | MCP sync-state automation gate added | Range: 2026-04-16 | Notes: introduced `scripts/update_mcp_sync_state.ps1` and validator fingerprint check against `MCP_MEMORY_SYNC.json`
H-20260417-001 | Modbus register intelligence baseline established | Range: 2026-04-17 | Notes: cross-referenced sniffer `KNOWN_REGS`/`KNOWN_BLOCKS` against upstream Huawei register catalogs and persisted durable decoding context for future register extraction work
H-20260417-002 | CodeDate release metadata system adopted | Range: 2026-04-17 | Notes: added `src/CodeDate.h`, build-time CODE_DATE discipline checks, and monitoring/API exposure of code date + build timestamp
H-20260417-003 | Proprietary Huawei FC `0x41` passive decode path implemented | Range: 2026-04-17 | Notes: added dedicated list-frame request/response decode + correlation in `huawei_decoder` and expanded canonical register coverage for off-integration traffic
H-20260417-004 | Dedicated cumulative register catalog introduced | Range: 2026-04-17 | Notes: added `REGISTER_CATALOG.md` with full decoded table and observed-not-decoded FC `0x41` address intelligence merged from logs, canonical refs, and Huawei V3 PDF
H-20260417-005 | Raw frame dump switched to targeted full FC `0x41` unknown capture | Range: 2026-04-17 | Notes: removed 32-byte truncation for this path, added chunked reconstructable log output, and filtered out normal Modbus traffic noise
H-20260418-001 | Dedicated Wi-Fi raw stream export added for FC `0x41` capture | Range: 2026-04-18 | Notes: introduced PSRAM-queued TCP exporter, settings/UI + monitoring counters, and collector script for long-running offline analysis captures
H-20260420-001 | Multi-hour FC `0x41` capture analysis produced first high-confidence proprietary power mapping | Range: 2026-04-20 | Notes: established `16300` aggregate-power relation to component words within `16304`, confirmed production->no-production transition behavior, and separated packed composite reads from truly unknown semantics
H-20260420-002 | Dashboard/source classification upgraded to preserve duplicate register names by origin | Range: 2026-04-20 | Notes: decode callback now tracks source id; cached/API values stay distinct per source/slave and homepage now labels each row with source + slave badges
H-20260420-003 | Home-page refresh metrics fixed for sub-second update streams | Range: 2026-04-20 | Notes: interval metrics now flow in milliseconds (`min_ms/avg_ms/max_ms`) so fast `0x41` rows no longer disappear due to `0s` rounding
H-20260420-004 | Home-page status bar streamlined for reverse-engineering workflow | Range: 2026-04-20 | Notes: removed general device/network clutter and added concise frame progress, poll cadence, and source/tag legend hints
H-20260420-005 | Register catalog aligned with live Home-page categories | Range: 2026-04-20 | Notes: added explicit `GRP_*` -> Home `mqtt_subtopic` mapping table and source-tag legend to keep decode backlog tracking consistent with dashboard group buckets
H-20260420-006 | Register catalog gained prioritized not-decoded implementation backlog | Range: 2026-04-20 | Notes: added P1-P4 quick triage chapter with practical constraints (non-battery first, battery-limited verification, low-priority system-time/metadata items)
H-20260420-007 | First high-priority reverse-engineered register batch promoted to decoded | Range: 2026-04-20 | Notes: implemented `16300/16304` inferred FC `0x41` power signals (`h41_active_power_*`) and documented `42045` off-grid mode in runtime decoder/catalog
H-20260420-008 | Provisional reverse-engineered values received explicit NEW badge classification | Range: 2026-04-20 | Notes: added API value-tag marker and Home-page badge/legend to distinguish fresh inference-based registers from confirmed decode sets
H-20260421-001 | Raw capture moved to profile-based mode for repeatable reverse-engineering comparisons | Range: 2026-04-21 | Notes: added capture profiles (`unknown_h41`, `compare_power`, `research_inverter_phase`) and FC03/FC04 request-response capture correlation alongside FC0x41 unknown traffic
H-20260421-002 | Confirmed FC0x41 power mirrors promoted to final meter-fast classification | Range: 2026-04-21 | Notes: reassigned `16300/16305/16307/16309` into `GRP_METER`, renamed to `meter_active_power_fast` + `grid_a/b/c_power_fast`, and removed provisional NEW-tag status for these four signals
H-20260421-003 | Research inverter-phase capture broadened to all unknown function-code families | Range: 2026-04-21 | Notes: `research_inverter_phase` now captures all unknown frames plus inverter-production FC03/FC04 context (`32016–32095`, `42056`)
H-20260422-001 | Added provisional FC0x41 raw telemetry channel for operator correlation | Range: 2026-04-22 | Notes: promoted `16312` into runtime decode as `h41_16312_raw` (`GRP_METER`) with explicit NEW-tag status pending semantic confirmation
H-20260422-002 | Promoted FC0x41 `16312` to confirmed fast reactive power mirror | Range: 2026-04-22 | Notes: renamed runtime signal to `meter_reactive_power_fast` (`var`), removed provisional NEW badge, and aligned register catalog as confirmed
H-20260422-003 | Added global all-frames capture mode and reconnect-hardening telemetry | Range: 2026-04-22 | Notes: introduced `all_frames` capture profile, collector stale-session timeout recovery, and monitoring visibility for stream reconnect/failure counters
H-20260422-004 | Added register-provenance badges and raw watchlist sensors for live reverse-engineering | Range: 2026-04-22 | Notes: decoder->API now carries source register addresses (`reg/reg_end`), Home rows show `Rxxxx` badges, and selected moving unmapped addresses are exposed as `h41_*_raw` metrics
H-20260422-005 | Introduced Live Modbus state page with unknown-register visibility and delta updates | Range: 2026-04-22 | Notes: added `/live` 4-column state UI + `/api/live_values` cursor endpoint; decoder now feeds unknown FC03/FC04/H41 register words into dedicated SPIRAM live cache for reverse-engineering
H-20260422-006 | Separated Live Modbus cache layer from MQTT publisher internals | Range: 2026-04-22 | Notes: added `LiveValueStore` module, rewired decoder output fanout in `main.cpp`, and removed live-page storage/API coupling from `mqtt_publisher`

## Source anchors
- Project runtime/docs source: this repository root (`src/`, `README.md`, build/config scripts)
- External/reference context: `_supporting-projects/*`

## Maintenance note
- Add milestone entries for major architecture/contract/workflow shifts, not for every minor edit.
