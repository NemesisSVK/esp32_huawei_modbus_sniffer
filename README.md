# Huawei Solar Modbus RTU Passive Sniffer

ESP32-S3 firmware that **passively intercepts** Modbus RTU traffic on the RS-485 bus between a Huawei SUN2000 inverter and its grid meter (DTSU666). Decoded values are published to MQTT. The inverter operates completely normally — the sniffer never transmits.

---

## Table of Contents

1. [How It Works](#how-it-works)
2. [Hardware](#hardware)
3. [Wiring](#wiring)
4. [Software Setup & Flashing](#software-setup--flashing)
5. [Configuration (config.json)](#configuration-configjson)
6. [Web UI](#web-ui)
7. [API Reference](#api-reference)
8. [OTA Updates](#ota-updates)
9. [Register Groups](#register-groups)
10. [MQTT Topics & Sign Conventions](#mqtt-topics--sign-conventions)
11. [Project Structure](#project-structure)
12. [Troubleshooting](#troubleshooting)

---

## How It Works

```
  SUN2000 Inverter               DTSU666 Meter
  (Modbus Master)    RS-485      (Slave, addr=1)
       ├──────────────────────────────────────┤
                         │
                  [parallel tap — no cut]
                         │
                 TTL-to-RS485 board
                   (MAX485, RX only)
                         │
                     ESP32-S3
                  ┌──────┴──────┐
             Decode frames    Web UI
             per register    <hostname>.local
                         │
                        MQTT
                         │
               Home Assistant / other
```

### Frame detection pipeline

1. **Inter-frame silence gap** (≥3.5 char-times at baud rate) marks frame boundaries
2. **CRC16-MODBUS** validation — corrupt/noise frames silently discarded
3. **Request frames** (FC=0x03, 8 bytes) stored with start address + count
4. **Response frames** matched against stored request → registers decoded
5. **Cold-start fallback** — if boot missed a request, response byte-count matched against known Huawei poll blocks
6. **Group filtering** — decoded values pass group enable/disable check before publishing

### Publish behaviour

Values are published **immediately on decode**. The inverter's natural polling rate (~1–5 s) is the limiting factor.

An optional **Republish Interval** controls how often *unchanged* values are re-sent as a MQTT keep-alive. Set to `0` to publish every cycle.

---

## Hardware

| Component | Specification |
|---|---|
| **MCU** | ESP32-S3 R16N8 (8 MB Flash, 16 MB OPI PSRAM, 240 MHz) |
| **RS-485 board** | TTL to RS485 Hardware Auto Control, MAX485, 3.3 V/5 V |

The **Hardware Auto Control** board monitors TXD and handles DE/RE automatically. Since the sniffer never transmits, the board stays permanently in receive mode — no DE/RE GPIO required (set to `-1`).

---

## Wiring

### RS-485 Board ↔ ESP32-S3

> ⚠️ Board labels (RXD/TXD) are **from the board's perspective** — swapped relative to the ESP32.

| Board Pin | Meaning | Connect to |
|---|---|---|
| **GND** | Ground | ESP32 GND |
| **RXD** | Board receives from MCU | GPIO 17 — ESP32 TX (idle) |
| **TXD** | Board transmits to MCU | **GPIO 16 — ESP32 RX ← data in** |
| **VCC** | Power | **ESP32 3.3 V** |

> GPIO pins are configurable via the web UI Settings → GPIO Pins (saved to config.json, no re-flash needed).

### RS-485 Board ↔ Bus

| Terminal | Connect to |
|---|---|
| **A** | RS-485 bus A wire (parallel tap) |
| **B** | RS-485 bus B wire |
| **GND** | Bus ground (optional but recommended) |

Connect **in parallel** with the existing cable — no cuts needed.

---

## Software Setup & Flashing

**Requirements:** VS Code + [PlatformIO](https://platformio.org/)

### First time

```bash
# 1. Edit data/config.json with your WiFi, MQTT, and GPIO settings
# 2. Optionally edit data/ota.json with your OTA password

# Flash firmware + filesystem
pio run --target upload
pio run --target uploadfs     # uploads data/ to LittleFS (config.json, ota.json)
```

### Subsequent firmware updates (wired)

```bash
pio run --target upload
# config.json is preserved on LittleFS — no re-flash of data/ needed
```

### OTA firmware updates (wireless)

See [OTA Updates](#ota-updates).

### Full factory reset

```bash
pio run --target erase        # erases flash completely
pio run --target upload
pio run --target uploadfs
```

---

## Configuration (config.json)

**`data/config.json`** is the live config file stored in LittleFS. Edit before `uploadfs` for factory defaults; thereafter use the web UI to change settings.

```json
{
  "wifi": {
    "ssid":     "your_ssid",
    "password": "your_password"
  },
  "mqtt": {
    "server":             "192.168.1.100",
    "port":               1883,
    "user":               "",
    "password":           "",
    "client_id":          "huawei-sniffer",
    "base_topic":         "huawei_solar",
    "republish_interval": 60,
    "format":             "json"
  },
  "device_info": {
    "name":         "Huawei Solar Sniffer",
    "manufacturer": "DIY",
    "model":        "ESP32-S3 R16N8"
  },
  "network": {
    "hostname":     "huawei-sniffer",
    "mdns_enabled": true
  },
  "security": {
    "auth_enabled":          false,
    "username":              "admin",
    "password":              "",
    "ip_whitelist_enabled":  false,
    "ip_ranges":             []
  },
  "rs485": {
    "baud_rate":        9600,
    "meter_slave_addr": 1
  },
  "pins": {
    "rs485_rx":    16,
    "rs485_tx":    17,
    "rs485_de_re": -1
  }
}
```

All fields are human-editable. Passwords are stored plaintext in LittleFS (access protected by Basic Auth + IP whitelist).

---

## Web UI

Navigate to **`http://huawei-sniffer.local/`** (or the device IP) after boot. The IP and mDNS address are printed to the serial monitor.

### Dashboard (`/`)

| Element | Description |
|---|---|
| **Status bar** | IP, hostname, uptime, free heap, RSSI, MQTT state (with reason on failure), frame count |
| **Group cards** | One card per auto-detected register group; enable/disable toggle |
| **Reset Groups** | Clears seen groups (republishing restarts on next frame) |

Status bar auto-refreshes every 4 seconds via `/api/status`.

### Settings (`/settings`)

All config.json fields are editable in the web UI. Changes take effect on **Save & Restart**.

| Card | Fields |
|---|---|
| **Network** | WiFi (SSID/password), hostname/mDNS, HTTP auth, IP whitelist/ranges |
| **MQTT** | Broker/port/credentials, client ID, base topic, payload format, device info (name/manufacturer/model) |
| **Publish Configuration** | Tier intervals, per-group tier+enable controls, manual priority group selectors |
| **RS485 / Modbus** | Baud rate, meter slave address, RS485 RX/TX/DE-RE pins |
| **Debug** | Serial logging, refresh metrics, raw capture profile, raw stream exporter settings |
| **Config Backup** | Export config.json (readable JSON) |

---

## API Reference

All endpoints respect the same Basic Auth + IP whitelist as the web pages.

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/status` | JSON: IP, hostname, uptime, heap, RSSI, MQTT state + reason, frame count, group list |
| `GET` | `/api/config` | JSON: current settings (passwords omitted) |
| `POST` | `/api/config` | JSON body: update settings + reboot |
| `GET` | `/api/config/export` | Download beautifier-style pretty `config.json` (stable order, readable formatting) |
| `GET` | `/api/values` | JSON: last decoded value for every register seen (`{group/name: {v, u, slave, group}}`) |
| `GET` | `/api/ota_status` | JSON: OTA armed state, remaining window, progress, last error |
| `POST` | `/api/ota_arm` | Arm the OTA flash window (requires `ota.json` on LittleFS) |
| `POST` | `/api/groups` | JSON `{key, enabled}`: toggle a group on/off |
| `POST` | `/api/reset_groups` | Clear all seen groups |
| `POST` | `/api/reboot` | Reboot device |

### `/api/status` response example

```json
{
  "ip": "192.168.1.42",
  "hostname": "huawei-sniffer",
  "uptime_s": 3720,
  "heap": 245760,
  "rssi": -62,
  "mqtt_ok": true,
  "mqtt_reason": "connected",
  "frames": 15243,
  "groups": [
    {"key": "meter", "label": "Grid Meter", "description": "...", "enabled": true}
  ]
}
```

### `/api/values` response example

```json
{
  "meter/meter_active_power": {"v": 1234.5, "u": "W", "slave": 1, "group": "meter"},
  "inverter/inverter_active_power": {"v": 4800.0, "u": "W", "slave": 0, "group": "inverter"}
}
```

---

## OTA Updates

OTA is configured via **`data/ota.json`** (flashed with `uploadfs`):

```json
{
  "ota": {
    "password":       "changeme",
    "window_seconds": 600,
    "port":           3232
  }
}
```

> ⚠️ **Change the password** before flashing.

### Workflow

1. Arm the OTA window: `POST /api/ota_arm` (or add an Arm button via the web UI)
2. Check status: `GET /api/ota_status`
3. In PlatformIO, add to `platformio.ini`:
   ```ini
   upload_protocol = espota
   upload_port     = huawei-sniffer.local   ; or IP
   upload_flags    = --auth=changeme
   ```
4. Flash firmware: `pio run --target upload`
5. Flash filesystem: `pio run --target uploadfs`  
   *(OTA uses `setRebootOnSuccess(false)` so both can be flashed in sequence before reboot)*

The device reboots automatically after the filesystem upload completes.

---

## Register Groups

Derived from [huawei-solar-lib](https://github.com/wlcrs/huawei-solar-lib). Every register is tagged with a group:

| Group key | Registers | Key data |
|---|---|---|
| `meter` | 37100–37138 | Phase V/I/P, PF, frequency, energy import/export |
| `inverter` | 32064–32095 | Active/reactive power, phase V/I, PF, frequency, temp |
| `status` | 32000–32010 | Running state, alarm codes, fault code |
| `energy` | 32106–32230 | Daily/monthly/yearly yield, MPPT yield |
| `device_info` | 30071–37201 | Rated power, string count, optimizer count |
| `pv_strings` | 32016–32063 | Per-string voltage and current |
| `battery` | 37758–37786 | SOC, status, bus V/I, charge/discharge power |
| `battery_unit1` | 37000–37068 | Unit 1 SOC, power, V/I, temperature, energy |
| `battery_unit2` | 37700–37755 | Unit 2 SOC, power, V/I, temperature, energy |
| `battery_packs` | 38200–38463 | Per-pack SOC, voltage, current, temperature |
| `battery_settings` | 47000–47955 | Working mode, charge limits, backup SOC |
| `sdongle` | 37498–37516 | Total PV, load, grid, battery power |

On first register hit from a group → group **auto-enabled** → web UI card appears → MQTT publishing starts → state saved to `groups.json`.

---

## MQTT Topics & Sign Conventions

### JSON mode (default)

```
huawei_solar/meter     -> {"meter_active_power":1250.0,"grid_a_voltage":230.1,...}
huawei_solar/inverter  -> {"inverter_active_power":4800.0,"daily_yield":12.34,...}
huawei_solar/battery   -> {"battery_soc":82.0,"battery_power":-1200.0,...}
huawei_solar/diag/cpu_temp              -> {"value":42.3}
huawei_solar/diag/memory_free_heap      -> {"value":245760}
huawei_solar/diag/littlefs_free_percent -> {"value":63.4}
huawei_solar/status    -> "online"   (LWT: "offline", retained)
```

### Individual topics mode

```
huawei_solar/meter/meter_active_power  → 1250.00
huawei_solar/inverter/inverter_active_power     → 4800.00
huawei_solar/battery/battery_soc       → 82.00
```

### Sign conventions

| Register | Positive | Negative |
|---|---|---|
| `meter_active_power` | Exporting to grid | Importing from grid |
| `battery_power` | Charging | Discharging |
| `sdongle_grid_power` | Importing from grid | Exporting to grid |
| `inverter_active_power` | Always positive | — |

---

## Project Structure

```
esp32_modbus_sniffer/
├── data/
│   ├── config.json          ★ edit WiFi/MQTT/pins before uploadfs
│   └── ota.json             ★ edit OTA password before uploadfs
├── platformio.ini           ← board, partitions, PSRAM type, lib deps
├── README.md
└── src/
    ├── config.h             ← compile-time defaults (UART num, buffer sizes, debug level)
    ├── reg_groups.h         ← group enum, labels, MQTT subtopics
    ├── modbus_rtu.h/.cpp    ← CRC16, frame parser
    ├── huawei_decoder.h/.cpp← 100+ grouped registers, cold-start fallback
    ├── ConfigManager.h/.cpp ← LittleFS config.json load/save (atomic write)
    ├── WiFiManager.h/.cpp   ← non-blocking WiFi reconnect
    ├── MQTTManager.h/.cpp   ← PubSubClient wrapper, LWT, state reason strings
    ├── OTAManager.h/.cpp    ← ArduinoOTA, ota.json config, arm window
    ├── IPWhitelistManager.h/.cpp  ← CIDR/range IP allowlist
    ├── mqtt_publisher.h/.cpp← per-group publish, changed-value filter, last-value cache
    ├── web_ui.h/.cpp        ← AsyncWebServer, PSRAM-backed page builder, all API routes
    ├── config_store.h/.cpp  ← stub (replaced by ConfigManager)
    └── main.cpp             ← boot sequence, UART, FreeRTOS sniffer task, watchdog
```

---

## Troubleshooting

### No frames appearing

- **Swap A and B** — RS-485 polarity must match. Try swapping if silent.
- **Check RX pin** — board TXD → ESP32 GPIO 16 (default RX). Verify in Settings → GPIO Pins.
- **Check baud rate** — Huawei default is 9600. Try 19200 or 4800 if no frames decode.
- Enable `DEBUG_LEVEL 3` in `config.h` for byte-level tracing.

### CRC fails on every frame

Baud rate mismatch. Cycle through 4800, 9600, 19200, 38400 via Settings → RS-485.

### Groups never appear in web UI

With `DEBUG_LEVEL 2` you'll see: `RSP slave=0x01 regs=39`. Verify **Meter Slave Address** in Settings → RS-485 matches the slave address shown.

### MQTT not connecting

- The **MQTT badge** on the dashboard shows the exact reason (e.g. `auth fail`, `broker unreachable`, `timeout`).
- Confirm broker IP is reachable from the ESP32 subnet.
- Leave username/password blank if broker has no auth.

### Battery group never appears

Normal — the inverter only polls battery registers when a LUNA2000 battery is physically connected and responding.

### http://huawei-sniffer.local/ not resolving

mDNS can be unreliable on some Windows setups. Use the device IP instead (shown in serial monitor on boot and in the status bar).

### Out of heap / rebooting

- The 30-second hardware watchdog triggers a panic + reboot if any task stalls.
- Check free heap in the status bar or `/api/status`. Normal runtime is >200 KB.
- Web page HTML is built in PSRAM (16 MB OPI) to avoid SRAM fragmentation.
