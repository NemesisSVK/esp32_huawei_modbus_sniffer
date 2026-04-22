# AI_CONTEXT.md
Last-Updated-UTC: 2026-04-22 20:40
Project: ESP32 Huawei Inverter Modbus Sniffer

## Purpose
High-signal startup context for multi-session, multi-machine AI work on this firmware.

## Session Start Checklist
1. Read `AGENTS.md` first.
2. Read `TASKS.md` to identify active priorities.
3. Read latest `DECISIONS.md` entries for non-negotiable constraints.
4. Read latest `SESSION_LOG.md` entry for most recent change context.
5. Use `HISTORY.md` for deeper background/milestones.

## Mission
Passively sniff and decode Modbus RTU traffic (Huawei SUN2000 <-> meter path), map values to known register groups, and publish stable MQTT telemetry without transmitting on the RS-485 bus.

## Runtime Snapshot
- Platform: ESP32-S3 N16R8 (`esp32-s3-devkitc-1-n16r8v`)
- Default env: `esp32-s3-n16r8v`
- OTA env: `esp32-s3-n16r8v-ota`
- Build scripts: `build_validate.py`, `ota_flags.py`, `post_upload.py`
- Filesystem: LittleFS

## Architecture Pillars
- Modbus capture/parsing: `src/modbus_rtu.*`
- Decode + group routing: `src/huawei_decoder.*`, `src/reg_groups.h`
- Publish pipeline: `src/mqtt_publisher.*`, `src/MQTTManager.*`, `src/AsyncMqttTransport.*`
- Live Modbus state store: `src/LiveValueStore.*`
- Runtime orchestration: `src/main.cpp`
- Web/API + group controls: `src/web_ui.*`
- Configuration persistence + schema mapping: `src/ConfigManager.*`
- OTA management: `src/OTAManager.*`

## Contract-Critical Behaviors
- Passive sniff safety by default (no active RS-485 transmit behavior unless explicitly requested).
- API/web endpoints should remain stable unless intentionally changed.
- Group enable/tier behavior is configuration-driven and must stay deterministic.
- Password fields are not exposed in API settings output.
- Firmware release identity uses `src/CodeDate.h` (`CODE_DATE` + newest-first Change log), validated during build.
- `debug.raw_frame_dump` is a profile-driven reverse-engineering mode controlled by `debug.raw_capture_profile` (`unknown_h41`, `compare_power`, `research_inverter_phase`, `all_frames`).
- `research_inverter_phase` currently captures all `FRAME_UNKNOWN` families plus FC03/FC04 inverter-production windows (`32016–32095`, `42056`) for phase-oriented reverse-engineering.
- `all_frames` profile captures every parsed frame (`REQ`, `RSP`, `EXC`, `UNKNOWN`) without filtering; use stream-only export for long sessions.
- High-volume raw capture can use dedicated TCP stream export (`raw_stream.*` settings) with PSRAM queueing; collector reference script is `scripts/raw_stream_collector.py`.
- Collector reference script supports stale-session recovery via socket recv timeout + idle timeout (`--conn-timeout`, `--idle-timeout`) to survive router/Wi-Fi disruptions.
- Dashboard/API value cache is source-aware (`FC03`, `FC04`, `H41-33`, `H41-X`) and slave-aware; duplicate register names from different sources/slaves are intentionally preserved for diagnostics.
- Decoder/publisher now propagate source register metadata (`reg_addr`, `reg_words`) into `/api/values`; Home page renders `Rxxxx` / `Rxxxx-yyyy` badges per row for direct provenance checks.
- Live Modbus page (`/live`) is state-based (no rolling log): rows are keyed by source+slave+register and updated in place.
- Live state transport uses `/api/live_values?since=<seq>` delta cursor polling (3s cadence expected), with unknown decoded register candidates included.
- Live state persistence is owned by `LiveValueStore` (separate from `mqtt_publisher`); runtime fanout in `main.cpp` publishes decoded known values to both modules.
- Unknown register extraction is performed in decoder paths for FC03/FC04 response words and H41 list-frame entries not covered by `KNOWN_REGS`.
- Newly inferred reverse-engineered registers can carry an additional provisional value tag (`NEW`) in `/api/values`; Home page renders this as a separate badge alongside source/slave labels.
- Refresh interval telemetry is millisecond-native in `/api/values` (`min_ms/avg_ms/max_ms`) with legacy rounded `*_s` compatibility fields.
- Home page top status bar is intentionally minimal (frames + polling cadence + tag/source legend); broader device/mqtt/network telemetry belongs to Monitoring page.
- Monitoring page includes raw stream reconnect diagnostics (`raw_stream_reconnects`, `raw_stream_failed_connects`) for reconnect triage.

## Build/Validation Matrix
- Firmware/runtime changes: `pio run`
- OTA-path changes: `pio run -e esp32-s3-n16r8v-ota`
- Settings/schema changes: validate via build path and synchronize `config.json.example`
- Code-date discipline: `build_validate.py` validates `src/CodeDate.h` format and newest-entry parity with `CODE_DATE`
- MCP sync-state refresh: `powershell -ExecutionPolicy Bypass -File scripts/update_mcp_sync_state.ps1 -SessionId <latest S-id>`
- Memory consistency check: `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1`

## Sensitive Files
- `config.json`, `data/config.json`, `ota.json`, `data/ota.json`

## Supporting References
- `_supporting-projects/huawei-solar-lib-3.0.0`
- `_supporting-projects/huawei_solar-2.0.0b5`
- `_supporting-projects/huawei_solar-1.6.0`
- `_supporting-projects/ESP32 Temp HomeAssistant Manager`

## Modbus Register Intelligence (Baseline)
- Dedicated cumulative catalog file: `REGISTER_CATALOG.md` (single working inventory for decoded + observed-not-decoded addresses).
- `REGISTER_CATALOG.md` now includes Home-page category mapping (section `0.2`: `GRP_*` -> `mqtt_subtopic`/UI label) and source-tag legend (`DOC`/`REV`/`UNK`) to keep analysis aligned with live dashboard buckets.
- `REGISTER_CATALOG.md` now also includes an explicit prioritized not-decoded backlog chapter (section `4`) with P1-P4 ranking and verification constraints; current policy favors non-battery operational fields and keeps battery-only/time/metadata fields lower.
- Confirmed FC `0x41` power mirrors are now exposed as meter-fast fields in `GRP_METER`: `meter_active_power_fast` (`16300`) and per-phase `grid_a/b/c_power_fast` (`16305/16307/16309`); `off_grid_mode` (`42045`) remains provisional.
- Sniffer decode table (`src/huawei_decoder.cpp`) currently defines 277 registers across 12 groups (`GRP_METER`, inverter AC/status/energy/info, PV strings, battery aggregate/unit/pack/settings, SDongle), including reverse-engineering watchlist sensors prefixed `h41_*_raw`.
- Canonical-address additions on 2026-04-17 include: `30110`, `30207`, `37926`, `37927`, `42056`, `47000`, `47089`, `47100`, `47242`.
- FC `0x41` observed-not-decoded baseline in `REGISTER_CATALOG.md`:
  - subcommand `0x33`: 40 distinct addresses not in `KNOWN_REGS` (with request frequency + V3-PDF mapping tags).
  - subcommand `0x34`: observed addresses `11100`, `15520`, `19000` (response-only in current captures).
- Long-capture + compare-power baseline (`captures/raw-stream1.bin/.ndjson`, `captures/raw-stream.bin`):
  - 202,329 frames over ~5.90h with continuous sequence (no gaps),
  - production -> no-production transition visible around capture offset `+05:05..+05:40`,
  - confirmed proprietary mapping: `16300/2` mirrors `meter_active_power`, and `16305/16307/16309` mirror `grid_a/b/c_power` with expected lag/cadence differences vs FC03.
- Composite FC `0x41/0x33` packed reads are common and often include known contiguous registers:
  - examples: `32080/4`, `37758/3`, `47081/2`, `47086/2`, `37119/4`, `32091/4`.
  - treat these as list-framed packed transport reads, not automatically as brand-new semantics.
- Upstream register map has much wider coverage (hundreds of additional addresses), so current sniffer decode is a focused subset, not full Map0 coverage.
- Cold-start fallback currently uses 18 register-block signatures in `KNOWN_BLOCKS`, including common traffic blocks:
  - `32000/1` (status seed), `32016/4` and `32016/48` (PV strings),
  - `32064/24` and `32064/32` (inverter AC),
  - `32106/10` and `32106/14` (energy),
  - `37113/25` and `37100/39` (meter),
  - battery blocks (`37760/26`, `37000/69`), plus info/optimizer triggers.
- Decode type contract in sniffer:
  - numeric: `U16`, `I16`, `U32`, `I32`, `I32ABS`
  - `STRING` entries are used for group detection only (not published as values).
- Proprietary Huawei FC path:
  - CRC-valid `0x41` list-framed traffic is decoded in a dedicated passive path inside `huawei_decoder`.
  - Request format: `count + [addr(2), words(1)] * N`.
  - Response format: `count + [addr(2), words(1), data(words*2)] * N`.
  - Decoder keeps FC03/FC04 classification untouched and uses lightweight hash-based request/response matching for `0x41`.

## Cross-Project Decode Context
- Home Assistant integration (`_supporting-projects/huawei_solar-1.6.0/sensor.py`) groups entities by register-name collections and drives reads through `batch_update` contexts.
- SUN2000 device layer (`_supporting-projects/huawei-solar-lib-3.0.0/src/huawei_solar/device/sun2000.py`) dynamically computes PV register coverage from `NB_PV_STRINGS` and has meter-offline filtering behavior for meter register reads.
- Canonical register-name/address definitions are in `_supporting-projects/huawei-solar-lib-3.0.0/src/huawei_solar/registers.py`; treat this as source of truth for parity work.

## MCP Usage
- `mcp__memory__` may be used as a secondary durable cache for stable project facts and decisions.
- Source-of-truth priority remains repository handoff files over MCP memory.
- `mcp__sequentialthinking__` is optional and intended for high-complexity debugging/design analysis only.
- `MCP_MEMORY_SYNC.json` tracks handoff fingerprint and latest MCP sync checkpoint.

## Known Unknowns
- Exact current decode coverage vs Huawei Map0 expectations still needs targeted validation.
- Additional `0x41` address ranges remain unclassified/proprietary (not present in canonical `registers.py`) and need deeper reverse-engineering with full non-truncated captures.
- Several frequently polled `0x41/0x33` addresses remain undocumented by public map files (`10000` block, `32300`, `34000`, `43139`, `43220`, `44001`, `45255`, `47303`, `47781`).
- `16300`/`16304` power-component mapping is now confirmed through compare-power captures; keep monitoring for edge cases but treat these as stable decode paths.
