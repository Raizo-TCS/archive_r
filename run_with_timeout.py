#!/usr/bin/env python3
"""Simple cross-platform timeout wrapper for CI workflows."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
from typing import List


def _terminate_process(proc: subprocess.Popen[bytes], grace: float = 5.0) -> None:
    """Attempt graceful termination, fall back to kill if needed."""
    try:
        if os.name == "nt":
            proc.terminate()
        else:
            proc.send_signal(signal.SIGTERM)
    except Exception:
        pass

    try:
        proc.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        try:
            proc.kill()
        except Exception:
            pass


def run_with_timeout(timeout_seconds: float, command: List[str]) -> int:
    print(f"[run_with_timeout] Starting: {command}", flush=True)
    try:
        proc = subprocess.Popen(command)
    except FileNotFoundError as exc:
        print(f"Failed to launch command: {exc}", file=sys.stderr, flush=True)
        return 127

    try:
        ret = proc.wait(timeout=timeout_seconds)
        print(f"[run_with_timeout] Finished with exit code {ret}", flush=True)
        return ret
    except subprocess.TimeoutExpired:
        print(f"[run_with_timeout] Timed out after {timeout_seconds}s", file=sys.stderr, flush=True)
        _terminate_process(proc)
        return 124
    except KeyboardInterrupt:
        _terminate_process(proc)
        raise


def main() -> int:
    if len(sys.argv) < 3:
        print("Usage: run_with_timeout.py <seconds> <command> [args...]", file=sys.stderr)
        return 2

    try:
        timeout_seconds = float(sys.argv[1])
    except ValueError:
        print(f"Invalid timeout value: {sys.argv[1]}", file=sys.stderr)
        return 2

    if timeout_seconds < 0:
        print("Timeout must be non-negative", file=sys.stderr)
        return 2

    command = sys.argv[2:]
    if not command:
        print("Command to run is required", file=sys.stderr)
        return 2

    return run_with_timeout(timeout_seconds, command)


if __name__ == "__main__":
    raise SystemExit(main())
