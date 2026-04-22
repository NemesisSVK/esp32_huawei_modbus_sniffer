#!/usr/bin/env python3
"""
Raw frame stream collector for ESP32 Modbus sniffer.

Listens on TCP, parses RFS1 binary records, and appends them to a .bin file.
Optional NDJSON sidecar can be enabled for metadata indexing.
"""

from __future__ import annotations

import argparse
import json
import socket
import time
from pathlib import Path

MAGIC = b"RFS1"
HEADER_LEN = 20


def u16be(b: bytes) -> int:
    return (b[0] << 8) | b[1]


def u32be(b: bytes) -> int:
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]


def parse_stream_buffer(buf: bytearray):
    """Yield (packet_bytes, meta_dict) tuples and mutate buffer consumed bytes."""
    out = []
    while True:
        if len(buf) < HEADER_LEN:
            break

        if buf[0:4] != MAGIC:
            idx = buf.find(MAGIC, 1)
            if idx < 0:
                # Keep a small suffix for magic boundary overlap.
                del buf[:-3]
                break
            del buf[:idx]
            continue

        version = buf[4]
        if version != 1:
            del buf[0]
            continue

        frame_type = buf[5]
        slave = buf[6]
        fc = buf[7]
        sub = buf[8]
        seq = u32be(buf[10:14])
        ts_ms = u32be(buf[14:18])
        raw_len = u16be(buf[18:20])
        packet_len = HEADER_LEN + raw_len
        if len(buf) < packet_len:
            break

        pkt = bytes(buf[:packet_len])
        meta = {
            "seq": seq,
            "ts_ms": ts_ms,
            "frame_type": frame_type,
            "slave": slave,
            "fc": fc,
            "sub": sub,
            "raw_len": raw_len,
        }
        out.append((pkt, meta))
        del buf[:packet_len]
    return out


def timestamp_name() -> str:
    return time.strftime("%Y%m%d-%H%M%S", time.localtime())


def _enable_keepalive(conn: socket.socket) -> None:
    try:
        conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    except OSError:
        # Best-effort only; platform-specific tuning is optional.
        pass


def run_server(
    host: str,
    port: int,
    out_bin: Path,
    out_index: Path | None,
    print_every: int,
    conn_timeout_s: float,
    idle_timeout_s: float,
) -> None:
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    if out_index is not None:
        out_index.parent.mkdir(parents=True, exist_ok=True)

    total_frames = 0
    total_bytes = 0
    start = time.time()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(1)
        print(f"[collector] listening on {host}:{port}")
        print(f"[collector] binary output: {out_bin}")
        if out_index:
            print(f"[collector] index output:  {out_index}")

        while True:
            conn, addr = srv.accept()
            print(f"[collector] client connected: {addr[0]}:{addr[1]}")
            conn.settimeout(conn_timeout_s)
            _enable_keepalive(conn)
            buf = bytearray()

            with conn, out_bin.open("ab") as fb:
                fi = out_index.open("a", encoding="utf-8") if out_index else None
                last_rx = time.time()
                try:
                    while True:
                        try:
                            chunk = conn.recv(8192)
                        except socket.timeout:
                            if idle_timeout_s > 0 and (time.time() - last_rx) >= idle_timeout_s:
                                print(
                                    f"[collector] idle timeout ({idle_timeout_s:.0f}s) "
                                    "without data; closing stale client"
                                )
                                break
                            continue
                        except OSError as e:
                            print(f"[collector] socket error: {e}; closing client")
                            break

                        if not chunk:
                            break
                        last_rx = time.time()
                        buf.extend(chunk)
                        for pkt, meta in parse_stream_buffer(buf):
                            fb.write(pkt)
                            total_frames += 1
                            total_bytes += len(pkt)

                            if fi:
                                fi.write(json.dumps(meta, separators=(",", ":")) + "\n")

                            if print_every > 0 and (total_frames % print_every) == 0:
                                dt = max(0.001, time.time() - start)
                                fps = total_frames / dt
                                mb = total_bytes / (1024 * 1024)
                                print(
                                    f"[collector] frames={total_frames} size={mb:.2f} MiB rate={fps:.1f} fps"
                                )
                finally:
                    if fi:
                        fi.close()
            print("[collector] client disconnected")


def main() -> int:
    ap = argparse.ArgumentParser(description="ESP32 raw frame stream collector")
    ap.add_argument("--host", default="0.0.0.0", help="listen host (default: 0.0.0.0)")
    ap.add_argument("--port", type=int, default=9900, help="listen port (default: 9900)")
    ap.add_argument(
        "--out",
        type=Path,
        default=Path(f"raw-stream-{timestamp_name()}.bin"),
        help="binary output file",
    )
    ap.add_argument(
        "--index",
        type=Path,
        default=None,
        help="optional NDJSON metadata sidecar file",
    )
    ap.add_argument(
        "--print-every",
        type=int,
        default=200,
        help="status print interval in frames (0 disables)",
    )
    ap.add_argument(
        "--conn-timeout",
        type=float,
        default=3.0,
        help="socket recv timeout in seconds (default: 3.0)",
    )
    ap.add_argument(
        "--idle-timeout",
        type=float,
        default=120.0,
        help="close stale client if no bytes for N seconds (0 disables, default: 120)",
    )
    args = ap.parse_args()

    try:
        run_server(
            args.host,
            args.port,
            args.out,
            args.index,
            args.print_every,
            args.conn_timeout,
            args.idle_timeout,
        )
    except KeyboardInterrupt:
        print("[collector] stopped by user")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
