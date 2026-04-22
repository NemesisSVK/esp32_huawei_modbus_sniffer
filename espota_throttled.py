#!/usr/bin/env python3
"""
Wrapper around Espressif's espota.py that throttles its progress output.

Motivation: espota.py writes progress updates using carriage returns (\\r). Some
terminals/loggers render these as new lines, causing very noisy logs.

Usage (intended for PlatformIO):
  python espota_throttled.py <path-to-espota.py> [espota args...]
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from typing import Optional


_PERCENT_RE = re.compile(r"(\d{1,3})%")


def _clamp_step(step: int) -> int:
    if step <= 0:
        return 1
    if step > 100:
        return 100
    return step


def _looks_like_progress(text: str) -> bool:
    return "Uploading:" in text and "%" in text


def _extract_percent(text: str) -> Optional[int]:
    match = _PERCENT_RE.search(text)
    if not match:
        return None
    try:
        value = int(match.group(1))
    except ValueError:
        return None
    if value < 0 or value > 100:
        return None
    return value


def main() -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--step", type=int, default=1)
    parser.add_argument("espota_path")
    parser.add_argument("espota_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    step = _clamp_step(args.step)
    espota_path = args.espota_path
    if not os.path.exists(espota_path):
        sys.stderr.write(f"[espota_throttled] espota not found: {espota_path}\n")
        return 2

    cmd = [sys.executable, espota_path]
    if args.espota_args and args.espota_args[0] == "--":
        cmd.extend(args.espota_args[1:])
    else:
        cmd.extend(args.espota_args)

    try:
        proc = subprocess.Popen(
            cmd,
            stdout=None,  # passthrough
            stderr=subprocess.PIPE,
            bufsize=0,
        )
    except Exception as exc:
        sys.stderr.write(f"[espota_throttled] failed to start espota: {exc}\n")
        return 2

    last_percent_printed: Optional[int] = None
    printed_done_line = False
    pending = b""

    assert proc.stderr is not None
    try:
        while True:
            chunk = proc.stderr.read(4096)
            if not chunk:
                break

            pending += chunk
            parts = re.split(rb"[\r\n]", pending)
            pending = parts.pop()  # remainder

            for raw in parts:
                if not raw:
                    continue
                text = raw.decode("utf-8", errors="replace")

                if _looks_like_progress(text):
                    percent = _extract_percent(text)
                    if percent is None:
                        continue

                    done = "Done" in text
                    should_print = (
                        last_percent_printed is None
                        or percent >= last_percent_printed + step
                        or (done and not printed_done_line)
                    )

                    if should_print:
                        suffix = " Done..." if done else ""
                        sys.stderr.write(f"Uploading: {percent}%{suffix}\n")
                        sys.stderr.flush()
                        printed_done_line = printed_done_line or done
                        last_percent_printed = percent
                    continue

                sys.stderr.write(text + "\n")
                sys.stderr.flush()
    except KeyboardInterrupt:
        try:
            proc.kill()
        except Exception:
            pass
        return 130
    finally:
        try:
            proc.wait()
        except Exception:
            pass

    # Flush any trailing bytes that weren't terminated by \r/\n
    if pending:
        text = pending.decode("utf-8", errors="replace")
        if not (_looks_like_progress(text) and _extract_percent(text) is not None):
            sys.stderr.write(text)
            sys.stderr.flush()

    return int(proc.returncode or 0)


if __name__ == "__main__":
    raise SystemExit(main())
