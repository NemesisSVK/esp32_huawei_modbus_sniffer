# PlatformIO/SCons extra script
# - Reads ota.json from project root
# - When using espota upload protocol, appends espota.py flags:
#     -a <password> -p <port>
# This keeps ota.json as the single source of truth for host-side OTA settings.
#
# type: ignore  # Pylance/SCons environment variables

import json
import os
import subprocess
import sys
import re
import socket
import ipaddress


def _read_ota_config(project_dir):
    ota_path = os.path.join(project_dir, "ota.json")
    if not os.path.exists(ota_path):
        return None, f"ota.json not found at {ota_path}"

    try:
        with open(ota_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        return None, f"ota.json is not valid JSON: {e}"

    if not isinstance(data, dict) or "ota" not in data or not isinstance(data["ota"], dict):
        return None, "ota.json must contain an object 'ota'"

    ota = data["ota"]
    password = ota.get("password", "")
    port = ota.get("port", 3232)

    if not isinstance(password, str) or not password.strip():
        return None, "ota.password must be a non-empty string"
    if not isinstance(port, int) or not (1 <= port <= 65535):
        return None, "ota.port must be an integer between 1 and 65535"

    return {"password": password, "port": port}, None


def _enable_throttled_progress(env):
    upload_protocol = env.subst("$UPLOAD_PROTOCOL")
    if upload_protocol != "espota":
        return

    # Replace UPLOADCMD so espota's progress output is throttled to one line per %
    # (VS Code/CI consoles sometimes render '\r' as new lines).
    wrapper = os.path.join(env["PROJECT_DIR"], "espota_throttled.py")
    env.Replace(
        UPLOADCMD=f'"$PYTHONEXE" "{wrapper}" --step 10 "$UPLOADER" $UPLOADERFLAGS -f $SOURCE'
    )

def _resolve_upload_host_to_ipv4(host):
    if not host:
        return None

    host_str = str(host).strip().strip('"').strip("'")
    if not host_str:
        return None

    # Already an IP?
    try:
        ip = ipaddress.ip_address(host_str)
        if ip.version == 4:
            return host_str
        return None
    except ValueError:
        pass

    # Try system resolver (may or may not support mDNS .local)
    try:
        return socket.gethostbyname(host_str)
    except Exception:
        pass

    # Fallback: call ping and parse resolved IP from output
    try:
        if sys.platform.startswith("win"):
            ping_cmd = ["ping", "-n", "1", "-4", host_str]
        else:
            ping_cmd = ["ping", "-c", "1", "-4", host_str]

        ping_res = subprocess.run(ping_cmd, capture_output=True, text=True)
        out = (ping_res.stdout or "") + "\n" + (ping_res.stderr or "")

        # Windows: Pinging name [1.2.3.4] ...
        m = re.search(r"\[([0-9]{1,3}(?:\.[0-9]{1,3}){3})\]", out)
        if m:
            return m.group(1)

        # Unix-like: PING name (1.2.3.4): ...
        m = re.search(r"\(([0-9]{1,3}(?:\.[0-9]{1,3}){3})\)", out)
        if m:
            return m.group(1)
    except Exception:
        pass

    return None

def _apply_upload_port_ip_fallback(env):
    upload_port = env.subst("$UPLOAD_PORT")
    if not upload_port:
        return

    resolved = _resolve_upload_host_to_ipv4(upload_port)
    if resolved and str(resolved) != str(upload_port):
        env.Replace(UPLOAD_PORT=resolved)
        print(f"[ota_flags] Resolved upload host {upload_port} -> {resolved}")
        return

    try:
        ipaddress.ip_address(str(upload_port))
    except ValueError:
        print(
            f"[ota_flags] Warning: could not resolve upload host '{upload_port}' to IPv4; "
            "if OTA is flaky, retry with --upload-port <device-ip>"
        )


def _apply_espota_flags(source, target, env):
    project_dir = env["PROJECT_DIR"]
    upload_protocol = env.subst("$UPLOAD_PROTOCOL")
    if upload_protocol != "espota":
        return

    _enable_throttled_progress(env)
    _apply_upload_port_ip_fallback(env)

    cfg, err = _read_ota_config(project_dir)
    if err:
        print(f"[ota_flags] Error: {err}")
        env.Exit(1)

    # espota.py flags:
    # -a / --auth    password
    # -p / --port    ESP OTA port
    env.Append(UPLOADERFLAGS=["-a", cfg["password"], "-p", str(cfg["port"])])
    print(f"[ota_flags] Using espota auth from ota.json, port={cfg['port']}")


try:
    Import("env")  # noqa: F821 - Provided by SCons

    _enable_throttled_progress(env)

    # Apply as close as possible to the upload action so we don't race with platform scripts
    env.AddPreAction("upload",      _apply_espota_flags)
    env.AddPreAction("uploadfs",    _apply_espota_flags)
    env.AddPreAction("uploadfsota", _apply_espota_flags)
except Exception as e:
    # Allow local import/testing outside SCons
    if __name__ == "__main__":
        raise
    print(f"[ota_flags] Warning: not running under SCons: {e}")
