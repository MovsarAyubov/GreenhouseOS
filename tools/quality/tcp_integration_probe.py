#!/usr/bin/env python3
"""Bench integration probe for SCADA->TCP->map runtime contract."""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _ensure_import_paths() -> None:
    repo = _repo_root()
    if str(repo) not in sys.path:
        sys.path.insert(0, str(repo))


_ensure_import_paths()
from tools.topology.topology_uploader import (  # noqa: E402
    ModbusTcpClient,
    TOPOLOGY_UPLOAD_CHUNK_WORDS,
    TOPO_OFF_REQ_CHUNK_WORDS,
    TOPO_OFF_RESULT_CODE,
    TOPO_OFF_RESULT_TOKEN,
    TOPO_OFF_SUBMIT_TOKEN,
)

DIR_BASE_DEFAULT = 1264
DIR_REGS = 32
DIR_OFF_MAP_VERSION = 0
DIR_OFF_MAP_FLAGS = 1
DIR_OFF_CMD_BASE = 8
DIR_OFF_CMD_BLOCK_SIZE = 12

CMD_PAYLOAD_WORDS = 16
CMD_OFF_TARGET_SLAVE_ID = 0
CMD_OFF_TARGET_MODULE_ID = 1
CMD_OFF_CMD_PROFILE_ID = 2
CMD_OFF_PAYLOAD_LEN = 3
CMD_OFF_PAYLOAD_BASE = 4
CMD_OFF_TRIGGER = CMD_OFF_PAYLOAD_BASE + CMD_PAYLOAD_WORDS
CMD_OFF_LAST_APPLIED_TRIGGER = CMD_OFF_TRIGGER + 1
CMD_OFF_RESULT = CMD_OFF_TRIGGER + 2
CMD_OFF_IO_ERR = CMD_OFF_TRIGGER + 3

DCMD_RESULT_APPLIED = 2
DCMD_RESULT_REJECT_BUSY = 13
CFG_RESULT_REJECT_TOPOLOGY_BOUNDS = 21

TOPO_BASE_DEFAULT = 1408
DIAG_BASE_DEFAULT = 1376
POINTS_BASE_DEFAULT = 0
POINTS_QTY_DEFAULT = 72


@dataclass
class CommandResult:
    trigger: int
    result: int
    io_err: int


def _parse_u16_csv(raw: str, what: str) -> list[int]:
    values: list[int] = []
    for chunk in raw.split(","):
        s = chunk.strip()
        if not s:
            continue
        try:
            value = int(s, 0)
        except ValueError as exc:
            raise SystemExit(f"{what}: invalid integer '{s}'") from exc
        if value < 0 or value > 0xFFFF:
            raise SystemExit(f"{what}: value {value} out of range [0..65535]")
        values.append(value)
    return values


def _must_read_registers(client: Any, address: int, qty: int, what: str) -> list[int]:
    regs = client.read_holding_registers(address, qty)
    if len(regs) != qty:
        raise RuntimeError(f"{what}: expected {qty} regs, got {len(regs)}")
    return regs


def _write_command_request(client: Any,
                           cmd_base: int,
                           slave_id: int,
                           module_id: int,
                           cmd_profile_id: int,
                           payload: Sequence[int]) -> None:
    if len(payload) == 0 or len(payload) > CMD_PAYLOAD_WORDS:
        raise RuntimeError(f"payload length must be in [1..{CMD_PAYLOAD_WORDS}]")

    client.write_single_register(cmd_base + CMD_OFF_TARGET_SLAVE_ID, slave_id)
    client.write_single_register(cmd_base + CMD_OFF_TARGET_MODULE_ID, module_id)
    client.write_single_register(cmd_base + CMD_OFF_CMD_PROFILE_ID, cmd_profile_id)
    client.write_single_register(cmd_base + CMD_OFF_PAYLOAD_LEN, len(payload))
    client.write_multiple_registers(cmd_base + CMD_OFF_PAYLOAD_BASE, payload)


def submit_command_and_wait(client: Any,
                            cmd_base: int,
                            slave_id: int,
                            module_id: int,
                            cmd_profile_id: int,
                            payload: Sequence[int],
                            trigger: int,
                            timeout_s: float,
                            poll_interval_s: float) -> CommandResult:
    _write_command_request(client, cmd_base, slave_id, module_id, cmd_profile_id, payload)
    client.write_single_register(cmd_base + CMD_OFF_TRIGGER, trigger & 0xFFFF)

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        last_trigger, result, io_err = _must_read_registers(
            client,
            cmd_base + CMD_OFF_LAST_APPLIED_TRIGGER,
            3,
            "command result",
        )
        if last_trigger == (trigger & 0xFFFF):
            return CommandResult(trigger=last_trigger, result=result, io_err=io_err)
        time.sleep(poll_interval_s)

    raise RuntimeError(f"timeout waiting for command trigger={trigger}")


def probe_busy_reject(client: Any,
                      cmd_base: int,
                      slave_id: int,
                      module_id: int,
                      cmd_profile_id: int,
                      payload: Sequence[int],
                      trigger_a: int,
                      trigger_b: int) -> bool:
    _write_command_request(client, cmd_base, slave_id, module_id, cmd_profile_id, payload)
    client.write_single_register(cmd_base + CMD_OFF_TRIGGER, trigger_a & 0xFFFF)
    client.write_single_register(cmd_base + CMD_OFF_TRIGGER, trigger_b & 0xFFFF)
    result = _must_read_registers(client, cmd_base + CMD_OFF_RESULT, 1, "busy result")[0]
    return result == DCMD_RESULT_REJECT_BUSY


def probe_invalid_topology_bounds(client: Any,
                                  topo_base: int,
                                  submit_token: int,
                                  timeout_s: float,
                                  poll_interval_s: float) -> bool:
    client.write_single_register(topo_base + TOPO_OFF_REQ_CHUNK_WORDS, TOPOLOGY_UPLOAD_CHUNK_WORDS + 1)
    client.write_single_register(topo_base + TOPO_OFF_SUBMIT_TOKEN, submit_token & 0xFFFF)

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        result_code, result_token = _must_read_registers(
            client,
            topo_base + TOPO_OFF_RESULT_CODE,
            2,
            "topology result",
        )
        if result_token == (submit_token & 0xFFFF):
            return result_code == CFG_RESULT_REJECT_TOPOLOGY_BOUNDS
        time.sleep(poll_interval_s)
    raise RuntimeError(f"timeout waiting for topology reject token={submit_token}")


def run_probe(args: argparse.Namespace) -> int:
    with ModbusTcpClient(args.host, args.port, args.unit_id, args.timeout_s) as client:
        directory = _must_read_registers(client, args.dir_base, DIR_REGS, "directory")
        map_version = directory[DIR_OFF_MAP_VERSION]
        map_flags = directory[DIR_OFF_MAP_FLAGS]
        cmd_base = directory[DIR_OFF_CMD_BASE]
        cmd_block_size = directory[DIR_OFF_CMD_BLOCK_SIZE]

        print(
            f"[dir] map_version={map_version} map_flags=0x{map_flags:04X} "
            f"cmd_base={cmd_base} cmd_block_size={cmd_block_size}"
        )
        if map_version != args.expected_map_version:
            raise RuntimeError(
                f"map version mismatch: expected={args.expected_map_version} got={map_version}"
            )
        if (map_flags & 0x0001) == 0:
            raise RuntimeError("directory validity bit is not set")
        if cmd_block_size < (CMD_OFF_IO_ERR + 1):
            raise RuntimeError(f"command block is too small: {cmd_block_size}")

        _ = _must_read_registers(client, args.points_base, args.points_qty, "points smoke")
        _ = _must_read_registers(client, args.diag_base, args.diag_qty, "diag smoke")
        print("[smoke] points and diagnostics FC03 read OK")

        if args.run_command:
            result = submit_command_and_wait(
                client=client,
                cmd_base=cmd_base,
                slave_id=args.cmd_slave_id,
                module_id=args.cmd_module_id,
                cmd_profile_id=args.cmd_profile_id,
                payload=args.cmd_payload,
                trigger=args.cmd_trigger,
                timeout_s=args.command_timeout_s,
                poll_interval_s=args.poll_interval_s,
            )
            print(
                f"[command] trigger={result.trigger} result={result.result} io_err={result.io_err}"
            )
            if result.result not in args.accept_command_results:
                raise RuntimeError(
                    f"command result unexpected: got={result.result} "
                    f"expected_any={args.accept_command_results}"
                )

        if args.run_busy_probe:
            ok = probe_busy_reject(
                client=client,
                cmd_base=cmd_base,
                slave_id=args.cmd_slave_id,
                module_id=args.cmd_module_id,
                cmd_profile_id=args.cmd_profile_id,
                payload=args.cmd_payload,
                trigger_a=args.busy_trigger_a,
                trigger_b=args.busy_trigger_b,
            )
            print(f"[busy] reject_busy_detected={ok}")
            if not ok:
                raise RuntimeError("busy probe did not observe REJECT_BUSY")

        if args.run_invalid_topology_probe:
            ok = probe_invalid_topology_bounds(
                client=client,
                topo_base=args.topo_base,
                submit_token=args.topo_probe_token,
                timeout_s=args.command_timeout_s,
                poll_interval_s=args.poll_interval_s,
            )
            print(f"[topology] reject_bounds_detected={ok}")
            if not ok:
                raise RuntimeError("invalid topology probe did not observe REJECT_TOPOLOGY_BOUNDS")

    print("[summary] probe passed")
    return 0


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Controller IPv4/hostname")
    parser.add_argument("--port", type=int, default=502, help="Modbus TCP port")
    parser.add_argument("--unit-id", type=int, default=1, help="Modbus unit id")
    parser.add_argument("--timeout-s", type=float, default=3.0, help="socket timeout in seconds")

    parser.add_argument("--dir-base", type=int, default=DIR_BASE_DEFAULT, help="directory start address")
    parser.add_argument("--expected-map-version", type=int, default=4, help="expected MAP_VERSION")
    parser.add_argument("--points-base", type=int, default=POINTS_BASE_DEFAULT, help="points FC03 start address")
    parser.add_argument("--points-qty", type=int, default=POINTS_QTY_DEFAULT, help="points FC03 register count")
    parser.add_argument("--diag-base", type=int, default=DIAG_BASE_DEFAULT, help="diagnostics FC03 start address")
    parser.add_argument("--diag-qty", type=int, default=32, help="diagnostics FC03 register count")

    parser.add_argument("--run-command", action="store_true", help="submit one data-driven command")
    parser.add_argument("--cmd-slave-id", type=int, default=1, help="command request slave_id")
    parser.add_argument("--cmd-module-id", type=int, default=101, help="command request module_id")
    parser.add_argument("--cmd-profile-id", type=int, default=5001, help="command request cmd_profile_id")
    parser.add_argument("--cmd-payload", default="1", help="comma-separated payload words, e.g. '11,22,33'")
    parser.add_argument("--cmd-trigger", type=int, default=100, help="trigger token for command submit")
    parser.add_argument(
        "--accept-command-results",
        default="2",
        help="comma-separated accepted command result codes (default: 2)",
    )
    parser.add_argument("--command-timeout-s", type=float, default=8.0, help="command/topology poll timeout")
    parser.add_argument("--poll-interval-s", type=float, default=0.2, help="poll interval for result wait")

    parser.add_argument("--run-busy-probe", action="store_true", help="check single in-flight reject busy behavior")
    parser.add_argument("--busy-trigger-a", type=int, default=200, help="first trigger for busy probe")
    parser.add_argument("--busy-trigger-b", type=int, default=201, help="second trigger for busy probe")

    parser.add_argument(
        "--run-invalid-topology-probe",
        action="store_true",
        help="submit out-of-bounds topology chunk_words and expect reject bounds",
    )
    parser.add_argument("--topo-base", type=int, default=TOPO_BASE_DEFAULT, help="topology upload base address")
    parser.add_argument("--topo-probe-token", type=int, default=300, help="submit token for topology fault probe")
    return parser


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = _build_parser()
    args = parser.parse_args(argv)

    if args.points_qty <= 0 or args.points_qty > 125:
        raise SystemExit("--points-qty must be in [1..125]")
    if args.diag_qty <= 0 or args.diag_qty > 125:
        raise SystemExit("--diag-qty must be in [1..125]")
    if args.command_timeout_s <= 0:
        raise SystemExit("--command-timeout-s must be > 0")
    if args.poll_interval_s <= 0:
        raise SystemExit("--poll-interval-s must be > 0")

    args.cmd_payload = _parse_u16_csv(args.cmd_payload, "--cmd-payload")
    if not args.cmd_payload:
        raise SystemExit("--cmd-payload must include at least one value")

    args.accept_command_results = _parse_u16_csv(args.accept_command_results, "--accept-command-results")
    if not args.accept_command_results:
        raise SystemExit("--accept-command-results must include at least one value")

    if args.run_busy_probe and (len(args.cmd_payload) == 0):
        raise SystemExit("--run-busy-probe requires non-empty --cmd-payload")

    return args


def main() -> int:
    args = parse_args()
    try:
        return run_probe(args)
    except Exception as exc:  # noqa: BLE001
        print(f"[error] {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
