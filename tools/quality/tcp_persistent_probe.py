#!/usr/bin/env python3
"""Sequential persistent Modbus TCP probe with diagnostics and trace dump."""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _ensure_import_paths() -> None:
    repo = _repo_root()
    if str(repo) not in sys.path:
        sys.path.insert(0, str(repo))


_ensure_import_paths()
from tools.topology.topology_uploader import ModbusTcpClient  # noqa: E402

READS_DEFAULT = "1264:32,1080:125,1205:35"
DIAG_BASE_DEFAULT = 1376
DIAG_QTY_DEFAULT = 32
TRACE_BASE_DEFAULT = 1552
TRACE_QTY_DEFAULT = 121
TRACE_DEPTH = 6
TRACE_ENTRY_REGS = 20

TRACE_EVENT_NAMES = {
    0: "none",
    1: "accept",
    2: "recv",
    3: "frame",
    4: "send",
    5: "malformed",
    6: "close",
}


@dataclass(frozen=True)
class ReadSpec:
    address: int
    qty: int


@dataclass(frozen=True)
class TraceEntry:
    seq: int
    event: int
    conn_index: int
    transaction_id: int
    rx_len_before: int
    rx_len_after: int
    mbap_length: int
    function_code: int
    start_reg: int
    qty: int
    tick_ms: int
    conn_ptr: int
    recv_err: int
    send_err: int
    io_len: int


def _u32(hi: int, lo: int) -> int:
    return ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)


def _i32(value: int) -> int:
    if value & 0x80000000:
        return value - 0x100000000
    return value


def _parse_reads(raw: str) -> list[ReadSpec]:
    specs: list[ReadSpec] = []
    for chunk in raw.split(","):
        token = chunk.strip()
        if not token:
            continue
        if ":" not in token:
            raise SystemExit(f"--reads: entry '{token}' must be address:qty")
        addr_s, qty_s = token.split(":", 1)
        try:
            address = int(addr_s, 0)
            qty = int(qty_s, 0)
        except ValueError as exc:
            raise SystemExit(f"--reads: invalid entry '{token}'") from exc
        if address < 0 or address > 0xFFFF:
            raise SystemExit(f"--reads: address out of range in '{token}'")
        if qty <= 0 or qty > 125:
            raise SystemExit(f"--reads: qty must be in [1..125] in '{token}'")
        specs.append(ReadSpec(address=address, qty=qty))
    if not specs:
        raise SystemExit("--reads must include at least one address:qty pair")
    return specs


def _next_transaction_id(client: object) -> int:
    raw = getattr(client, "_tx_id", 0)
    return raw if isinstance(raw, int) else 0


def _reconnect(client: ModbusTcpClient, downtime_s: float) -> None:
    try:
        client.close()
    except Exception:  # noqa: BLE001
        pass
    if downtime_s > 0.0:
        time.sleep(downtime_s)
    client.connect()


def _read_diag_snapshot(client: ModbusTcpClient, base: int, qty: int) -> dict[str, int]:
    regs = client.read_holding_registers(base, qty)
    if len(regs) != qty:
        raise RuntimeError(f"diag: expected {qty} regs, got {len(regs)}")
    if qty < 32:
        raise RuntimeError("diag: qty must be >= 32 to decode TCP counters")
    return {
        "accept_err": _u32(regs[20], regs[21]),
        "recv_timeout": _u32(regs[22], regs[23]),
        "stale_close": _u32(regs[24], regs[25]),
        "malformed_mbap": _u32(regs[26], regs[27]),
        "send_err": _u32(regs[28], regs[29]),
        "last_err": _i32(_u32(regs[30], regs[31])),
    }


def _format_diag(diag: dict[str, int]) -> str:
    return (
        f"acceptErr={diag['accept_err']} recvTimeout={diag['recv_timeout']} "
        f"staleClose={diag['stale_close']} malformedMbap={diag['malformed_mbap']} "
        f"sendErr={diag['send_err']} lastErr={diag['last_err']}"
    )


def _read_trace_entries(client: ModbusTcpClient, base: int, qty: int) -> list[TraceEntry]:
    regs = client.read_holding_registers(base, qty)
    if len(regs) != qty:
        raise RuntimeError(f"trace: expected {qty} regs, got {len(regs)}")
    if qty < 1:
        raise RuntimeError("trace: qty must be >= 1")

    count = min(regs[0], TRACE_DEPTH)
    entries: list[TraceEntry] = []
    for idx in range(count):
        off = 1 + (idx * TRACE_ENTRY_REGS)
        if off + TRACE_ENTRY_REGS > len(regs):
            break
        entries.append(
            TraceEntry(
                seq=regs[off + 0],
                event=regs[off + 1],
                conn_index=regs[off + 2],
                transaction_id=regs[off + 3],
                rx_len_before=regs[off + 4],
                rx_len_after=regs[off + 5],
                mbap_length=regs[off + 6],
                function_code=regs[off + 7],
                start_reg=regs[off + 8],
                qty=regs[off + 9],
                tick_ms=_u32(regs[off + 10], regs[off + 11]),
                conn_ptr=_u32(regs[off + 12], regs[off + 13]),
                recv_err=_i32(_u32(regs[off + 14], regs[off + 15])),
                send_err=_i32(_u32(regs[off + 16], regs[off + 17])),
                io_len=_u32(regs[off + 18], regs[off + 19]),
            )
        )
    return entries


def _print_trace(entries: Sequence[TraceEntry]) -> None:
    if not entries:
        print("[trace] empty")
        return
    for entry in entries:
        event_name = TRACE_EVENT_NAMES.get(entry.event, f"event_{entry.event}")
        print(
            f"[trace] seq={entry.seq} tick_ms={entry.tick_ms} event={event_name} "
            f"conn_index={entry.conn_index} conn_ptr=0x{entry.conn_ptr:08X} tx_id={entry.transaction_id} "
            f"rx_before={entry.rx_len_before} rx_after={entry.rx_len_after} mbap_len={entry.mbap_length} "
            f"fc={entry.function_code} start_reg={entry.start_reg} qty={entry.qty} "
            f"recv_err={entry.recv_err} send_err={entry.send_err} io_len={entry.io_len}"
        )


def run_probe(args: argparse.Namespace) -> int:
    request_id = 0
    timeouts = 0
    total_reads = len(args.reads) * args.cycles

    print(
        f"[start] host={args.host}:{args.port} unit={args.unit_id} cycles={args.cycles} "
        f"requests={total_reads} reconnect_per_request={args.reconnect_per_request} gap_ms={args.gap_ms}"
    )
    print(f"[reads] {', '.join(f'{spec.address}:{spec.qty}' for spec in args.reads)}")

    with ModbusTcpClient(args.host, args.port, args.unit_id, args.timeout_s) as client:
        if not args.skip_diag_before:
            diag = _read_diag_snapshot(client, args.diag_base, args.diag_qty)
            print(f"[diag before] {_format_diag(diag)}")

        for cycle in range(1, args.cycles + 1):
            for spec in args.reads:
                request_id += 1
                if args.reconnect_per_request and request_id > 1:
                    _reconnect(client, args.reconnect_downtime_s)

                tx_id = _next_transaction_id(client)
                started = time.monotonic()
                try:
                    regs = client.read_holding_registers(spec.address, spec.qty)
                    duration_ms = (time.monotonic() - started) * 1000.0
                    print(
                        f"[req {request_id}] OK cycle={cycle} tx_id={tx_id} "
                        f"fc=3 addr={spec.address} qty={spec.qty} dur_ms={duration_ms:.1f} words={len(regs)}"
                    )
                except Exception as exc:  # noqa: BLE001
                    duration_ms = (time.monotonic() - started) * 1000.0
                    timeouts += 1
                    print(
                        f"[req {request_id}] FAIL cycle={cycle} tx_id={tx_id} "
                        f"fc=3 addr={spec.address} qty={spec.qty} dur_ms={duration_ms:.1f} err={exc}"
                    )

                    if not args.skip_diag_after_timeout or not args.skip_trace_after_timeout:
                        try:
                            _reconnect(client, args.reconnect_downtime_s)
                        except Exception as reconnect_exc:  # noqa: BLE001
                            print(f"[timeout reconnect] FAIL err={reconnect_exc}")
                        else:
                            if not args.skip_diag_after_timeout:
                                try:
                                    diag = _read_diag_snapshot(client, args.diag_base, args.diag_qty)
                                    print(f"[diag after timeout] {_format_diag(diag)}")
                                except Exception as diag_exc:  # noqa: BLE001
                                    print(f"[diag after timeout] FAIL err={diag_exc}")
                            if not args.skip_trace_after_timeout:
                                try:
                                    entries = _read_trace_entries(client, args.trace_base, args.trace_qty)
                                    _print_trace(entries)
                                except Exception as trace_exc:  # noqa: BLE001
                                    print(f"[trace after timeout] FAIL err={trace_exc}")

                    if not args.continue_after_timeout:
                        print(f"[summary] requests={request_id} timeouts={timeouts}")
                        return 2

                if args.gap_ms > 0:
                    time.sleep(args.gap_ms / 1000.0)

    print(f"[summary] requests={request_id} timeouts={timeouts}")
    return 0 if timeouts == 0 else 2


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Controller IPv4/hostname")
    parser.add_argument("--port", type=int, default=502, help="Modbus TCP port")
    parser.add_argument("--unit-id", type=int, default=1, help="Modbus unit id")
    parser.add_argument("--timeout-s", type=float, default=4.0, help="Socket timeout in seconds")
    parser.add_argument("--cycles", type=int, default=50, help="Number of read cycles to execute")
    parser.add_argument("--reads", default=READS_DEFAULT, help="Comma-separated FC3 reads as address:qty")
    parser.add_argument("--gap-ms", type=int, default=0, help="Sleep between sequential requests")
    parser.add_argument("--reconnect-per-request", action="store_true", help="Close/reopen socket before every request")
    parser.add_argument("--reconnect-downtime-s", type=float, default=0.2, help="Disconnect pause before reconnect")
    parser.add_argument("--continue-after-timeout", action="store_true", help="Reconnect and continue after timeout")
    parser.add_argument("--diag-base", type=int, default=DIAG_BASE_DEFAULT, help="Diagnostics window base address")
    parser.add_argument("--diag-qty", type=int, default=DIAG_QTY_DEFAULT, help="Diagnostics FC3 register count")
    parser.add_argument("--trace-base", type=int, default=TRACE_BASE_DEFAULT, help="TCP trace window base address")
    parser.add_argument("--trace-qty", type=int, default=TRACE_QTY_DEFAULT, help="TCP trace FC3 register count")
    parser.add_argument("--skip-diag-before", action="store_true", help="Do not read diagnostics before the run")
    parser.add_argument("--skip-diag-after-timeout", action="store_true", help="Do not read diagnostics after timeout")
    parser.add_argument("--skip-trace-after-timeout", action="store_true", help="Do not read TCP trace after timeout")
    args = parser.parse_args(argv)

    if args.timeout_s <= 0:
        raise SystemExit("--timeout-s must be > 0")
    if args.cycles <= 0:
        raise SystemExit("--cycles must be > 0")
    if args.gap_ms < 0:
        raise SystemExit("--gap-ms must be >= 0")
    if args.reconnect_downtime_s < 0:
        raise SystemExit("--reconnect-downtime-s must be >= 0")
    if args.diag_qty < 32 or args.diag_qty > 125:
        raise SystemExit("--diag-qty must be in [32..125]")
    if args.trace_qty <= 0 or args.trace_qty > 125:
        raise SystemExit("--trace-qty must be in [1..125]")

    args.reads = _parse_reads(args.reads)
    return args


def main() -> int:
    args = parse_args()
    return run_probe(args)


if __name__ == "__main__":
    raise SystemExit(main())
