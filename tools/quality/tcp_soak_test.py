#!/usr/bin/env python3
"""Long-running Modbus TCP soak test with reconnect flap and optional topology upload."""

from __future__ import annotations

import argparse
import random
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _ensure_import_paths() -> None:
    repo = _repo_root()
    if str(repo) not in sys.path:
        sys.path.insert(0, str(repo))


_ensure_import_paths()
from tools.topology.topology_uploader import ModbusTcpClient  # noqa: E402


@dataclass
class SoakStats:
    cycles_total: int = 0
    reads_ok: int = 0
    reads_failed: int = 0
    retries_ok: int = 0
    retries_failed: int = 0
    reconnect_forced: int = 0
    reconnect_random: int = 0
    upload_runs: int = 0
    upload_failed: int = 0


def _u32(hi: int, lo: int) -> int:
    return ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)


def _diag_snapshot(diag_regs: List[int]) -> str:
    if len(diag_regs) < 32:
        return "diag=short"
    accept_err = _u32(diag_regs[20], diag_regs[21])
    recv_timeout = _u32(diag_regs[22], diag_regs[23])
    stale_close = _u32(diag_regs[24], diag_regs[25])
    malformed = _u32(diag_regs[26], diag_regs[27])
    send_err = _u32(diag_regs[28], diag_regs[29])
    last_err = _u32(diag_regs[30], diag_regs[31])
    return (
        f"diag acceptErr={accept_err} recvTimeout={recv_timeout} staleClose={stale_close} "
        f"malformed={malformed} sendErr={send_err} lastErr=0x{last_err:08X}"
    )


def _background_upload(stop_evt: threading.Event,
                       stats: SoakStats,
                       host: str,
                       port: int,
                       unit_id: int,
                       chunks: Path,
                       upload_interval_s: float,
                       io_retries: int) -> None:
    script = _repo_root() / "tools" / "topology" / "topology_uploader.py"
    cmd = [
        sys.executable,
        str(script),
        "--host",
        host,
        "--port",
        str(port),
        "--unit-id",
        str(unit_id),
        "--chunks",
        str(chunks),
        "--io-retries",
        str(io_retries),
    ]

    while not stop_evt.is_set():
        stats.upload_runs += 1
        started = time.time()
        try:
            result = subprocess.run(
                cmd,
                check=False,
                cwd=str(_repo_root()),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            elapsed = time.time() - started
            if result.returncode != 0:
                stats.upload_failed += 1
                print(f"[upload] FAIL rc={result.returncode} elapsed={elapsed:.1f}s")
                print(result.stdout.strip())
            else:
                print(f"[upload] OK elapsed={elapsed:.1f}s")
        except Exception as exc:  # noqa: BLE001
            stats.upload_failed += 1
            print(f"[upload] EXC {exc}")

        if stop_evt.wait(upload_interval_s):
            break


def _reconnect(client: ModbusTcpClient, downtime_s: float) -> None:
    try:
        client.close()
    except Exception:  # noqa: BLE001
        pass
    if downtime_s > 0.0:
        time.sleep(downtime_s)
    client.connect()


def run_soak(args: argparse.Namespace) -> int:
    end_at = time.monotonic() + (args.duration_h * 3600.0)
    stats = SoakStats()
    stop_evt = threading.Event()
    uploader: threading.Thread | None = None

    if args.chunks is not None:
        uploader = threading.Thread(
            target=_background_upload,
            args=(
                stop_evt,
                stats,
                args.host,
                args.port,
                args.unit_id,
                args.chunks,
                args.upload_interval_s,
                args.io_retries,
            ),
            daemon=True,
        )
        uploader.start()

    print(
        f"[start] host={args.host}:{args.port} unit={args.unit_id} duration_h={args.duration_h} "
        f"poll_s={args.poll_s} points={args.points_base}+{args.points_qty}"
    )

    with ModbusTcpClient(args.host, args.port, args.unit_id, args.timeout_s) as client:
        cycle = 0
        while time.monotonic() < end_at:
            cycle_start = time.monotonic()
            cycle += 1
            stats.cycles_total = cycle

            try:
                if args.reconnect_every_cycles > 0 and cycle > 1 and (cycle % args.reconnect_every_cycles) == 0:
                    _reconnect(client, args.reconnect_downtime_s)
                    stats.reconnect_forced += 1
                elif args.reconnect_probability > 0.0 and random.random() < args.reconnect_probability:
                    _reconnect(client, args.reconnect_downtime_s)
                    stats.reconnect_random += 1

                _ = client.read_holding_registers(args.points_base, args.points_qty)
                stats.reads_ok += 1

                if args.diag_every_cycles > 0 and (cycle % args.diag_every_cycles) == 0:
                    diag = client.read_holding_registers(args.diag_base, args.diag_qty)
                    print(f"[cycle {cycle}] OK {_diag_snapshot(diag)}")
                else:
                    print(f"[cycle {cycle}] OK")

            except Exception as exc:  # noqa: BLE001
                stats.reads_failed += 1
                print(f"[cycle {cycle}] FAIL {exc}")
                try:
                    _reconnect(client, args.reconnect_downtime_s)
                    _ = client.read_holding_registers(args.points_base, args.points_qty)
                    stats.retries_ok += 1
                    print(f"[cycle {cycle}] RETRY_OK")
                except Exception as retry_exc:  # noqa: BLE001
                    stats.retries_failed += 1
                    print(f"[cycle {cycle}] RETRY_FAIL {retry_exc}")

            elapsed = time.monotonic() - cycle_start
            sleep_for = args.poll_s - elapsed
            if sleep_for > 0:
                time.sleep(sleep_for)

    stop_evt.set()
    if uploader is not None:
        uploader.join(timeout=5.0)

    print("[summary]")
    print(
        f"cycles={stats.cycles_total} reads_ok={stats.reads_ok} reads_failed={stats.reads_failed} "
        f"retry_ok={stats.retries_ok} retry_failed={stats.retries_failed}"
    )
    print(
        f"reconnect_forced={stats.reconnect_forced} reconnect_random={stats.reconnect_random} "
        f"upload_runs={stats.upload_runs} upload_failed={stats.upload_failed}"
    )
    return 0 if stats.retries_failed == 0 else 2


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Modbus TCP soak test for greenhouseOS")
    parser.add_argument("--host", required=True, help="Controller IPv4/hostname")
    parser.add_argument("--port", type=int, default=502, help="Modbus TCP port")
    parser.add_argument("--unit-id", type=int, default=1, help="Modbus unit id")
    parser.add_argument("--duration-h", type=float, default=24.0, help="Soak duration in hours")
    parser.add_argument("--poll-s", type=float, default=5.0, help="FC03 poll period in seconds")
    parser.add_argument("--timeout-s", type=float, default=4.0, help="socket timeout in seconds")
    parser.add_argument("--points-base", type=int, default=0, help="points FC03 start address")
    parser.add_argument("--points-qty", type=int, default=72, help="points FC03 register count")
    parser.add_argument("--diag-base", type=int, default=1376, help="diagnostics FC03 start address")
    parser.add_argument("--diag-qty", type=int, default=32, help="diagnostics FC03 register count")
    parser.add_argument("--diag-every-cycles", type=int, default=12, help="read diagnostics every N cycles")
    parser.add_argument("--reconnect-every-cycles", type=int, default=60, help="force reconnect every N cycles; 0=off")
    parser.add_argument("--reconnect-probability", type=float, default=0.0, help="random reconnect probability per cycle")
    parser.add_argument("--reconnect-downtime-s", type=float, default=0.2, help="disconnect pause before reconnect")
    parser.add_argument("--io-retries", type=int, default=3, help="topology uploader transport retries")
    parser.add_argument("--upload-interval-s", type=float, default=900.0, help="background upload period in seconds")
    parser.add_argument("--chunks", type=Path, default=None, help="optional topology chunks JSON for background uploads")
    args = parser.parse_args()

    if args.duration_h <= 0:
        raise SystemExit("--duration-h must be > 0")
    if args.poll_s <= 0:
        raise SystemExit("--poll-s must be > 0")
    if args.points_qty <= 0 or args.points_qty > 125:
        raise SystemExit("--points-qty must be in [1..125]")
    if args.diag_qty <= 0 or args.diag_qty > 125:
        raise SystemExit("--diag-qty must be in [1..125]")
    if args.reconnect_probability < 0.0 or args.reconnect_probability > 1.0:
        raise SystemExit("--reconnect-probability must be in [0..1]")
    if args.chunks is not None and not args.chunks.exists():
        raise SystemExit(f"chunks file not found: {args.chunks}")

    return args


def main() -> int:
    args = parse_args()
    return run_soak(args)


if __name__ == "__main__":
    raise SystemExit(main())
