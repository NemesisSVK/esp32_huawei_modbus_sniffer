Import('env')

import subprocess
import os
import shutil
import time
import sys
import re
import socket
import ipaddress

def run_streaming_command(cmd):
    """Run a command, stream combined stdout/stderr live, and return (rc, output)."""
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    output_lines = []
    assert proc.stdout is not None
    for line in proc.stdout:
        output_lines.append(line)
        print(line, end="")

    proc.wait()
    return proc.returncode, "".join(output_lines)

def sync_data_file(project_dir, filename, required):
    src_path = os.path.join(project_dir, filename)
    data_dir = os.path.join(project_dir, "data")
    dst_path = os.path.join(data_dir, filename)

    os.makedirs(data_dir, exist_ok=True)

    if not os.path.exists(src_path):
        if required:
            raise Exception(f"Source {filename} does not exist")

        # Avoid uploading stale secrets/config
        if os.path.exists(dst_path):
            os.remove(dst_path)
            print(f"Removed stale {dst_path} (no {filename} present)")
        else:
            print(f"No {filename} present - skipping")
        return

    shutil.copy(src_path, dst_path)
    print(f"Synced {filename} -> {dst_path}")

def sync_data_files(project_dir, require_ota_json):
    # config.json is required — it is the device config
    sync_data_file(project_dir, "config.json", required=True)

    # ota.json is required for OTA uploads; optional for USB flashing.
    # If missing when not required, we remove data/ota.json to prevent an old
    # password/port being flashed if the root file was deleted.
    sync_data_file(project_dir, "ota.json", required=require_ota_json)

def resolve_upload_host_to_ipv4(host):
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

def after_upload(source, target, env):
    print("Post script started")
    print("Current dir:", os.getcwd())

    project_dir    = env['PROJECT_DIR']
    pioenv         = env.get('PIOENV', None)
    upload_port    = env.get('UPLOAD_PORT', None)
    upload_protocol = env.subst("$UPLOAD_PROTOCOL")

    print("Syncing data/ files before filesystem upload...")
    sync_data_files(project_dir, require_ota_json=(upload_protocol == "espota"))

    print("Running post-upload script: uploading filesystem...")
    cmd = ['pio', 'run']
    if pioenv:
        cmd += ['-e', pioenv]
    cmd += ['-t', 'uploadfs']
    if upload_port:
        cmd += ['--upload-port', str(upload_port)]

    # If using espota and a hostname (e.g. *.local) was provided, resolve it to a
    # stable IPv4 to avoid transient mDNS failures between firmware upload and uploadfs.
    if upload_protocol == "espota" and upload_port:
        resolved = resolve_upload_host_to_ipv4(upload_port)
        if resolved and str(resolved) != str(upload_port):
            try:
                idx = cmd.index("--upload-port")
                cmd[idx + 1] = resolved
                print(f"Resolved upload host {upload_port} -> {resolved}")
            except Exception:
                pass

    # OTA: retry uploadfs for a short period because the device may be rebooting
    # after receiving the firmware image, causing mDNS to be temporarily unavailable.
    attempts      = 1
    delay_seconds = 0
    if upload_protocol == "espota":
        attempts      = 12
        delay_seconds = 5

    for attempt in range(1, attempts + 1):
        if attempt > 1:
            print(f"Retrying filesystem upload (attempt {attempt}/{attempts}) after {delay_seconds}s...")
            time.sleep(delay_seconds)

        print("Executing:", " ".join(cmd))
        return_code, combined = run_streaming_command(cmd)
        if return_code == 0:
            print("Filesystem upload completed successfully.")
            return

        retriable = upload_protocol == "espota" and (
            "Host " in combined and " Not Found" in combined or
            "No response from the ESP" in combined or
            "Receive Failed" in combined or
            "Error: Please specify IP address" in combined or
            # Mid-transfer failure — device may have rebooted (e.g. watchdog, brown-out).
            # Retry: the device will come back up with OTA still armed.
            "Error Uploading" in combined or
            # Windows (Hyper-V / WSL2) can reserve blocks of ephemeral ports, causing
            # espota's local TCP bind() to fail.  A retry picks a new random port.
            "Listen Failed" in combined
        )

        if not retriable or attempt == attempts:
            print("Filesystem upload failed.")
            env.Exit(1)

def before_uploadfs(source, target, env):
    project_dir     = env['PROJECT_DIR']
    upload_protocol = env.subst("$UPLOAD_PROTOCOL")
    print("Pre-uploadfs: syncing data/ files...")
    sync_data_files(project_dir, require_ota_json=(upload_protocol == "espota"))

def before_buildfs(source, target, env):
    project_dir     = env['PROJECT_DIR']
    upload_protocol = env.subst("$UPLOAD_PROTOCOL")
    # Critical: uploadfs builds littlefs.bin before running upload action.
    # Sync here so the FS image always contains the latest root config/ota files.
    print("Pre-buildfs: syncing data/ files before LittleFS image build...")
    sync_data_files(project_dir, require_ota_json=(upload_protocol == "espota"))

env.AddPostAction("upload", after_upload)

# Bind sync to the actual LittleFS image artifact so it runs before packing files.
littlefs_image_target = env.subst("$BUILD_DIR/littlefs.bin")
env.AddPreAction(littlefs_image_target, before_buildfs)
env.AddPreAction("uploadfs", before_uploadfs)
