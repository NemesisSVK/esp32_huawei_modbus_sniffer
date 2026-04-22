# SESSION_LOG.md
Last-Updated-UTC: 2026-04-22 20:41

## Purpose
Hot-context chronological handoff log for recent sessions. Keep entries concise and operational.

## Entries
S-20260416-001 | UTC: 2026-04-16 20:43
Goal: Bootstrap the AI handoff system for this newly imported project.
Changes:
- Created `AGENTS.md` with project-specific workflow/guardrails and memory-maintenance rules.
- Created `AI_CONTEXT.md` with startup checklist, architecture map, build matrix, and sensitive-file map.
- Created `TASKS.md` with initial Kanban-style backlog and bootstrap completion item.
- Created `DECISIONS.md` with baseline durable decisions (memory system, passive-sniffer default, supporting-projects scope).
- Created `HISTORY.md` with initial milestone anchors.
Verification:
- Reviewed project structure and key files (`README.md`, `platformio.ini`, main runtime/decoder/config/web/mqtt/ota modules, validation scripts).
- Verified supporting references are present under `_supporting-projects/`.
Open Issues:
- `SESSION_LOG.md` file had not yet been committed in the initial write pass.
- No memory-validator script exists yet for this repository.
Next Action:
- Backfill missing session log file and complete memory consistency check.

S-20260416-002 | UTC: 2026-04-16 20:45
Goal: Backfill missing hot-context log file and close bootstrap consistency gap.
Changes:
- Added `SESSION_LOG.md` with structured UTC entries.
- Re-verified presence/content consistency for `AGENTS.md`, `AI_CONTEXT.md`, `TASKS.md`, `DECISIONS.md`, and `HISTORY.md`.
Verification:
- Directory/file check in repo root confirmed all six handoff files exist.
- Content check confirmed task board and decision/history references are aligned with bootstrap state.
Open Issues:
- Backlog tasks remain unstarted (`T-20260416-002`..`T-20260416-005`), especially decode-coverage and passive-safety validation.
Next Action:
- Begin `T-20260416-003` (passive safety path audit) or `T-20260416-004` (Map0 decode coverage validation) based on user priority.

S-20260416-003 | UTC: 2026-04-16 20:50
Goal: Implement automated AI memory consistency validation for this repository.
Linked Tasks: T-20260416-005
Linked Decisions: D-20260416-004
Linked History: H-20260416-003
Changes:
- Added `scripts/validate_ai_memory.ps1` (required files, timestamp format, ID duplication, optional linked ID, and freshness checks).
- Updated `AGENTS.md` to require running validator after memory updates.
- Updated `AI_CONTEXT.md` build/validation matrix with validator command.
- Updated task/decision/history records to capture this workflow policy change.
Verification:
- Ran `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1` (result captured in current session).
Open Issues:
- `T-20260416-002`, `T-20260416-003`, and `T-20260416-004` remain open.
Next Action:
- Start `T-20260416-003` (passive sniff safety audit) unless user reprioritizes.

S-20260416-004 | UTC: 2026-04-16 21:00
Goal: Populate MCP memory and codify MCP server usage rules for ongoing agent work.
Linked Tasks: T-20260416-006
Linked Decisions: D-20260416-005
Linked History: H-20260416-004
Changes:
- Seeded `mcp__memory__` knowledge graph with durable project entities/relations (mission, contracts, build matrix, validator policy, sensitive file policy, known unknowns, and accepted decisions).
- Updated `AGENTS.md` with explicit MCP augmentation policy (secondary cache model, no secrets/transient data, optional sequential thinking).
- Updated `AI_CONTEXT.md` with MCP usage guidance and source-of-truth priority.
- Updated task/decision/history records for this policy shift.
Verification:
- `mcp__memory__` accepted entity and relation creation requests without errors.
- Ran `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1`.
Open Issues:
- `T-20260416-002`, `T-20260416-003`, and `T-20260416-004` remain open.
Next Action:
- Start `T-20260416-003` passive safety path audit.

S-20260416-005 | UTC: 2026-04-16 21:05
Goal: Add an automatic MCP sync-state gate to reduce drift between handoff files and MCP memory usage.
Linked Tasks: T-20260416-007
Linked Decisions: D-20260416-006
Linked History: H-20260416-005
Changes:
- Added `scripts/update_mcp_sync_state.ps1` to generate `MCP_MEMORY_SYNC.json` with UTC checkpoint and handoff fingerprint.
- Extended `scripts/validate_ai_memory.ps1` to require `MCP_MEMORY_SYNC.json` and fail on fingerprint mismatch.
- Updated `AGENTS.md` and `AI_CONTEXT.md` with required sync-state workflow commands.
Verification:
- Ran `powershell -ExecutionPolicy Bypass -File scripts/update_mcp_sync_state.ps1 -SessionId S-20260416-005`.
- Ran `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1`.
Open Issues:
- `T-20260416-002`, `T-20260416-003`, and `T-20260416-004` remain open.
Next Action:
- Start `T-20260416-003` passive safety path audit.

S-20260416-006 | UTC: 2026-04-16 21:06
Goal: Fix validator parsing bug and complete memory validation gate.
Linked Tasks: T-20260416-007
Changes:
- Fixed PowerShell interpolation bug in `scripts/validate_ai_memory.ps1` (`"${p}:$hash"`).
- Added this session checkpoint to satisfy freshness requirement after validator-script update.
Verification:
- Ran `powershell -ExecutionPolicy Bypass -File scripts/update_mcp_sync_state.ps1 -SessionId S-20260416-006`.
- Ran `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1`.
Open Issues:
- `T-20260416-002`, `T-20260416-003`, and `T-20260416-004` remain open.
Next Action:
- Start `T-20260416-003` passive safety path audit.

S-20260417-001 | UTC: 2026-04-17 20:00
Goal: Build a durable Modbus register-intelligence memory baseline for ongoing sniffer register identification work.
Linked Tasks: T-20260417-001
Linked Decisions: D-20260417-001, D-20260417-002
Linked History: H-20260417-001
Changes:
- Analyzed local decoder register tables and fallback signatures in `src/huawei_decoder.cpp` (`KNOWN_REGS`, `KNOWN_BLOCKS`) and group mapping in `src/reg_groups.h`.
- Cross-checked register coverage against canonical upstream source `_supporting-projects/huawei-solar-lib-3.0.0/src/huawei_solar/registers.py`.
- Confirmed implemented sniffer set currently covers 239 register addresses across 12 groups, all present in upstream canonical mapping.
- Captured cross-project behavior context from `_supporting-projects/huawei_solar-1.6.0/sensor.py` and `_supporting-projects/huawei-solar-lib-3.0.0/src/huawei_solar/device/sun2000.py` (batch-update model, dynamic PV-register computation, meter-offline filtering behavior).
- Updated `AGENTS.md` and `AI_CONTEXT.md` with register crosswalk guardrails and protocol known-unknowns (CRC-valid non-FC03/FC04 traffic remains explicit unknown path).
- Updated task board, decisions, and milestones for this rollout.
Verification:
- Collected coverage/fallback statistics using PowerShell regex extraction against `huawei_decoder.cpp` and `registers.py`.
- Ran `powershell -ExecutionPolicy Bypass -File scripts/update_mcp_sync_state.ps1 -SessionId S-20260417-001 -Notes "Register intelligence memory baseline"`.
- Ran `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1` (first parallel attempt failed due sync/validate race; second sequential run passed).
Open Issues:
- CRC-valid `FRAME_UNKNOWN` traffic (e.g. non-FC03/FC04 function-code family) still needs protocol classification before decode expansion.
- Upstream canonical register map remains much larger than current sniffer implementation (expansion prioritization pending).
Next Action:
- Seed MCP memory with durable register intelligence entities/relations, then run sync-state update + validator.

S-20260417-002 | UTC: 2026-04-17 20:08
Goal: Implement HA-manager-style `CodeDate.h` release metadata and validation workflow in the sniffer project.
Linked Tasks: T-20260417-004
Linked Decisions: D-20260417-003
Linked History: H-20260417-002
Changes:
- Added `src/CodeDate.h` with `CODE_DATE` (`YYYYMMDD-HHMM`, UTC) and newest-first concise Change log bullets.
- Extended `build_validate.py` with `validate_code_date()` to enforce:
  - `CODE_DATE` format,
  - presence of Change log block,
  - newest Change log bullet timestamp parity with `CODE_DATE`,
  - non-empty newest description.
- Hardened CodeDate validation gate to fail build when `src/CodeDate.h` is missing (no skip/warn fallback).
- Wired `CODE_DATE` and `BUILD_TIMESTAMP` into `src/web_ui.cpp` monitoring/status API payloads and monitoring UI rendering.
- Replaced incorrect monitoring `build_ts` source (device name) with actual `BUILD_TIMESTAMP`.
- Updated `AGENTS.md`/`AI_CONTEXT.md` to make CodeDate discipline explicit and durable for future sessions.
Verification:
- Ran `pio run` (successful build; build validator output included `CODE_DATE validated successfully`).
- Re-ran `pio run` after strict missing-file enforcement in `build_validate.py` (successful; `CODE_DATE validated successfully`).
- Ran `powershell -ExecutionPolicy Bypass -File scripts/update_mcp_sync_state.ps1 -SessionId S-20260417-002 -Notes "CodeDate system rollout"`.
- Ran `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1`.
Open Issues:
- `T-20260417-002` (unknown non-FC03/FC04 traffic classification) and `T-20260417-003` (next register expansion prioritization) remain open.
Next Action:
- Refresh MCP sync-state fingerprint and run memory validator.

S-20260417-003 | UTC: 2026-04-17 20:39
Goal: Decode off-integration SUN2000 traffic by classifying and implementing support for dominant CRC-valid proprietary `FRAME_UNKNOWN` path.
Linked Tasks: T-20260417-002
Linked Decisions: D-20260417-004
Linked History: H-20260417-003
Changes:
- Mined all local `*sniffer-log*.txt` captures and confirmed dominant proprietary traffic is FC `0x41`, mainly subcommands `0x33` and `0x34`, with consistent list-framed payload structure.
- Implemented dedicated passive FC `0x41` handling in `src/huawei_decoder.cpp`:
  - strict list-frame header validation (`len`/CRC already validated upstream),
  - request-shape parser (`count + addr/words tuples`) and lightweight hash-based request cache,
  - response-shape parser (`count + addr/words/data tuples`) with decode of overlapping `KNOWN_REGS` values,
  - request/response match signal for telemetry (`matched=yes/no`) while keeping FC03/FC04 path untouched.
- Expanded `KNOWN_REGS` using observed canonical addresses from `_supporting-projects/huawei-solar-lib-3.0.0/src/huawei_solar/registers.py`:
  - `30110`, `30207`, `37926`, `37927`, `42056`, `47000`, `47089`, `47100`, `47242`.
- Updated `src/CodeDate.h` to `20260417-2038` with matching newest change-log bullet.
- Updated handoff memory docs (`TASKS.md`, `DECISIONS.md`, `AI_CONTEXT.md`, `HISTORY.md`) for durability.
Verification:
- Ran `pio run` after decoder/register changes (success).
- Ran `pio run` again after `CodeDate.h` update (success; `CODE_DATE validated successfully`).
Open Issues:
- Significant FC `0x41` address space remains unknown/proprietary and not in canonical register map; current logs are often truncated at 32 bytes, limiting reverse-engineering depth for long frames.
Next Action:
- Capture full-length raw `0x41` frames (non-truncated) and prioritize next canonical/proprietary additions (`T-20260417-003`, `T-20260417-005`).

S-20260417-004 | UTC: 2026-04-17 21:17
Goal: Build a dedicated cumulative register file that centralizes decoded and unknown register intelligence.
Linked Tasks: T-20260417-006
Linked Decisions: D-20260417-005
Linked History: H-20260417-004
Changes:
- Added `REGISTER_CATALOG.md` as a single inventory source for register intelligence.
- Auto-generated decoded register table from `src/huawei_decoder.cpp` (`KNOWN_REGS`) with 248 entries across all groups.
- Parsed all local sniffer logs (`!CLEAN!sniffer-log*`, `sniffer-log*`) and extracted strict non-truncated FC `0x41` list-frame address sets with request/response frequency.
- Added FC `0x41/0x33` observed-not-decoded table (40 addresses) with per-address request words/count and classification tags.
- Added FC `0x41/0x34` observed address table (`11100`, `15520`, `19000`) with response counts and sample raw values.
- Cross-referenced observed addresses against canonical `_supporting-projects/huawei-solar-lib-3.0.0/src/huawei_solar/registers.py` and Huawei V3 PDF mappings for documented-name enrichment.
- Updated `TASKS.md`, `AI_CONTEXT.md`, `DECISIONS.md`, and `HISTORY.md` to wire this catalog into ongoing workflow.
Verification:
- Parsed/validated register inventory data using deterministic scripts over `src/huawei_decoder.cpp`, log files, canonical `registers.py`, and `SUN2000MA V200R024C00SPC106 Modbus Interface Definitions(V3.0).pdf`.
Open Issues:
- `0x41` log truncation at 32 bytes still limits extraction for many response frames.
- Multiple high-frequency `0x41/0x33` addresses remain unmapped (`16300`, `16304`, `10000` block, `32300`, `34000`, `43139`, `43220`, `44001`, `45255`, `47303`, `47781`).
Next Action:
- Use `REGISTER_CATALOG.md` to approve next implementation batch for decoded additions and decide handling for proprietary low-range offsets.

S-20260417-005 | UTC: 2026-04-17 21:56
Goal: Repurpose `debug.raw_frame_dump` for filtered full-frame unknown capture to support FC `0x41` reverse-engineering.
Linked Tasks: T-20260417-008
Linked Decisions: D-20260417-006
Linked History: H-20260417-005
Changes:
- Updated `src/huawei_decoder.cpp` raw frame dump behavior:
  - filter to `FRAME_UNKNOWN` + `fc=0x41` only,
  - remove 32-byte truncation for this path,
  - emit full payload as chunked reconstructable lines with sequence/chunk/offset metadata.
- Updated docs/comments for the setting in `src/huawei_decoder.h`, `src/ConfigManager.h`, and `src/web_ui.cpp` help text to reflect new semantics.
- Updated `src/CodeDate.h` to `20260417-2155` with matching top changelog bullet.
Verification:
- Ran `pio run` (success, `CODE_DATE validated successfully`).
Open Issues:
- Unknown FC `0x41` address semantics remain unresolved; this change only improves capture fidelity and noise filtering.
Next Action:
- Capture new logs with `debug.raw_frame_dump=true` and mine full FC `0x41` responses for unresolved addresses.

S-20260418-001 | UTC: 2026-04-18 19:13
Goal: Add a robust Wi-Fi streaming path for raw FC `0x41` frame capture to avoid `/logs` page bottlenecks.
Linked Tasks: T-20260418-001
Linked Decisions: D-20260418-001
Linked History: H-20260418-001
Changes:
- Added `src/RawFrameStreamer.h/.cpp`:
  - PSRAM-backed queue of captured raw frames,
  - background TCP sender task with reconnect loop,
  - compact binary wire format (`RFS1` header + metadata + raw bytes),
  - runtime stats for queued/sent/dropped/reconnect/fail-connect counters.
- Wired raw stream into `src/huawei_decoder.cpp` raw-capture path:
  - enqueue unknown FC `0x41` frames when `debug.raw_frame_dump` is enabled,
  - optional serial mirroring behavior (`raw_stream.serial_mirror`).
- Extended settings/schema and validation:
  - `src/ConfigManager.h/.cpp` adds `raw_stream` config fields,
  - `build_validate.py` validates `debug.sensor_refresh_metrics`, `debug.raw_frame_dump`, and `raw_stream.*`.
- Extended UI/API:
  - `src/web_ui.cpp` settings form now exposes raw stream controls,
  - monitoring API/UI includes raw stream status and counters.
- Added collector helper script: `scripts/raw_stream_collector.py`.
- Updated `config.json.example` with `raw_stream` section and expanded `debug` defaults.
- Updated `src/CodeDate.h` to `20260418-1912` with matching top changelog bullet.
Verification:
- Ran `pio run` (success; `CODE_DATE validated successfully`).
Open Issues:
- End-to-end long-duration field validation pending for queue sizing/reconnect behavior under real network jitter.
Next Action:
- Run a >=30 minute capture with `scripts/raw_stream_collector.py` and tune `raw_stream.queue_kb` based on observed drop counters.

S-20260420-001 | UTC: 2026-04-20 12:10
Goal: Consolidate long-capture reverse-engineering findings and update project + MCP memory.
Linked Tasks: T-20260418-002, T-20260420-002, T-20260420-001
Linked Decisions: D-20260420-001, D-20260420-002
Linked History: H-20260420-001
Changes:
- Analyzed `captures/raw-stream1.bin/.ndjson` (~202k frames, ~5.90h) and `captures/raw-stream.bin/.ndjson` (~90k frames, ~2.64h).
- Confirmed latest capture contains production -> no-production transition window (~`+05:05..+05:40` capture offset) via known power/status channels.
- Established high-confidence proprietary mapping candidate:
  - `16300/2` behaves as aggregate signed power.
  - `16304/9` contains three signed component words (positions 2/4/6) that approximately sum to `16300` (mostly quantization-level residual).
- Distinguished packed contiguous FC `0x41/0x33` list reads from genuinely unknown semantics (e.g. `32080/4`, `37758/3`, `47081/2`, `47086/2`, `37119/4`, `32091/4` are composite transport reads overlapping known addresses).
- Updated memory artifacts: `TASKS.md`, `DECISIONS.md`, `AI_CONTEXT.md`, `HISTORY.md`.
- Added MCP memory entity `FC41_LongCapture_Inference_2026-04-20` with durable observations.
Verification:
- Dataset/stat checks via local Python parsing scripts over `captures/raw-stream1.bin/.ndjson` and `captures/raw-stream.bin/.ndjson` (frame continuity, subcommand distribution, per-address value behavior, correlation checks).
Open Issues:
- Several `0x41/0x33` addresses remain unresolved (`37829`, `42017`, `42045`, `37918`, and lower-frequency proprietary blocks).
Next Action:
- Convert `16300/16304` inference into implementation-ready mapping spec and proceed with decoder integration in a controlled batch.

S-20260420-002 | UTC: 2026-04-20 18:47
Goal: Expose duplicate register values by decode source on the homepage without collapsing same-name entries.
Linked Tasks: T-20260420-003
Linked Decisions: D-20260420-003
Linked History: H-20260420-002
Changes:
- Extended decoder callback contract (`DecodedValueCallback`) to include `source_id`, and tagged FC03/FC04/H41 paths in `src/huawei_decoder.cpp`.
- Updated MQTT/dashboard cache model in `src/mqtt_publisher.cpp` to keep entries distinct by `name + source_id + slave_addr`.
- Kept Home Assistant discovery stable by suppressing duplicate discovery publishes for same logical register name.
- Updated `/api/values` output keys to include source+slave identity (`register@source#slave`) and added source metadata fields.
- Updated homepage rendering in `src/web_ui.cpp` to show source badge (`DOC`/`REV` + source tag) and slave badge per register row.
- Updated `src/CodeDate.h` with `20260420-1847` release marker and changelog bullet.
Verification:
- Ran `pio run` (initially failed due source-id enum visibility in `mqtt_publisher.cpp`, then fixed).
- Re-ran `pio run` after fixes (success, `CODE_DATE validated successfully`).
Open Issues:
- Dashboard now distinguishes by source family/slave but still intentionally collapses repeated updates from the same source/slave pair.
Next Action:
- If needed, add an optional source legend/help text on Home page and decide whether MQTT payload topics should gain source-qualified fields too.

S-20260420-003 | UTC: 2026-04-20 18:49
Goal: Finalize source-aware implementation hygiene and re-verify build gates.
Linked Tasks: T-20260420-003
Changes:
- Removed unintended UTF-8 BOM markers from `src/mqtt_publisher.cpp` and `src/web_ui.cpp`.
- Re-ran firmware build gate after encoding cleanup.
Verification:
- Ran `pio run` (success, `CODE_DATE validated successfully`).
Open Issues:
- None newly introduced in this cleanup pass.
Next Action:
- Keep capture analysis focused on unresolved proprietary `0x41` addresses and correlate them with source-tagged live values.

S-20260420-004 | UTC: 2026-04-20 19:07
Goal: Fix missing min/avg/max display for fast-updating Home page rows.
Linked Tasks: T-20260420-004
Linked Decisions: D-20260420-004
Linked History: H-20260420-003
Changes:
- Updated `src/mqtt_publisher.cpp` to export millisecond-native interval metrics in `/api/values` (`min_ms`, `avg_ms`, `max_ms`) while keeping legacy rounded `*_s` fields.
- Updated Home page JS in `src/web_ui.cpp` to render interval stats from `*_ms` and format sub-second values in `ms` instead of hiding them when rounded seconds are `0`.
- Updated `src/CodeDate.h` to `20260420-1906` with matching changelog entry.
Verification:
- Ran `pio run` (success, `CODE_DATE validated successfully`).
Open Issues:
- None identified in this fix path.
Next Action:
- Observe next daytime capture session and verify all frequently-updating `REV H41-33` rows now retain visible interval stats.

S-20260420-005 | UTC: 2026-04-20 19:19
Goal: Streamline Home page top status area and add concise source-tag help.
Linked Tasks: T-20260420-005
Linked Decisions: D-20260420-005
Linked History: H-20260420-004
Changes:
- Updated Home page JS in `src/web_ui.cpp` to remove IP/host/uptime/heap/RSSI/MQTT items from the status bar.
- Kept `Frames` progress and added explicit polling cadence display (`values 3s / status 4s`).
- Added inline legend hints for `DOC/REV/UNK`, `S#`, and source families (`FC03/FC04` vs `H41-33/H41-X`).
- Updated `src/CodeDate.h` to `20260420-1918`.
Verification:
- Ran `pio run` (success, `CODE_DATE validated successfully`).
Open Issues:
- None for this UI adjustment.
Next Action:
- Optionally move the legend into a collapsible help chip if we want an even cleaner top bar.

S-20260420-006 | UTC: 2026-04-20 19:31
Goal: Align register catalog categorization with the Home-page grouping model.
Linked Tasks: T-20260420-006
Changes:
- Updated `REGISTER_CATALOG.md` timestamp and intro notes to reflect `/api/values` Home-page grouping model.
- Added section `0` with source-tag legend (`DOC`/`REV`/`UNK`) and enum-to-Home-group mapping (`GRP_*` -> `mqtt_subtopic` key + UI label + decoded count).
- Replaced legacy bullet list of group counts with a direct note pointing to the new mapping table.
Verification:
- Cross-checked mapping values against `src/reg_groups.h` (`GROUP_INFO`) and source tags against `src/mqtt_publisher.cpp`.
Open Issues:
- Main decoded table still keeps enum group in row-level column; Home-page key/label mapping is intentionally centralized in section `0.2`.
Next Action:
- If desired, extend the row table with an additional `home_group` column in a follow-up catalog format update.

S-20260420-007 | UTC: 2026-04-20 19:32
Goal: Add a quick-priority chapter for identified but not-yet-decoded registers.
Linked Tasks: T-20260420-007
Linked Decisions: D-20260420-006
Linked History: H-20260420-006
Changes:
- Updated `REGISTER_CATALOG.md` with new section `4` ("Identified But Not Yet Decoded Backlog") containing P1-P4 priority buckets.
- Added implementation shortlist and per-item validation constraints, explicitly lowering battery-domain items due to missing battery hardware and treating system time as low priority.
- Renumbered catalog notes section from `4` to `5`.
Verification:
- Cross-checked backlog entries against existing unknown-address table counts and known-documentation tags in the same catalog.
Open Issues:
- Priorities are current-session policy and should be revisited when battery hardware becomes available.
Next Action:
- Use the new P1 list (`16300`, `16304`, `42045`) as the next decode implementation batch candidate.

S-20260420-008 | UTC: 2026-04-20 19:52
Goal: Implement the first high-priority reverse-engineered register batch from catalog backlog.
Linked Tasks: T-20260420-008
Linked Decisions: D-20260420-007
Linked History: H-20260420-007
Changes:
- Added inferred FC `0x41` power decode entries in `src/huawei_decoder.cpp`:
  - `16300/2` -> `h41_active_power_total` (`I32`, W)
  - `16305/1`, `16307/1`, `16309/1` -> `h41_active_power_ch1/ch2/ch3` (`I16`, W)
- Added documented `42045/1` decode entry -> `off_grid_mode` (`U16`) in inverter status group.
- Updated `REGISTER_CATALOG.md` to reflect promotion from backlog:
  - decoded counts (`GRP_INVERTER_AC`, `GRP_INVERTER_STATUS`, total),
  - removed `16300/16304/42045` from not-decoded table,
  - updated not-decoded count (`40` -> `37`),
  - marked promoted backlog items.
- Updated `src/CodeDate.h` to `20260420-1951`.
Verification:
- Ran `pio run` (success, `CODE_DATE validated successfully`).
Open Issues:
- `h41_active_power_*` fields are inference-based and should stay under continued validation as more daytime captures arrive.
Next Action:
- Observe live values and confirm `h41_active_power_total` ~= `h41_active_power_ch1 + ch2 + ch3` under varying production/load states.

S-20260420-009 | UTC: 2026-04-20 20:00
Goal: Add a dedicated provisional tag for fresh reverse-engineered registers to improve Home-page identification.
Linked Tasks: T-20260420-009
Linked Decisions: D-20260420-008
Linked History: H-20260420-008
Changes:
- Kept API-side provisional tagging in `src/mqtt_publisher.cpp` (`vtag="NEW"`) for current inference-based names: `h41_active_power_total`, `h41_active_power_ch1/ch2/ch3`, and `off_grid_mode`.
- Updated Home page renderer in `src/web_ui.cpp` to pass through `e.vtag`, render a dedicated `NEW` badge (`badge-new`), and extend the top legend text to include `NEW=unconfirmed decode`.
- Updated `src/CodeDate.h` to `20260420-1956` with matching changelog entry.
Verification:
- Ran `pio run` (success, `CODE_DATE validated successfully`).
Open Issues:
- `NEW` tagging currently uses a curated name list; revisit when additional reverse-engineered candidates are promoted/demoted.
Next Action:
- As new registers are inferred, add/remove names from provisional tagging list and promote to confirmed status once field behavior is validated.

S-20260421-001 | UTC: 2026-04-21 07:36
Goal: Implement profile-based raw capture controls for repeatable comparison/reverse-engineering sessions.
Linked Tasks: T-20260421-001
Linked Decisions: D-20260421-001
Linked History: H-20260421-001
Changes:
- Added `debug.raw_capture_profile` in settings/config plumbing (`ConfigManager`, `config.json.example`, `build_validate.py`) with supported values `unknown_h41`, `compare_power`, `research_inverter_phase`.
- Updated decoder raw-capture path (`src/huawei_decoder.cpp`) to apply profile-aware filtering:
  - `unknown_h41`: existing behavior (unknown FC `0x41` only)
  - `compare_power`/`research_inverter_phase`: include unknown FC `0x41` plus selected FC03/FC04 request-response pairs via internal pending-request correlation.
- Wired startup config in `main.cpp` via `huawei_decoder_set_raw_capture_profile(...)`.
- Updated settings UI (`src/web_ui.cpp`) to show `Raw Capture` + `Capture Profile` selector and adjusted raw-stream help text; added profile display to monitoring card payload/UI.
- Simplified streamer enqueue gate (`RawFrameStreamer.cpp`) to accept frames selected by decoder profile logic.
- Updated code metadata in `src/CodeDate.h` to `20260421-0735`.
Verification:
- Ran `pio run` (success, config validation + CODE_DATE validation passed).
Open Issues:
- Current capture profiles are fixed, code-defined ranges; if needed later we can move range lists into configurable profile presets.
Next Action:
- Run one daytime `compare_power` capture and execute the comparison report to quantify H41 vs FC03/FC04 match directly.

S-20260421-002 | UTC: 2026-04-21 14:26
Goal: Promote confirmed FC `0x41` power mirrors to final naming/group classification and clear provisional status.
Linked Tasks: T-20260421-002
Linked Decisions: D-20260421-002
Linked History: H-20260421-002
Changes:
- Updated decoder register definitions in `src/huawei_decoder.cpp`:
  - `16300` -> `meter_active_power_fast` (`GRP_METER`)
  - `16305` -> `grid_a_power_fast` (`GRP_METER`)
  - `16307` -> `grid_b_power_fast` (`GRP_METER`)
  - `16309` -> `grid_c_power_fast` (`GRP_METER`)
- Removed provisional `NEW` tagging for these four confirmed fields in `src/mqtt_publisher.cpp` (`is_unconfirmed_register` now keeps only `off_grid_mode`).
- Updated `REGISTER_CATALOG.md` grouping/counts and register names to match runtime classification (`GRP_METER` fast-path mirrors).
- Updated release metadata in `src/CodeDate.h` (`20260421-1425`) and synchronized memory docs (`AI_CONTEXT.md`, `TASKS.md`, `DECISIONS.md`, `HISTORY.md`).
Verification:
- Pending: `pio run`
- Pending: `powershell -ExecutionPolicy Bypass -File scripts/update_mcp_sync_state.ps1 -SessionId S-20260421-002`
- Pending: `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1`
Open Issues:
- `off_grid_mode` remains provisional (`NEW`) until stronger behavioral confirmation captures are available.
Next Action:
- Build and run memory sync/validation gates, then verify homepage/API show the new meter-fast names under meter group with source separation intact.

S-20260421-003 | UTC: 2026-04-21 19:32
Goal: Refocus `research_inverter_phase` capture mode for inverter-production reverse-engineering while avoiding unknown-protocol blind spots.
Linked Tasks: T-20260421-003
Linked Decisions: D-20260421-003
Linked History: H-20260421-003
Changes:
- Updated `src/huawei_decoder.cpp` profile filters:
  - `research_inverter_phase` FC03/FC04 ranges now target inverter-production context only: `32016–32095` and `42056`.
  - `research_inverter_phase` now captures all `FRAME_UNKNOWN` frames regardless of function code (not limited to FC `0x41`).
  - `compare_power` behavior remains unchanged (unknown FC `0x41` + compare ranges).
- Updated settings help text in `src/web_ui.cpp` to explicitly document each capture profile’s frame-family scope.
- Updated release metadata in `src/CodeDate.h` (`20260421-1443`).
- Updated memory docs (`AI_CONTEXT.md`, `TASKS.md`, `DECISIONS.md`, `HISTORY.md`).
Verification:
- Pending: `pio run`
- Pending: `powershell -ExecutionPolicy Bypass -File scripts/update_mcp_sync_state.ps1 -SessionId S-20260421-003`
- Pending: `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1`
Open Issues:
- Capture volume may increase in research mode due to all unknown function-code families; keep raw stream enabled with serial mirror disabled for long runs.
Next Action:
- Run daytime research capture and compute address census novelty; stop when no new unknown addresses appear over extended intervals.

S-20260422-001 | UTC: 2026-04-22 10:24
Goal: Expose FC `0x41` register `16312` as provisional raw telemetry for live homepage correlation.
Linked Tasks: T-20260422-001
Linked Decisions: D-20260422-001
Linked History: H-20260422-001
Changes:
- Added `h41_16312_raw` (`16312`, `I16`, scale `1`) to `KNOWN_REGS` in `src/huawei_decoder.cpp` under `GRP_METER` as a provisional FC `0x41` telemetry channel.
- Extended provisional tag routing in `src/mqtt_publisher.cpp` so `h41_16312_raw` is emitted with `vtag="NEW"` on `/api/values` and the Home page.
- Updated `REGISTER_CATALOG.md` counts and decoded inventory with the new provisional row (`GRP_METER` count `27`, total decoded `254`).
- Updated release metadata in `src/CodeDate.h` (`CODE_DATE=20260422-0959`) with matching top changelog bullet.
- Updated memory/task/decision/history documents for this rollout.
Verification:
- Ran `pio run` (success; includes `config.json` and `CODE_DATE` validation).
Open Issues:
- Semantic meaning of `h41_16312_raw` remains unconfirmed; keep as operator-correlation telemetry until stronger evidence is available.
Next Action:
- Continue live correlation and promote/rename once field semantics are validated against controlled state changes.

S-20260422-002 | UTC: 2026-04-22 10:40
Goal: Promote FC `0x41` register `16312` from provisional raw channel to confirmed fast reactive-power signal.
Linked Tasks: T-20260422-002
Linked Decisions: D-20260422-002
Linked History: H-20260422-002
Changes:
- Renamed decoder register `16312` in `src/huawei_decoder.cpp` from `h41_16312_raw` to `meter_reactive_power_fast`.
- Set `16312` unit to `var` and kept placement in `GRP_METER` with existing FC `0x41` fast meter mirrors.
- Removed provisional `NEW` tagging for this signal in `src/mqtt_publisher.cpp` by deleting it from `is_unconfirmed_register()`.
- Updated `REGISTER_CATALOG.md` to mark `16312` as confirmed and aligned explanatory note for below-30000 FC `0x41` mirrors.
- Updated `src/CodeDate.h` to `20260422-1040` with matching top changelog bullet.
- Updated task/decision/history entries for this promotion.
Verification:
- Ran `pio run` (success; includes `config.json` and `CODE_DATE` validation).
- Ran `powershell -ExecutionPolicy Bypass -File scripts/update_mcp_sync_state.ps1 -SessionId S-20260422-002`.
- Ran `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1`.
Open Issues:
- `off_grid_mode` remains the only currently NEW-tagged provisional reverse-engineered signal.
Next Action:
- Run build and memory validation gates, then confirm Home page shows `meter_reactive_power_fast` without NEW badge.

S-20260422-003 | UTC: 2026-04-22 11:31
Goal: Implement global all-frame capture mode and improve raw-stream reconnect resilience after Wi-Fi/router interruptions.
Linked Tasks: T-20260422-003
Linked Decisions: D-20260422-003, D-20260422-004
Linked History: H-20260422-003
Changes:
- Added `all_frames` profile in `src/huawei_decoder.cpp`; this profile now captures every parsed frame family (`REQ`, `RSP`, `EXC`, `UNKNOWN`) with no capture filter.
- Extended profile parsing/settings contract in `src/ConfigManager.h`, `src/web_ui.cpp`, and `build_validate.py` to support `debug.raw_capture_profile="all_frames"`.
- Improved Monitoring page visibility in `src/web_ui.cpp` by rendering raw stream reconnect diagnostics (`Raw Stream Reconnects`, `Raw Stream Failed Connects`).
- Hardened collector reconnect behavior in `scripts/raw_stream_collector.py`:
  - Added socket keepalive best-effort setup.
  - Added configurable recv timeout (`--conn-timeout`) and stale-client idle timeout (`--idle-timeout`).
  - Added graceful Ctrl+C shutdown message to avoid traceback noise during planned stop.
- Updated release metadata in `src/CodeDate.h` (`CODE_DATE=20260422-1131`) with matching changelog bullet.
- Updated memory docs (`AI_CONTEXT.md`, `TASKS.md`, `DECISIONS.md`, `HISTORY.md`).
Verification:
- Ran `py -3 -m py_compile scripts/raw_stream_collector.py`.
- Ran `pio run` (success).
- Ran `powershell -ExecutionPolicy Bypass -File scripts/update_mcp_sync_state.ps1 -SessionId S-20260422-003`.
- Ran `powershell -ExecutionPolicy Bypass -File scripts/validate_ai_memory.ps1`.
Open Issues:
- `all_frames` can generate very large capture files; recommend stream-only mode (`raw_stream.serial_mirror=false`) and collector-side file rotation for multi-hour runs.
Next Action:
- Run memory sync/validator gates and verify Settings page profile switch + Monitoring reconnect counters on device.

S-20260422-004 | UTC: 2026-04-22 19:12
Goal: Expose moving unmapped raw watchlist telemetry and make source-register provenance visible on Home page for faster live reverse-engineering.
Linked Tasks: T-20260422-004
Linked Decisions: D-20260422-005
Linked History: H-20260422-004
Changes:
- Extended decoder callback contract to carry `reg_addr` and `reg_words` for every decoded value.
- Propagated register metadata through publisher cache and `/api/values` payload (`reg`, `reg_words`, `reg_end`).
- Updated Home page renderer to show per-row register badges (`Rxxxx` / `Rxxxx-yyyy`).
- Added moving unmapped watchlist sensors as raw decoded fields:
  - Inverter/diagnostic: `h41_16000_raw`, `h41_16001_raw`, `h41_30209_raw`, `h41_30210_raw`, `h41_30283_raw`, `h41_30284_raw`, `h41_30285_raw`, `h41_30286_raw`, `h41_35304_raw`, `h41_35306_raw`, `h41_35307_raw`, `h41_40000_raw`, `h41_40001_raw`, `h41_42017_raw`, `h41_42018_raw`.
  - Battery/status: `h41_37518_raw`, `h41_37519_raw`, `h41_37829_raw`, `h41_37830_raw`, `h41_37831_raw`, `h41_37918_raw`, `h41_37919_raw`, `h41_37928_raw`.
- Tagged `h41_*` names as provisional (`NEW`) in API/Home badge flow.
- Updated `src/CodeDate.h` to `20260422-1912` with matching changelog entry.
Verification:
- Ran `pio run` (success).
Open Issues:
- `REGISTER_CATALOG.md` still needs the new watchlist entries mirrored in its decoded table and backlog notes.
Next Action:
- Align `REGISTER_CATALOG.md` with the new `h41_*_raw` watchlist rows and continue daytime correlation triage.

S-20260422-005 | UTC: 2026-04-22 19:48
Goal: Add a dedicated Live Modbus state page that updates rows in place and includes unknown decoded registers for faster reverse-engineering.
Linked Tasks: T-20260422-005
Linked Decisions: D-20260422-006
Linked History: H-20260422-005
Changes:
- Added a new Live page route `/live` and nav entry in `src/web_ui.cpp`.
- Implemented 4 fixed columns on Live page (`Inverter`, `Battery`, `Meter`, `Other`) with in-place row updates (no rolling log behavior).
- Added delta API endpoint `/api/live_values?since=` backed by `mqtt_get_live_values_json()` in `src/mqtt_publisher.cpp`.
- Added a dedicated SPIRAM-backed live-state cache in `src/mqtt_publisher.cpp` keyed by source+slave+register.
- Extended decoder unknown extraction:
  - FC03/FC04 response words not covered by `KNOWN_REGS` are now emitted via `mqtt_publish_unknown_value_u16()`.
  - H41 list-frame entry words not covered by `KNOWN_REGS` are now emitted similarly.
- Kept Home page behavior unchanged; Live page is a separate reverse-engineering view.
- Updated `src/CodeDate.h` to `20260422-1948` with matching newest changelog bullet.
Verification:
- Ran `pio run` (initial pass found compile error due `MODBUS_MAX_REG_COUNT` symbol; fixed to bounded local array).
- Ran `pio run` again (success).
- Ran `pio run` after SPIRAM allocation fix for live cache (success, RAM usage returned to ~30.8%).
Open Issues:
- Live-state unknown value rendering currently exports unknown words as raw `U16`; signedness/scale inference remains intentionally unresolved.
Next Action:
- Validate `/live` on device during daytime traffic and tune unknown formatting/range heuristics if specific candidates need richer decoding display.

S-20260422-006 | UTC: 2026-04-22 20:41
Goal: Complete interrupted refactor so Live Modbus storage is fully separated from MQTT publisher internals.
Linked Tasks: T-20260422-006
Linked Decisions: D-20260422-007
Linked History: H-20260422-006
Changes:
- Finished runtime fanout wiring in `src/main.cpp`:
  - added `LiveValueStore.h` include,
  - added `on_decoded_value(...)` callback that forwards known decoded values to both `mqtt_publish_value(...)` and `live_value_store_publish_known(...)`,
  - initialized Live store via `live_value_store_init()` and switched decoder init to `huawei_decoder_init(on_decoded_value)`.
- Kept decoder unknown publishing on the dedicated path (`live_value_store_publish_unknown_u16(...)`) and kept `/api/live_values` on `live_value_store_get_json(...)`.
- Finalized module split by removing live-cache API declarations from `src/mqtt_publisher.h` and live-cache implementation responsibilities from `src/mqtt_publisher.cpp`.
- Added dedicated module files for Live state ownership: `src/LiveValueStore.h`, `src/LiveValueStore.cpp`.
- Updated release metadata in `src/CodeDate.h` (`CODE_DATE=20260422-2040`) with matching newest changelog bullet.
- Updated memory docs (`TASKS.md`, `DECISIONS.md`, `AI_CONTEXT.md`, `HISTORY.md`).
Verification:
- Ran `pio run` (success).
Open Issues:
- Live unknown-value inference remains raw-first (`U16`) by design pending additional semantics validation.
Next Action:
- Run MCP sync-state + memory validator gates for this session checkpoint.
