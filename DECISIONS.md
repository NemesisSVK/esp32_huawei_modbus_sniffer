# DECISIONS.md

D-20260416-001 | UTC: 2026-04-16 20:43
Decision: Use a six-file AI handoff system (`AGENTS.md`, `AI_CONTEXT.md`, `TASKS.md`, `DECISIONS.md`, `HISTORY.md`, `SESSION_LOG.md`) as project memory.
Status: Accepted
Context: Need reliable continuity across sessions/computers for a new project.
Rationale: Separates stable context, active execution, durable rationale, milestones, and chronological handoff notes.
Impact: Faster startup context and lower session drift.
Supersedes: None

D-20260416-002 | UTC: 2026-04-16 20:43
Decision: Preserve passive-sniffer behavior as a hard default (no active RS-485 transmission unless explicitly requested).
Status: Accepted
Context: Project purpose is observation/decoding of SUN2000 Modbus traffic, not active control.
Rationale: Safety and operational integrity of inverter/meter communication path.
Impact: Changes touching UART/RS485/decoder paths must be reviewed against passive-only behavior.
Supersedes: None

D-20260416-003 | UTC: 2026-04-16 20:43
Decision: Treat `_supporting-projects/` as read-only reference context for decoding and integration parity.
Status: Accepted
Context: Workspace includes multiple reference projects and libraries used to build this sniffer.
Rationale: Preserve upstream/reference integrity while still leveraging them for design/validation.
Impact: Primary changes should target this project root unless user explicitly requests supporting-project edits.
Supersedes: None

D-20260416-004 | UTC: 2026-04-16 20:50
Decision: Use `scripts/validate_ai_memory.ps1` as the required memory consistency gate after project-state updates.
Status: Accepted
Context: Manual memory updates are easy to miss during iterative firmware and tooling changes.
Rationale: Automated checks enforce required files, timestamp formats, ID consistency, and freshness against latest session entry.
Impact: Memory updates are considered incomplete until validator passes.
Supersedes: None

D-20260416-005 | UTC: 2026-04-16 21:00
Decision: Use MCP memory as an optional secondary durable cache while keeping repository handoff files as source of truth.
Status: Accepted
Context: MCP memory/thinking servers are available and can accelerate continuity.
Rationale: Hybrid approach improves recall speed without weakening deterministic, synced file-based project memory.
Impact: Agents may read/write durable facts to `mcp__memory__`, but must still update and validate local handoff files.
Supersedes: None

D-20260416-006 | UTC: 2026-04-16 21:05
Decision: Enforce MCP sync freshness via tracked handoff fingerprint in `MCP_MEMORY_SYNC.json`.
Status: Accepted
Context: MCP sync can drift if handoff files are updated without refreshing MCP state.
Rationale: Fingerprint-based gate provides deterministic check for "MCP sync acknowledged" in the local workflow.
Impact: After memory/MCP-relevant updates, run `scripts/update_mcp_sync_state.ps1` before final validation.
Supersedes: None

D-20260417-001 | UTC: 2026-04-17 20:00
Decision: Use `huawei-solar-lib-3.0.0` `registers.py` as canonical reference for sniffer register parity work.
Status: Accepted
Context: Register-identification work will be ongoing and easily drifts without a single source of truth.
Rationale: Canonical address/type/scale parity reduces decode mistakes when expanding `KNOWN_REGS`.
Impact: Any `KNOWN_REGS` additions/changes must be cross-checked against canonical register definitions or explicitly documented as proprietary.
Supersedes: None

D-20260417-002 | UTC: 2026-04-17 20:00
Decision: Keep CRC-valid non-FC03/FC04 traffic as explicit unknown protocol path until proven otherwise.
Status: Accepted
Context: Captures include CRC-valid frames that do not map to FC03/FC04 request-response decoding.
Rationale: Coercing unknown function-code families into existing Modbus decode logic risks false register interpretation.
Impact: Unknown-frame analysis stays a separate workstream (`FRAME_UNKNOWN`) and must not silently alter FC03/FC04 decode behavior.
Supersedes: None

D-20260417-003 | UTC: 2026-04-17 20:06
Decision: Adopt HA-manager-style `CodeDate.h` as required firmware release metadata contract in this sniffer project.
Status: Accepted
Context: Need stable, human-readable firmware build identity with concise per-release change list, consistent across machines/sessions.
Rationale: `CODE_DATE` + top-of-file change bullets provide deterministic release tagging, and build-time validation prevents drift.
Impact: Firmware/runtime code changes must update `src/CodeDate.h`; build now fails when `CODE_DATE` format or newest Change log parity is invalid.
Supersedes: None

D-20260417-004 | UTC: 2026-04-17 20:39
Decision: Decode Huawei proprietary FC `0x41` list-framed traffic via a dedicated passive path in `huawei_decoder` without reclassifying it as FC03/FC04.
Status: Accepted
Context: Off-integration SUN2000+dongle traffic is dominated by CRC-valid `FRAME_UNKNOWN` frames (`0x41 0x33/0x34`) that carry address+word list payloads and value tuples.
Rationale: Proprietary framing is self-describing and can be decoded safely while preserving the FC03/FC04 parser contract and passive sniff behavior.
Impact: Added `0x41` request hash correlation + response entry decode in decoder hot path; FC03/FC04 pipeline and cold-start fallback remain unchanged.
Supersedes: None

D-20260417-005 | UTC: 2026-04-17 21:17
Decision: Maintain `REGISTER_CATALOG.md` as the single cumulative working inventory for decoded and observed-not-decoded register intelligence.
Status: Accepted
Context: Register facts were previously split across decoder code, session notes, and ad-hoc analysis output.
Rationale: A dedicated catalog reduces drift, keeps unknown-address triage visible, and gives a stable source for implementation prioritization.
Impact: Register discovery/triage work should update `REGISTER_CATALOG.md` in the same session; memory updates reference this file as the operational register backlog source.
Supersedes: None

D-20260417-006 | UTC: 2026-04-17 21:56
Decision: Scope `debug.raw_frame_dump` to filtered full capture of unknown proprietary FC `0x41` frames instead of dumping every Modbus frame.
Status: Accepted
Context: Reverse-engineering was blocked by 32-byte truncation and excessive noise from normal FC03/FC04 traffic.
Rationale: Unknown FC `0x41` frames are the current reverse-engineering target; chunked full-frame output keeps logs reconstructable while reducing unrelated volume.
Impact: Enabling raw dump now logs only `FRAME_UNKNOWN` + `fc=0x41` payloads in chunked lines with frame sequence/chunk metadata.
Supersedes: None

D-20260418-001 | UTC: 2026-04-18 19:13
Decision: Add a dedicated TCP raw-stream export path for `debug.raw_frame_dump` traffic with PSRAM queueing and optional serial mirroring.
Status: Accepted
Context: Browser `/logs` polling is not reliable for sustained high-volume FC `0x41` capture and can introduce apparent gaps/corruption under load.
Rationale: A compact binary stream to a collector app removes web UI bottlenecks and provides durable long-run capture without changing passive RS-485 behavior.
Impact: Raw capture now supports stream-first operation (`raw_stream.*` settings); when stream is enabled and `serial_mirror=false`, decoder avoids verbose serial dump spam while preserving full frame export.
Supersedes: None

D-20260420-001 | UTC: 2026-04-20 12:10
Decision: Treat FC `0x41/0x33` `16300/2` as high-confidence aggregate signed power and `16304/9` as a packed component block where words 2/4/6 form the matching three-component power channels.
Status: Accepted (inference-backed)
Context: Multi-hour raw stream analysis (`captures/raw-stream1*`) showed stable production->no-production transitions and repeatable numeric coupling between these addresses.
Rationale: `16300` tracks inverter production sign/magnitude and approximately equals the sum of `16304` component words (mostly within quantization error), making this the strongest currently observed proprietary mapping.
Impact: Prioritize `16300` + decomposed `16304` for next decode batch; treat naming as provisional until contradicted by future captures.
Supersedes: None

D-20260420-002 | UTC: 2026-04-20 12:10
Decision: Classify many FC `0x41/0x33` "unknown" entries as packed contiguous composite reads when they overlap already-known canonical addresses.
Status: Accepted
Context: Long-capture parsing shows repeated frames like `32080/4`, `37758/3`, `47081/2`, `47086/2`, `37119/4`, and `32091/4` that contain known fields plus adjacent words.
Rationale: These are transport/framing artifacts of list-based read batching rather than evidence of wholly new register semantics.
Impact: Reverse-engineering priority should focus on non-overlap unknowns (`16300`, `16304`, `37829`, etc.) while composite frames should be decoded by slice extraction of known subranges.
Supersedes: None

D-20260420-003 | UTC: 2026-04-20 18:47
Decision: Keep dashboard/API value cache source-aware and slave-aware, and do not collapse identical register names across different decode sources.
Status: Accepted
Context: Same logical register (for example `active_power`) can arrive from multiple polling families (FC03/FC04 and proprietary FC `0x41`) with different cadence and diagnostic value.
Rationale: Source-level visibility is required to understand polling behavior and compare data origin quality without losing fast-path observations.
Impact: Decoder callback carries `source_id`; `/api/values` keys include source+slave identity; homepage renders source and slave badges per row.
Supersedes: None

D-20260420-004 | UTC: 2026-04-20 19:07
Decision: Treat refresh interval metrics as millisecond-native in API/UI and only use rounded seconds as compatibility fields.
Status: Accepted
Context: Fast-updating FC `0x41` rows produced valid intervals below 1 second; rounding to integer seconds yielded `0s`, causing the UI to hide metrics.
Rationale: Millisecond-native metrics preserve visibility for high-frequency sources without changing the underlying per-row sampling logic.
Impact: `/api/values` now includes `min_ms/avg_ms/max_ms`; Home page renders `<1s` intervals in `ms` and keeps legacy `*_s` fields for compatibility.
Supersedes: None

D-20260420-005 | UTC: 2026-04-20 19:19
Decision: Keep the Home page status bar focused on decoding progress and operator hints, not general device telemetry.
Status: Accepted
Context: Network/runtime details (IP/host/heap/RSSI/MQTT) add noise during reverse-engineering sessions where the main need is data-flow progress and source-tag interpretation.
Rationale: A concise status bar improves scanability while preserving essential context (`Frames`, polling cadence, source-tag legend).
Impact: Home status bar now shows `Frames`, refresh intervals, and DOC/REV/UNK + source-family help; broader device telemetry remains available on Monitoring page.
Supersedes: None

D-20260420-006 | UTC: 2026-04-20 19:32
Decision: Maintain a dedicated prioritized "not yet decoded" chapter in `REGISTER_CATALOG.md`, prioritizing non-battery operational registers and de-prioritizing low-value metadata/time fields.
Status: Accepted
Context: Reverse-engineering candidates had become hard to triage quickly, and battery-related registers cannot be verified on this installation due to no connected battery.
Rationale: A single backlog chapter with explicit priority + verification constraints keeps implementation order clear and avoids spending effort on low-impact fields.
Impact: Catalog now includes P1-P4 backlog entries and hardware-validation constraints; current default ordering favors `16300/16304` and `42045` while keeping `40000` (system time) and battery-domain items lower.
Supersedes: None

D-20260420-007 | UTC: 2026-04-20 19:52
Decision: Expose first FC `0x41` inferred power candidates as explicitly `h41_*`-prefixed sensors and keep them in existing inverter groups rather than creating a new experimental group.
Status: Accepted
Context: `16300/16304` had strongest long-capture evidence and were top backlog candidates, but semantics remain inference-based compared to canonical map entries.
Rationale: Prefixing preserves provenance and reduces confusion with canonical registers while still making high-value signals visible in normal dashboards.
Impact: Added `h41_active_power_total` and `h41_active_power_ch1/ch2/ch3` to `GRP_INVERTER_AC`, plus `off_grid_mode` (`42045`) in `GRP_INVERTER_STATUS`.
Supersedes: None

D-20260420-008 | UTC: 2026-04-20 20:00
Decision: Mark freshly reverse-engineered registers with an explicit value tag (`NEW`) in dashboard/API output until semantics are confirmed.
Status: Accepted
Context: Source tags (`DOC/REV/UNK`) alone identify decode origin, but they do not distinguish mature reverse-engineered items from newly inferred mappings under validation.
Rationale: A dedicated provisional marker improves operator awareness and avoids over-trusting early inference-based values.
Impact: `/api/values` now emits `vtag="NEW"` for selected provisional register names, and Home page renders an additional `NEW` badge with legend help.
Supersedes: None

D-20260421-001 | UTC: 2026-04-21 07:36
Decision: Keep a single raw-capture feature toggle and select behavior via `debug.raw_capture_profile` instead of adding separate capture functions.
Status: Accepted
Context: Reverse-engineering now requires targeted multi-source captures (FC `0x41` plus selected FC03/FC04 windows) while preserving existing unknown-H41 workflow.
Rationale: Profile-based routing keeps UX simple, avoids duplicated controls, and allows future research modes without protocol/transport rewrites.
Impact: Added profiles `unknown_h41`, `compare_power`, and `research_inverter_phase`; decoder now filters and correlates FC03/FC04 request-response pairs for capture based on profile.
Supersedes: None

D-20260421-002 | UTC: 2026-04-21 14:25
Decision: Promote confirmed FC `0x41` power mirrors (`16300/16305/16307/16309`) from provisional `h41_*` inverter naming into final Grid Meter fast-path names and group assignment.
Status: Accepted
Context: Multi-hour compare-power captures confirmed strong lag-adjusted equivalence with FC03 meter values (`meter_active_power`, `grid_a/b/c_power`) while preserving significantly faster update cadence from FC `0x41`.
Rationale: Semantics are now sufficiently validated to move from provenance-first provisional labels to operational naming that reflects true meaning, while still avoiding MQTT key collisions with slower FC03 names.
Impact: Decoder now publishes `meter_active_power_fast` + `grid_a/b/c_power_fast` in `GRP_METER`; provisional `NEW` value tagging is removed for these four registers (kept for other still-unconfirmed fields like `off_grid_mode`).
Supersedes: D-20260420-007 (for these four power fields only), D-20260420-008 (for these four power fields only)

D-20260421-003 | UTC: 2026-04-21 14:43
Decision: In `research_inverter_phase` mode, capture all unknown protocol frames (any function code), not only unknown FC `0x41`, while narrowing FC03/FC04 capture to inverter-production ranges.
Status: Accepted
Context: Reverse-engineering for missing inverter production fields must avoid false assumptions about transport family; non-`0x41` unknown carriers are possible.
Rationale: Broad unknown capture avoids protocol blind spots, and focused FC03/FC04 ranges keep comparison context relevant (`W`, `V`, `A`) without unnecessary traffic.
Impact: Research profile now captures `FRAME_UNKNOWN` universally plus FC03/FC04 requests/responses overlapping `32016–32095` and `42056`; UI help text now documents this behavior.
Supersedes: D-20260421-001 (research-profile scope only)

D-20260422-001 | UTC: 2026-04-22 10:24
Decision: Publish FC `0x41` word `16312` as provisional raw telemetry (`h41_16312_raw`) in `GRP_METER` and keep it explicitly tagged `NEW` until semantics are confirmed.
Status: Accepted
Context: `16312` is highly frequent and dynamic in long captures, but current correlation analysis does not prove a canonical field mapping.
Rationale: Exposing the signal enables operator-led live correlation while preventing false confidence via explicit provisional tagging.
Impact: Decoder now emits `h41_16312_raw`; `/api/values` marks it with `vtag="NEW"` for Home-page visibility/triage.
Supersedes: None

D-20260422-002 | UTC: 2026-04-22 10:40
Decision: Promote FC `0x41` word `16312` from provisional raw telemetry to confirmed `meter_reactive_power_fast`.
Status: Accepted
Context: Correlation analysis against FC03 `37115` (`meter_reactive_power`) reached strong agreement (`r≈0.96`) with matching signed range and expected polling lag.
Rationale: Evidence is sufficient to move from provisional naming to operational naming while preserving non-collision with FC03 (`*_fast` suffix).
Impact: Decoder now publishes `meter_reactive_power_fast` (`unit=var`) in `GRP_METER`; provisional NEW-tagging removed for this signal.
Supersedes: D-20260422-001 (for register `16312` only)

D-20260422-003 | UTC: 2026-04-22 11:31
Decision: Add a global raw-capture profile `all_frames` that exports every parsed frame family without filtering.
Status: Accepted
Context: Reverse-engineering on alternate channels requires full baseline datasets, not only filtered FC `0x41`/targeted FC03/FC04 windows.
Rationale: A profile extension keeps a single capture UX while enabling protocol-agnostic evidence collection for future channels.
Impact: `debug.raw_capture_profile` now supports `all_frames`; decoder captures `REQ`/`RSP`/`EXC`/`UNKNOWN` frames unfiltered, and UI/validation reflect the new option.
Supersedes: None

D-20260422-004 | UTC: 2026-04-22 11:31
Decision: Treat collector-side stale TCP session eviction as mandatory reconnect-hardening for raw stream capture.
Status: Accepted
Context: Router/Wi-Fi interruptions can leave half-open collector sockets that block `accept()` and prevent firmware reconnect despite low traffic volume.
Rationale: Idle-timeout and recv-timeout handling in collector is lower risk/faster recovery than attempting protocol keepalive redesign in firmware path.
Impact: Collector now exposes `--conn-timeout` and `--idle-timeout`, closes stale clients automatically, and exits cleanly on Ctrl+C; monitoring UI now surfaces stream reconnect and failed-connect counters for triage.
Supersedes: None

D-20260422-005 | UTC: 2026-04-22 19:12
Decision: Carry source-register metadata with every decoded value and render register badges on Home rows, while exposing selected moving unmapped addresses as explicit `h41_*_raw` watch sensors.
Status: Accepted
Context: Reverse-engineering workflow now depends on fast visual correlation between live values and register addresses; hidden register provenance slows operator validation.
Rationale: Passing `reg_addr/reg_words` through decoder->cache->API gives deterministic per-row provenance without fragile name-to-address lookups, and dedicated `h41_*_raw` sensors keep watchlist telemetry visible without changing protocol behavior.
Impact: Decoder callback now includes register metadata; `/api/values` returns `reg/reg_words/reg_end`; Home rows show `Rxxxx` badges; selected moving addresses (`16000/16001`, `30209/30210`, `30283-30286`, `35304/35306/35307`, `37518/37519`, `37829-37831`, `37918/37919/37928`, `40000/40001`, `42017/42018`) are published for live operator analysis and tagged `NEW`.
Supersedes: None

D-20260422-006 | UTC: 2026-04-22 19:48
Decision: Implement Live Modbus as an in-place state view with delta transport cursor, not as a rolling log stream.
Status: Accepted
Context: Reverse-engineering workflow needs stable rows keyed by source/slave/register so values update in place and unknown candidates remain easy to track visually.
Rationale: State-view behavior reduces UI noise and enables long-running manual correlation; delta API (`since` cursor) limits payload to changed entries while preserving 3-second polling cadence.
Impact: Added `/live` page with 4 fixed columns (`Inverter`, `Battery`, `Meter`, `Other`), new `/api/live_values?since=` endpoint, and decoder-side unknown-register extraction for FC03/FC04 and H41 list frames into a dedicated SPIRAM-backed live cache.
Supersedes: None

D-20260422-007 | UTC: 2026-04-22 20:40
Decision: Keep Live Modbus cache ownership in a dedicated module (`LiveValueStore`) and keep `mqtt_publisher` focused on MQTT publish/discovery/cache duties only.
Status: Accepted
Context: Initial Live Modbus rollout stored live-state logic inside `mqtt_publisher`, creating an unnecessary module coupling between web live-state concerns and MQTT transport behavior.
Rationale: Separating responsibilities reduces maintenance risk, keeps module boundaries clear, and avoids side effects when evolving MQTT or Live page features independently.
Impact: Added `src/LiveValueStore.*`, rewired decoder value fanout in `main.cpp` to publish known values to both MQTT and Live store, moved `/api/live_values` backend to LiveValueStore, and removed live-specific APIs from `mqtt_publisher`.
Supersedes: None
