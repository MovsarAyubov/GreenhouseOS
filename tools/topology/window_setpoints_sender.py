#!/usr/bin/env python3
"""Submit window setpoint blocks through the greenhouseOS command ingress."""

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


DIR_BASE = 1264
DIR_REGS = 13
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

RESULT_NAMES = {
    0: "IDLE",
    1: "QUEUED",
    2: "APPLIED",
    10: "REJECT_BOUNDS",
    11: "REJECT_TOPOLOGY",
    12: "REJECT_FC",
    13: "REJECT_BUSY",
    14: "REJECT_PARTIAL",
    15: "TRANSPORT_FAIL",
    16: "ACK_FAIL",
}

IO_ERR_NAMES = {
    0: "NONE",
    1: "TIMEOUT",
    2: "CRC",
    3: "FRAME",
    4: "UART",
}

PROFILE_IDS = {
    1: {
        "manual": 5002,
        "core": 5004,
        "auto": 5005,
        "weather": 5006,
        "step": 5007,
        "curtain_core": 5015,
        "curtain_rad_temp": 5016,
        "curtain_hum": 5017,
        "curtain_fault_reset": 5018,
        "greenhouse_targets": 5019,
    },
    2: {
        "manual": 5008,
        "core": 5009,
        "auto": 5010,
        "weather": 5011,
        "step": 5012,
        "curtain_core": 5020,
        "curtain_rad_temp": 5021,
        "curtain_hum": 5022,
        "curtain_fault_reset": 5023,
        "greenhouse_targets": 5024,
    },
}

BLOCK_WORDS = {
    "manual": 2,
    "core": 16,
    "auto": 16,
    "weather": 2,
    "step": 6,
    "curtain_core": 8,
    "curtain_rad_temp": 10,
    "curtain_hum": 6,
    "curtain_fault_reset": 1,
    "greenhouse_targets": 2,
}


@dataclass(frozen=True)
class SubmitResult:
    trigger: int
    result: int
    io_err: int


def _parse_words(raw: str, expected: int) -> list[int]:
    words: list[int] = []
    for chunk in raw.split(","):
        text = chunk.strip()
        if not text:
            continue
        value = int(text, 0)
        if value < 0 or value > 0xFFFF:
            raise ValueError(f"value {value} out of uint16 range")
        words.append(value)
    if len(words) != expected:
        raise ValueError(f"expected {expected} payload words, got {len(words)}")
    return words


def _next_trigger() -> int:
    token = int(time.time() * 1000) & 0xFFFF
    return token if token != 0 else 1


def _read_cmd_base(client: ModbusTcpClient) -> tuple[int, int]:
    directory = client.read_holding_registers(DIR_BASE, DIR_REGS)
    return directory[DIR_OFF_CMD_BASE], directory[DIR_OFF_CMD_BLOCK_SIZE]


def _submit(client: ModbusTcpClient,
            cmd_base: int,
            slave_id: int,
            module_id: int,
            profile_id: int,
            payload: Sequence[int],
            trigger: int,
            timeout_s: float,
            poll_interval_s: float) -> SubmitResult:
    if not payload or len(payload) > CMD_PAYLOAD_WORDS:
        raise RuntimeError(f"payload length must be in [1..{CMD_PAYLOAD_WORDS}]")

    client.write_single_register(cmd_base + CMD_OFF_TARGET_SLAVE_ID, slave_id)
    client.write_single_register(cmd_base + CMD_OFF_TARGET_MODULE_ID, module_id)
    client.write_single_register(cmd_base + CMD_OFF_CMD_PROFILE_ID, profile_id)
    client.write_single_register(cmd_base + CMD_OFF_PAYLOAD_LEN, len(payload))
    client.write_multiple_registers(cmd_base + CMD_OFF_PAYLOAD_BASE, payload)
    client.write_single_register(cmd_base + CMD_OFF_TRIGGER, trigger)

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        last_trigger, result, io_err = client.read_holding_registers(
            cmd_base + CMD_OFF_LAST_APPLIED_TRIGGER,
            3,
        )
        if last_trigger == trigger:
            return SubmitResult(last_trigger, result, io_err)
        time.sleep(poll_interval_s)
    raise TimeoutError(f"timeout waiting for trigger={trigger}")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=502)
    parser.add_argument("--unit-id", type=int, default=1)
    parser.add_argument("--timeout-s", type=float, default=5.0)
    parser.add_argument("--command-timeout-s", type=float, default=8.0)
    parser.add_argument("--poll-interval-s", type=float, default=0.2)
    parser.add_argument("--zone", type=int, default=1, choices=sorted(PROFILE_IDS))
    parser.add_argument("--module-id", type=int, default=None)
    parser.add_argument("--slave-id", type=int, default=None)
    parser.add_argument("--block", required=True, choices=sorted(BLOCK_WORDS))
    parser.add_argument("--payload", required=True, help="comma-separated uint16 values for the selected block")
    parser.add_argument("--trigger", type=int, default=None)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    module_id = args.module_id if args.module_id is not None else 100 + args.zone
    slave_id = args.slave_id if args.slave_id is not None else args.zone
    profile_id = PROFILE_IDS[args.zone][args.block]
    payload = _parse_words(args.payload, BLOCK_WORDS[args.block])
    trigger = args.trigger if args.trigger is not None else _next_trigger()
    if trigger <= 0 or trigger > 0xFFFF:
        raise SystemExit("--trigger must be in [1..65535]")

    with ModbusTcpClient(args.host, args.port, args.unit_id, args.timeout_s) as client:
        cmd_base, cmd_block_size = _read_cmd_base(client)
        if cmd_block_size < CMD_OFF_IO_ERR + 1:
            raise RuntimeError(f"command block too small: {cmd_block_size}")
        result = _submit(
            client=client,
            cmd_base=cmd_base,
            slave_id=slave_id,
            module_id=module_id,
            profile_id=profile_id,
            payload=payload,
            trigger=trigger,
            timeout_s=args.command_timeout_s,
            poll_interval_s=args.poll_interval_s,
        )

    print(
        "trigger=%d result=%d(%s) io_err=%d(%s)"
        % (
            result.trigger,
            result.result,
            RESULT_NAMES.get(result.result, "UNKNOWN"),
            result.io_err,
            IO_ERR_NAMES.get(result.io_err, "UNKNOWN"),
        )
    )
    return 0 if result.result == 2 and result.io_err == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
