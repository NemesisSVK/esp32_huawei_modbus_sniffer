# Huawei Solar Modbus RTU Passive Sniffer

ESP32-S3 firmware that passively sniffs Huawei Modbus RTU traffic and publishes decoded values to MQTT.

Primary tested deployment is on the SUN2000 <-> SDongle uplink bus. Direct SUN2000 <-> DTSU666 meter-bus decoding is also supported and tested.

## Tested Hardware Profile

- Inverter: Huawei SUN2000 family (exact model variant not yet recorded in repo metadata)
- Smart Dongle: Huawei SDongle (exact hardware variant not yet recorded in repo metadata)
- Power meter: Huawei DTSU666-H (direct meter-bus FC03 float map tested)
- Sniffer device: ESP32-S3 R16N8 + MAX485 auto-direction RS-485 TTL board

If you share your exact SUN2000 and SDongle model strings, add them here so compatibility expectations are explicit.

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

- Dashboard: `/`
- Settings: `/settings`
- Monitoring: `/monitoring`
- Priority monitor: `/priority`

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
