# Huawei Solar Modbus RTU Passive Sniffer

ESP32-S3 firmware that passively sniffs Huawei Modbus RTU traffic and publishes decoded values to MQTT.

Primary tested deployment is on the SUN2000 <-> SDongle uplink bus. Direct SUN2000 <-> DTSU666 meter-bus decoding is also supported and tested.

## Tested Hardware Profile

- Inverter: Huawei SUN2000-8K-MAP0, firmware `V200R024C00SPC106`
- Smart Dongle: Huawei SDongleA-05, firmware `V200R022C10SPC110`
- Power meter: Huawei DTSU666-H (direct meter-bus FC03 float map tested)
- Sniffer device: ESP32-S3 R16N8 + MAX485 auto-direction RS-485 TTL board

## Important Topology Notes

- Primary tested wiring in this project: tap the SUN2000 <-> SDongle RS-485 line.
- Secondary tested wiring: tap the SUN2000 <-> DTSU666 RS-485 line.
- With SDongle physically removed, useful traffic on the primary uplink bus drops to almost nothing. Many values visible there exist because SDongle actively polls for them.
- Sniffer stays passive: no transmit in normal operation.

## How It Works

1. UART byte stream is split into frames using Modbus RTU inter-frame gap timing.
2. CRC16-MODBUS is validated.
3. Requests and responses are correlated.
4. Known registers are decoded and tagged by group/source.
5. Group-level publish filtering is applied.
6. Values are published through async MQTT.

## Hardware

- MCU: ESP32-S3 R16N8 (8 MB flash, 16 MB PSRAM)
- RS-485 board: MAX485 auto-direction board (RX-only usage)

## Wiring

### RS-485 board <-> ESP32-S3

- `GND` -> ESP32 `GND`
- `RXD` (board RX from MCU) -> ESP32 TX GPIO (default `17`, idle)
- `TXD` (board TX to MCU) -> ESP32 RX GPIO (default `16`, data input)
- `VCC` -> ESP32 `3.3V`

### RS-485 board <-> target bus

- `A` -> bus A
- `B` -> bus B
- `GND` -> bus ground (recommended)

Tap in parallel, no wire cutting.

## Software Setup

Requirements: VS Code + PlatformIO

First flash:

```bash
pio run --target upload
pio run --target uploadfs
```

Regular firmware update:

```bash
pio run --target upload
```

## Configuration

- Runtime config: `config.json` (on LittleFS)
- Example config in repo: `config.json.example`
- OTA config example: `ota.json.example`

Group enable persistence is stored in `config.json` under `publish.group_enabled`.

## Web UI

| Page | Path | What it does |
|---|---|---|
| Dashboard | `/` | Main runtime overview with decoded groups, MQTT status, and quick health info. |
| Settings | `/settings` | Edit and save runtime configuration (network, MQTT, RS-485, publish behavior, debug). |
| Monitoring | `/monitoring` | Detailed diagnostics (connectivity, MQTT counters, memory, sniffer/availability telemetry). |
| Priority Monitor | `/priority` | Fast view of values routed through the manual priority publish group. |
| Live Modbus | `/live` | Live register-state page (known + unknown activity) fed by `/api/live_values`. |
| Logs | `/logs` | Browser log viewer with start/stop capture controls for runtime debugging. |
| OTA | `/ota` | OTA control page: arm OTA window, view OTA config/status, and copy upload command hints. |

### Page behavior notes

- `Dashboard` refreshes frequently for at-a-glance health and grouped values.
- `Monitoring` is lower-rate but deeper, including raw stream queue/sent/dropped counters.
- `Logs` is session-oriented: open page, start capture, inspect issues, then stop capture.
- `Priority Monitor` only shows values explicitly routed to manual priority group in Settings.
- `Live Modbus` is useful when reverse-engineering: it shows what is currently moving on the bus.

## API Reference

All endpoints follow the same auth/whitelist policy.

- `GET /api/status`
- `GET /api/config`
- `POST /api/config`
- `GET /api/config/export`
- `GET /api/values`
- `GET /api/priority_values`
- `GET /api/register_catalog`
- `GET /api/live_values`
- `GET /api/monitoring/cards`
- `POST /api/ota_arm`
- `GET /api/ota_status`
- `GET /api/logs`
- `POST /api/logs/start`
- `POST /api/logs/stop`
- `POST /api/reboot`

## OTA Workflow (How it works)

1. Configure OTA credentials/window in `ota.json` (on LittleFS).
2. Open `/ota` and click arm (or call `POST /api/ota_arm`).
3. While window is active, upload with the OTA environment (`esp32-s3-n16r8v-ota`).
4. Track live state on `/ota` (backed by `GET /api/ota_status`).

Design intent: OTA is explicitly armed for a limited window, not left permanently open.

## Raw Stream Collector

Raw stream exports binary frame records (`RFS1`) from device to a TCP collector.

Typical flow:

1. Start collector on your PC:

```bash
python scripts/raw_stream_collector.py --host 0.0.0.0 --port 9900 --out raw-stream.bin --index raw-stream.ndjson
```

2. In `/settings` set `raw_stream` options:
   - `enabled=true`
   - `host=<collector-ip>`
   - `port=9900` (or your chosen port)
   - adjust `queue_kb`, `reconnect_ms`, `connect_timeout_ms`, `serial_mirror` as needed

3. Watch health in `/monitoring`:
   - connected/disconnected state
   - queued/capacity
   - sent/dropped/reconnect counters

Use this when you need longer captures for protocol analysis without relying only on serial logs.

## Register Coverage by Bus

Use these bus-specific inventories for exact decoded registers:

- SUN2000 <-> SDongle uplink (primary tested): [REGISTER_LIST_MODBUS_SDONGLE_UPLINK.md](REGISTER_LIST_MODBUS_SDONGLE_UPLINK.md)
- SUN2000 <-> DTSU666 direct meter bus (tested): [REGISTER_LIST_MODBUS_DIRECT_METER.md](REGISTER_LIST_MODBUS_DIRECT_METER.md)

Extended reverse-engineering notes/backlog:

- [REGISTER_CATALOG.md](REGISTER_CATALOG.md)

## MQTT Notes

- Primary integration target is Home Assistant via MQTT (entity-oriented telemetry flow).
- JSON and individual-topic modes are both supported.
- Availability/LWT and diagnostics are managed by async MQTT stack.

## Project Structure

```text
esp32_modbus_sniffer/
|- data/
|- src/
|  |- main.cpp
|  |- modbus_rtu.*
|  |- huawei_decoder.*
|  |- reg_groups.h
|  |- ConfigManager.*
|  |- AsyncMqttTransport.*
|  |- MQTTManager.*
|  |- mqtt_publisher.*
|  |- web_ui.*
|  |- OTAManager.*
|  |- WiFiManager.*
|  |- IPWhitelistManager.*
|- platformio.ini
|- README.md
```

## Troubleshooting Quick Checks

- No frames: verify A/B polarity and RX pin.
- CRC fails: check baud mismatch.
- No useful uplink data: confirm SDongle is present and polling.
- MQTT issues: check broker reachability and auth.

## License

- License text: [LICENSE](LICENSE) (PolyForm Noncommercial 1.0.0)
- Required notice and scope clarification: [NOTICE](NOTICE)
