# PlatformIO/SCons build script — validates config.json before build.
# Sniffer-specific variant of build_validate.py (ported from HA Manager).
# type: ignore  # Pylance/SCons environment variables

import os
import json
import datetime
import ipaddress
import re

print("Build validation script loaded — config validation active")

IN_SCONS = False
try:
    from SCons.Script import Import  # noqa: F821 - Provided by SCons
    Import("env")                    # noqa: F821 - Provided by SCons
    IN_SCONS = True
except ImportError:
    IN_SCONS = False


# ============================================================
# CODE_DATE validation (same discipline as HA manager project)
# ============================================================

def validate_code_date(project_dir):
    """Validate src/CodeDate.h CODE_DATE format and Change log entry.

    Enforces:
    - CODE_DATE matches YYYYMMDD-HHMM
    - The first (newest) Change log bullet references the same CODE_DATE and has a description
    """
    code_date_path = os.path.join(project_dir, "src", "CodeDate.h")
    if not os.path.exists(code_date_path):
        raise Exception("src/CodeDate.h not found; CODE_DATE validation requires this file")

    with open(code_date_path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()

    code_date_value = None
    for line in lines:
        match = re.match(r'^\s*#define\s+CODE_DATE\s+"(\d{8}-\d{4})"\s*$', line)
        if match:
            code_date_value = match.group(1)
            break

    if not code_date_value:
        raise Exception('src/CodeDate.h must define CODE_DATE as "YYYYMMDD-HHMM"')

    change_log_idx = None
    for idx, line in enumerate(lines):
        if "Change log" in line:
            change_log_idx = idx
            break

    if change_log_idx is None:
        raise Exception("src/CodeDate.h must contain a 'Change log' comment block")

    bullets = []
    for line in lines[change_log_idx + 1 :]:
        bullet_match = re.match(r'^\s*//\s*-\s*(\d{8}-\d{4})\s+UTC:\s*(.+?)\s*$', line)
        if bullet_match:
            bullets.append((bullet_match.group(1), bullet_match.group(2)))
            continue

        # Stop once we hit the CODE_DATE define or a non-comment section.
        if line.strip().startswith("#define CODE_DATE"):
            break
        if line.strip().startswith("#define") or line.strip().startswith("#include"):
            break

    if not bullets:
        raise Exception("src/CodeDate.h Change log must include at least one entry like: // - YYYYMMDD-HHMM UTC: description")

    newest_date, newest_desc = bullets[0]
    if newest_date != code_date_value:
        raise Exception(
            f"src/CodeDate.h Change log newest entry must match CODE_DATE ({code_date_value}); found {newest_date}"
        )
    if not newest_desc or newest_desc.strip() == "":
        raise Exception("src/CodeDate.h Change log newest entry must include a short description after 'UTC:'")


# ============================================================
# IP whitelist validation (mirrors C++ IPWhitelistManager logic)
# ============================================================

def is_valid_ip_or_range(range_str):
    """Single IP or base-suffix range (same format as C++ code)."""
    if '-' in range_str:
        try:
            base_str, suffix_str = range_str.split('-', 1)
            base = ipaddress.IPv4Address(base_str)
            suffix = int(suffix_str)
            if not (1 <= suffix <= 255):
                return False
            if base.packed[3] + suffix > 255:
                return False
            return True
        except Exception:
            return False
    else:
        try:
            ipaddress.IPv4Address(range_str)
            return True
        except Exception:
            return False


def validate_ip_whitelist(security):
    enabled = security.get("ip_whitelist_enabled", False)
    if not isinstance(enabled, bool):
        raise Exception("security.ip_whitelist_enabled must be a boolean")

    ip_ranges = security.get("ip_ranges", [])
    if not isinstance(ip_ranges, list):
        raise Exception("security.ip_ranges must be an array")
    if len(ip_ranges) > 50:
        raise Exception("security.ip_ranges cannot exceed 50 entries")

    for i, r in enumerate(ip_ranges):
        if not isinstance(r, str):
            raise Exception(f"security.ip_ranges[{i}] must be a string")
        if not is_valid_ip_or_range(r):
            raise Exception(f"security.ip_ranges[{i}] is not a valid IP or range: {r!r}")


# ============================================================
# Main config.json validator
# ============================================================

def validate_config(project_dir):
    """Validate config.json at project root before each build."""
    print("=" * 50)
    print("Validating config.json...")
    print("=" * 50)

    config_path = os.path.join(project_dir, "config.json")
    if not os.path.exists(config_path):
        raise Exception(
            "config.json not found at project root.\n"
            "  Create it from config.json.example and fill in your secrets.\n"
            "  post_upload.py will sync it to data/ before each flash."
        )

    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            config = json.load(f)
        print("config.json is valid JSON")
    except json.JSONDecodeError as e:
        raise Exception(f"config.json is invalid JSON: {e}")

    # ---- Required top-level sections ----
    for section in ("wifi", "mqtt", "network", "device_info", "rs485", "pins", "security"):
        if section not in config:
            raise Exception(f"Missing required section '{section}' in config.json")

    # ---- wifi ----
    wifi = config["wifi"]
    if not wifi.get("ssid"):
        print("Warning: wifi.ssid is empty")
    if not wifi.get("password"):
        print("Warning: wifi.password is empty")

    # ---- mqtt ----
    mqtt = config["mqtt"]
    if not mqtt.get("server"):
        print("Warning: mqtt.server is empty — device will start without MQTT")

    port = mqtt.get("port")
    if not isinstance(port, int) or not (1 <= port <= 65535):
        raise Exception(f"mqtt.port must be an integer 1–65535, got: {port!r}")

    ri = mqtt.get("republish_interval", 60)
    if not isinstance(ri, int) or ri < 0:
        raise Exception(f"mqtt.republish_interval must be a non-negative integer, got: {ri!r}")

    fmt = mqtt.get("format", "json")
    if fmt not in ("json", "individual"):
        raise Exception(f"mqtt.format must be 'json' or 'individual', got: {fmt!r}")

    # ---- network ----
    network = config["network"]
    if not network.get("hostname"):
        print("Warning: network.hostname is empty")

    if not isinstance(network.get("mdns_enabled", True), bool):
        raise Exception("network.mdns_enabled must be a boolean")

    # ---- device_info ----
    di = config["device_info"]
    if not di.get("name"):
        print("Warning: device_info.name is empty")

    # ---- rs485 ----
    rs485 = config["rs485"]
    baud = rs485.get("baud_rate")
    if not isinstance(baud, int) or baud <= 0:
        raise Exception(f"rs485.baud_rate must be a positive integer, got: {baud!r}")

    slave = rs485.get("meter_slave_addr")
    if not isinstance(slave, int) or not (1 <= slave <= 247):
        raise Exception(f"rs485.meter_slave_addr must be 1–247 (Modbus spec), got: {slave!r}")

    # ---- pins ----
    pins = config["pins"]
    rx_pin = pins.get("rs485_rx")
    if not isinstance(rx_pin, int) or rx_pin < 0:
        raise Exception(f"pins.rs485_rx must be a non-negative GPIO number, got: {rx_pin!r}")

    tx_pin = pins.get("rs485_tx")
    if not isinstance(tx_pin, int) or tx_pin < 0:
        raise Exception(f"pins.rs485_tx must be a non-negative GPIO number, got: {tx_pin!r}")

    de_re = pins.get("rs485_de_re", -1)
    if not isinstance(de_re, int):
        raise Exception(f"pins.rs485_de_re must be an integer (-1 = auto-control board), got: {de_re!r}")

    # ---- security ----
    security = config["security"]
    if not isinstance(security.get("auth_enabled"), bool):
        raise Exception("security.auth_enabled must be a boolean")

    if security.get("auth_enabled"):
        if not security.get("username"):
            raise Exception("security.username is required when auth_enabled is true")
        if not security.get("password"):
            raise Exception("security.password is required when auth_enabled is true")

    try:
        validate_ip_whitelist(security)
        print("IP whitelist configuration validated successfully")
    except Exception as e:
        print(f"Error: {e}")
        raise

    # ---- debug (optional section) ----
    debug = config.get("debug", {})
    if "logging_enabled" in debug and not isinstance(debug["logging_enabled"], bool):
        raise Exception("debug.logging_enabled must be a boolean")
    if "sensor_refresh_metrics" in debug and not isinstance(debug["sensor_refresh_metrics"], bool):
        raise Exception("debug.sensor_refresh_metrics must be a boolean")
    if "raw_frame_dump" in debug and not isinstance(debug["raw_frame_dump"], bool):
        raise Exception("debug.raw_frame_dump must be a boolean")
    if "raw_capture_profile" in debug:
        rcp = debug["raw_capture_profile"]
        if not isinstance(rcp, str):
            raise Exception("debug.raw_capture_profile must be a string")
        if rcp not in ("unknown_h41", "all_frames"):
            raise Exception(
                f"debug.raw_capture_profile must be one of ['unknown_h41','all_frames'], got: {rcp!r}"
            )

    # ---- raw_stream (optional section) ----
    raw_stream = config.get("raw_stream", {})
    if raw_stream and not isinstance(raw_stream, dict):
        raise Exception("raw_stream must be an object")
    if "enabled" in raw_stream and not isinstance(raw_stream["enabled"], bool):
        raise Exception("raw_stream.enabled must be a boolean")
    if "host" in raw_stream and not isinstance(raw_stream["host"], str):
        raise Exception("raw_stream.host must be a string")
    if "port" in raw_stream:
        p = raw_stream["port"]
        if not isinstance(p, int) or not (1 <= p <= 65535):
            raise Exception(f"raw_stream.port must be 1-65535, got: {p!r}")
    if "queue_kb" in raw_stream:
        qk = raw_stream["queue_kb"]
        if not isinstance(qk, int) or not (32 <= qk <= 2048):
            raise Exception(f"raw_stream.queue_kb must be 32-2048, got: {qk!r}")
    if "reconnect_ms" in raw_stream:
        rm = raw_stream["reconnect_ms"]
        if not isinstance(rm, int) or not (100 <= rm <= 60000):
            raise Exception(f"raw_stream.reconnect_ms must be 100-60000, got: {rm!r}")
    if "connect_timeout_ms" in raw_stream:
        ctm = raw_stream["connect_timeout_ms"]
        if not isinstance(ctm, int) or not (100 <= ctm <= 30000):
            raise Exception(f"raw_stream.connect_timeout_ms must be 100-30000, got: {ctm!r}")
    if "serial_mirror" in raw_stream and not isinstance(raw_stream["serial_mirror"], bool):
        raise Exception("raw_stream.serial_mirror must be a boolean")
    if raw_stream.get("enabled", False):
        if not raw_stream.get("host"):
            raise Exception("raw_stream.host is required when raw_stream.enabled is true")

    # ---- publish (optional section) ----
    publish = config.get("publish", {})
    if publish and not isinstance(publish, dict):
        raise Exception("publish must be an object")
    tiers = publish.get("tiers", {})
    if tiers and not isinstance(tiers, dict):
        raise Exception("publish.tiers must be an object")
    for tk in ("high", "medium", "low"):
        tnode = tiers.get(tk, {})
        if tnode and not isinstance(tnode, dict):
            raise Exception(f"publish.tiers.{tk} must be an object")
        if "interval_s" in tnode:
            iv = tnode["interval_s"]
            if not isinstance(iv, int) or iv < 0:
                raise Exception(f"publish.tiers.{tk}.interval_s must be a non-negative integer, got: {iv!r}")

    mg = publish.get("manual_group", {})
    if mg and not isinstance(mg, dict):
        raise Exception("publish.manual_group must be an object")
    if "enabled" in mg and not isinstance(mg["enabled"], bool):
        raise Exception("publish.manual_group.enabled must be a boolean")
    if "tier" in mg:
        tier = mg["tier"]
        if not isinstance(tier, str) or tier not in ("high", "medium", "low"):
            raise Exception(f"publish.manual_group.tier must be one of high|medium|low, got: {tier!r}")
    if "registers" in mg:
        regs = mg["registers"]
        if not isinstance(regs, list):
            raise Exception("publish.manual_group.registers must be an array")
        if len(regs) > 64:
            raise Exception(f"publish.manual_group.registers max length is 64, got: {len(regs)}")
        for i, r in enumerate(regs):
            if not isinstance(r, str):
                raise Exception(f"publish.manual_group.registers[{i}] must be a string")
            if not r.strip():
                raise Exception(f"publish.manual_group.registers[{i}] must not be empty")

    # Validate CODE_DATE discipline and Change log formatting
    validate_code_date(project_dir)
    print("CODE_DATE validated successfully")

    print("config.json validation completed successfully")
    print("=" * 50)

    # Generate BuildConfig.h with build timestamp
    generate_build_config(project_dir)

    # Validate ota.json (required when using espota upload protocol)
    upload_protocol = ""
    if IN_SCONS:
        try:
            upload_protocol = env.subst("$UPLOAD_PROTOCOL")  # noqa: F821
        except Exception:
            pass

    ota_path = os.path.join(project_dir, "ota.json")
    if os.path.exists(ota_path) or upload_protocol == "espota":
        try:
            validate_ota_json(project_dir)
        except Exception as e:
            if upload_protocol == "espota":
                raise
            print(f"Warning: ota.json validation failed: {e}")


# ============================================================
# BuildConfig.h generator
# ============================================================

def generate_build_config(project_dir):
    """Generate src/BuildConfig.h with UTC build timestamp."""
    out_path = os.path.join(project_dir, "src", "BuildConfig.h")
    ts = datetime.datetime.utcnow().isoformat() + 'Z'

    content = f"""#ifndef BUILD_CONFIG_H
#define BUILD_CONFIG_H

// Auto-generated by build_validate.py — do not edit manually.
// Regenerated on every build.

#define BUILD_TIMESTAMP "{ts}"

#endif // BUILD_CONFIG_H
"""
    with open(out_path, 'w') as f:
        f.write(content)
    print(f"Generated {out_path}  [{ts}]")


# ============================================================
# ota.json validator
# ============================================================

def validate_ota_json(project_dir):
    """Validate ota.json schema used by both host scripts and OTAManager."""
    ota_path = os.path.join(project_dir, "ota.json")
    if not os.path.exists(ota_path):
        raise Exception("ota.json not found at project root")

    try:
        with open(ota_path, "r", encoding="utf-8") as f:
            doc = json.load(f)
    except Exception as e:
        raise Exception(f"ota.json is invalid JSON: {e}")

    if not isinstance(doc, dict) or "ota" not in doc or not isinstance(doc["ota"], dict):
        raise Exception("ota.json must contain an object 'ota'")

    ota = doc["ota"]
    password       = ota.get("password", "")
    window_seconds = ota.get("window_seconds")
    port           = ota.get("port")

    if not isinstance(password, str) or not password.strip():
        raise Exception("ota.password must be a non-empty string")
    if not isinstance(window_seconds, int) or not (10 <= window_seconds <= 3600):
        raise Exception("ota.window_seconds must be an integer between 10 and 3600")
    if not isinstance(port, int) or not (1 <= port <= 65535):
        raise Exception("ota.port must be an integer between 1 and 65535")

    print("ota.json validation completed successfully")


# ============================================================
# SCons / standalone entry points
# ============================================================

if IN_SCONS:
    validate_config(".")
else:
    try:
        validate_config(".")
    except Exception as e:
        print(f"Validation failed: {e}")
        exit(1)

if IN_SCONS:
    env.AddPreAction("$BUILD", validate_config)  # noqa: F821
