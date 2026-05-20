# Huawei Solar Modbus RTU Passive Sniffer

ESP32-S3 firmware that passively sniffs Huawei Modbus RTU traffic and publishes decoded telemetry to MQTT (optimized for Home Assistant MQTT).

This project is passive by design: no RS-485 transmit in normal operation.

## Tested Scope and Topology

Primary tested setup is on the SUN2000 uplink side (SUN2000 <-> SDongle), not the direct meter trunk.

Important behavior from field testing:
- With SDongle attached, this bus carries most of the useful traffic (including meter-related values that the SDongle requests).
- With SDongle removed, traffic on this bus is minimal.

Direct meter-bus decode support is also implemented and validated separately.

## Tested Hardware

- Inverter: `SUN2000-8K-MAP0`
- Inverter firmware: `V200R024C00SPC106`
- Dongle: `SDongleA-05`
- Dongle firmware: `V200R022C10SPC110`
- Power meter: `DTSU666-H`
- MCU: ESP32-S3 R16N8 (8 MB flash, 16 MB PSRAM)
- RS-485 adapter: MAX485 TTL module (hardware auto direction control)

## What It Does

- Captures Modbus RTU frames with inter-frame gap + CRC validation.
- Correlates request/response and decodes known registers.
- Supports standard FC03/FC04 and observed proprietary Huawei FC0x41 traffic.
- Publishes grouped telemetry to MQTT on configured tier cadences.
- Exposes async web UI for settings, live views, monitoring, logs, and OTA flow.
- Supports optional raw stream exporter for long capture sessions to external TCP collector.
- Supports optional derived `home_consumption` metric (`A - B`) from selected priority-stream signals.

## Register Coverage by Bus

Use these files for exact decoded register inventories:

- [`REGISTER_LIST_MODBUS_SDONGLE_UPLINK.md`](REGISTER_LIST_MODBUS_SDONGLE_UPLINK.md)
- [`REGISTER_LIST_MODBUS_DIRECT_METER.md`](REGISTER_LIST_MODBUS_DIRECT_METER.md)
- Full cumulative catalog with notes: [`REGISTER_CATALOG.md`](REGISTER_CATALOG.md)

Quick summary:
- SUN2000 <-> SDongle uplink bus: Map0-style FC03/FC04 plus FC0x41-derived fast/proprietary signals.
- Direct DTSU666 meter bus: FC03 float register map (2102..2222 family).

## Wiring (Passive Tap)

### RS-485 board to ESP32-S3

- `GND` -> ESP32 `GND`
- `RXD` -> ESP32 `TX` (default GPIO `17`, idle only)
- `TXD` -> ESP32 `RX` (default GPIO `16`, data input)
- `VCC` -> ESP32 `3.3V`

### RS-485 board to bus

- `A` -> bus `A`
- `B` -> bus `B`
- `GND` -> bus ground (recommended)

Use a parallel tap. No cable cutting is required.

## Configuration

Main runtime config file is `data/config.json`.

Template:
- [`config.json.example`](config.json.example)

Relevant current sections include:
- `raw_stream` (collector host/port/queue/reconnect)
- `publish.tiers`, `publish.group_tiers`, `publish.group_enabled`
- `publish.manual_group` (priority topic routing)
- `home_consumption` (derived power from `source:register` selectors)

Example `home_consumption` block:

```json
"home_consumption": {
  "enabled": false,
  "selector_a": "h41_33:inverter_active_power_fast",
  "selector_b": "h41_33:meter_active_power_fast",
  "max_skew_ms": 1000,
  "output_name": "home_consumption_power"
}
```

Rules:
- `home_consumption` requires `publish.manual_group.enabled = true`.
- Selectors must be different and valid (`fc03|fc04|h41_33|h41_x:register` or plain register where allowed).
- Selected registers must also exist in `publish.manual_group.registers`.

## Web UI Pages

Base URL: `http://<hostname>.local/` (or device IP).

- `/` Dashboard: grouped latest decoded values, source badges, status summary.
- `/priority`: 1s view for manual priority-group values (`/api/priority_values`).
- `/live`: live Modbus state/delta view (`/api/live_values`).
- `/monitoring`: health cards (WiFi/MQTT/sniffer/memory/raw-stream counters).
- `/settings`: full runtime settings editor + config export.
- `/logs`: browser log session controls and streaming log viewer.
- `/ota`: explicit OTA arming/status workflow.

## OTA Behavior

OTA is intentionally gated:

1. Configure `data/ota.json` (password/window/port).
2. Arm OTA window via `/api/ota_arm` (or OTA page).
3. Flash within allowed window.

Status endpoint: `/api/ota_status`.

## Raw Stream Collector

When `raw_stream.enabled` is true, selected raw capture records are queued and pushed to an external TCP collector.

Useful for long captures without serial tethering:
- Configure `raw_stream.host` and `raw_stream.port`.
- Use queue/reconnect settings to absorb bursts and unstable links.
- Optionally mirror to serial using `raw_stream.serial_mirror = true`.

Helper script is included:
- `scripts/raw_stream_collector.py`

## API Reference (Current)

- `GET /api/status`
- `GET /api/config`
- `POST /api/config`
- `GET /api/config/export`
- `POST /api/reboot`
- `GET /api/values`
- `GET /api/priority_values`
- `GET /api/register_catalog`
- `GET /api/live_values?since=<seq>`
- `GET /api/monitoring/cards`
- `GET /api/logs?since=<id>`
- `POST /api/logs/start`
- `POST /api/logs/stop`
- `POST /api/ota_arm`
- `GET /api/ota_status`

## MQTT Notes

- Base topic defaults to `huawei_solar`.
- Grouped payload publishing is JSON-oriented in current implementation.
- Manual priority group publishes under `.../priority`.
- Derived `home_consumption` is emitted in the priority payload when enabled and valid.
- Group availability topics are used for online/offline state.

## Build and Flash

PlatformIO workflow:

```bash
pio run --target upload
pio run --target uploadfs
```

`uploadfs` is required when `data/config.json` or `data/ota.json` defaults are changed.

## License

This repository uses PolyForm Noncommercial 1.0.0:
- [`LICENSE`](LICENSE)
- [`NOTICE`](NOTICE)
