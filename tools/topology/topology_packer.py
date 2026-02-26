#!/usr/bin/env python3
"""Pack topology_config v2 JSON into binary payload and Modbus upload chunks."""

from __future__ import annotations

import argparse
import json
import math
import struct
import zlib
from pathlib import Path
from typing import Any, Dict, Iterable, List

GH_TOPOLOGY_V2_MAGIC = 0x324F5054
GH_TOPOLOGY_V2_VERSION_MAJOR = 2

GH_TOPOLOGY_V2_MAX_MODULES = 48
GH_TOPOLOGY_V2_MAX_REQ_PROFILES = 192
GH_TOPOLOGY_V2_MAX_POINTS = 640
GH_TOPOLOGY_V2_MAX_COMMANDS = 192
GH_TOPOLOGY_V2_MAX_POLICIES = 64

BUS_RTU1 = 1
BUS_RTU2 = 2
BUS_TCP = 3
POLICY_ACTION_MAX = 2

TOPOLOGY_MAX_BLOB_SIZE = 4096
TOPOLOGY_UPLOAD_CHUNK_WORDS = 120
TOPOLOGY_UPLOAD_CHUNK_BYTES = TOPOLOGY_UPLOAD_CHUNK_WORDS * 2

TOPOLOGY_UPLOAD_FLAG_COMMIT = 0x0001
TOPOLOGY_UPLOAD_FLAG_RESET = 0x0002

HEADER_FMT = "<IHHIIIIIHHHHHHIIIIIII"
MODULE_FMT = "<HBBBBHHHHHHHIII"
REQUEST_FMT = "<HHBBHHHHBBHHH"
POINT_FMT = "<HHHHBbBBHHhh"
COMMAND_FMT = "<HHBBHHHHH"
POLICY_FMT = "<HBBBBHHHH"

HEADER_SIZE = struct.calcsize(HEADER_FMT)


class TopologyPackError(ValueError):
    """Raised when input topology cannot be packed."""


def _parse_int(value: Any, name: str) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        text = value.strip().lower()
        base = 10
        if text.startswith("0x"):
            base = 16
        try:
            return int(text, base)
        except ValueError as exc:
            raise TopologyPackError(f"{name}: invalid integer '{value}'") from exc
    raise TopologyPackError(f"{name}: integer expected")


def _range_check(value: int, min_val: int, max_val: int, name: str) -> int:
    if value < min_val or value > max_val:
        raise TopologyPackError(f"{name}: value {value} out of range [{min_val}..{max_val}]")
    return value


def _u8(value: Any, name: str) -> int:
    return _range_check(_parse_int(value, name), 0, 0xFF, name)


def _u16(value: Any, name: str) -> int:
    return _range_check(_parse_int(value, name), 0, 0xFFFF, name)


def _u32(value: Any, name: str) -> int:
    return _range_check(_parse_int(value, name), 0, 0xFFFFFFFF, name)


def _i8(value: Any, name: str) -> int:
    return _range_check(_parse_int(value, name), -128, 127, name)


def _i16(value: Any, name: str) -> int:
    return _range_check(_parse_int(value, name), -32768, 32767, name)


def _expect_list(config: Dict[str, Any], key: str) -> List[Dict[str, Any]]:
    raw = config.get(key, [])
    if not isinstance(raw, list):
        raise TopologyPackError(f"{key}: list expected")
    for idx, item in enumerate(raw):
        if not isinstance(item, dict):
            raise TopologyPackError(f"{key}[{idx}]: object expected")
    return raw


def _pack_modules(items: List[Dict[str, Any]]) -> bytes:
    rows = []
    for idx, item in enumerate(items):
        prefix = f"modules[{idx}]"
        rows.append(
            struct.pack(
                MODULE_FMT,
                _u16(item["module_id"], f"{prefix}.module_id"),
                _u8(item["module_type"], f"{prefix}.module_type"),
                _u8(item["bus_type"], f"{prefix}.bus_type"),
                _u8(item["bus_index"], f"{prefix}.bus_index"),
                _u8(item["slave_id"], f"{prefix}.slave_id"),
                _u16(item["zone_id"], f"{prefix}.zone_id"),
                _u16(item["req_first"], f"{prefix}.req_first"),
                _u16(item["req_count"], f"{prefix}.req_count"),
                _u16(item["cmd_first"], f"{prefix}.cmd_first"),
                _u16(item["cmd_count"], f"{prefix}.cmd_count"),
                _u16(item["offline_reprobe_ms"], f"{prefix}.offline_reprobe_ms"),
                _u16(item["heartbeat_timeout_ms"], f"{prefix}.heartbeat_timeout_ms"),
                _u32(item["capability_mask"], f"{prefix}.capability_mask"),
                _u32(item["user_param0"], f"{prefix}.user_param0"),
                _u32(item["user_param1"], f"{prefix}.user_param1"),
            )
        )
    return b"".join(rows)


def _pack_requests(items: List[Dict[str, Any]]) -> bytes:
    rows = []
    for idx, item in enumerate(items):
        prefix = f"requests[{idx}]"
        rows.append(
            struct.pack(
                REQUEST_FMT,
                _u16(item["req_id"], f"{prefix}.req_id"),
                _u16(item["module_id"], f"{prefix}.module_id"),
                _u8(item["fc"], f"{prefix}.fc"),
                _u8(item["priority"], f"{prefix}.priority"),
                _u16(item["start_reg"], f"{prefix}.start_reg"),
                _u16(item["reg_count"], f"{prefix}.reg_count"),
                _u16(item["period_ms"], f"{prefix}.period_ms"),
                _u16(item["timeout_ms"], f"{prefix}.timeout_ms"),
                _u8(item["retries"], f"{prefix}.retries"),
                _u8(item["backoff_ms"], f"{prefix}.backoff_ms"),
                _u16(item["point_first"], f"{prefix}.point_first"),
                _u16(item["point_count"], f"{prefix}.point_count"),
                _u16(item["flags"], f"{prefix}.flags"),
            )
        )
    return b"".join(rows)


def _pack_points(items: List[Dict[str, Any]]) -> bytes:
    rows = []
    for idx, item in enumerate(items):
        prefix = f"points[{idx}]"
        rows.append(
            struct.pack(
                POINT_FMT,
                _u16(item["point_id"], f"{prefix}.point_id"),
                _u16(item["module_id"], f"{prefix}.module_id"),
                _u16(item["req_id"], f"{prefix}.req_id"),
                _u16(item["reg_offset"], f"{prefix}.reg_offset"),
                _u8(item["point_type"], f"{prefix}.point_type"),
                _i8(item["scale_pow10"], f"{prefix}.scale_pow10"),
                _u8(item["bit_index"], f"{prefix}.bit_index"),
                _u8(item["quality_policy"], f"{prefix}.quality_policy"),
                _u16(item["publish_index"], f"{prefix}.publish_index"),
                _u16(item["stale_timeout_s"], f"{prefix}.stale_timeout_s"),
                _i16(item["alarm_low"], f"{prefix}.alarm_low"),
                _i16(item["alarm_high"], f"{prefix}.alarm_high"),
            )
        )
    return b"".join(rows)


def _pack_commands(items: List[Dict[str, Any]]) -> bytes:
    rows = []
    for idx, item in enumerate(items):
        prefix = f"commands[{idx}]"
        rows.append(
            struct.pack(
                COMMAND_FMT,
                _u16(item["cmd_id"], f"{prefix}.cmd_id"),
                _u16(item["module_id"], f"{prefix}.module_id"),
                _u8(item["fc"], f"{prefix}.fc"),
                _u8(item["retries"], f"{prefix}.retries"),
                _u16(item["start_reg"], f"{prefix}.start_reg"),
                _u16(item["max_reg_count"], f"{prefix}.max_reg_count"),
                _u16(item["timeout_ms"], f"{prefix}.timeout_ms"),
                _u16(item["ack_point_id"], f"{prefix}.ack_point_id"),
                _u16(item["flags"], f"{prefix}.flags"),
            )
        )
    return b"".join(rows)


def _pack_policies(items: List[Dict[str, Any]]) -> bytes:
    rows = []
    for idx, item in enumerate(items):
        prefix = f"policies[{idx}]"
        rows.append(
            struct.pack(
                POLICY_FMT,
                _u16(item["module_id"], f"{prefix}.module_id"),
                _u8(item["on_timeout"], f"{prefix}.on_timeout"),
                _u8(item["on_crc_error"], f"{prefix}.on_crc_error"),
                _u8(item["on_link_loss"], f"{prefix}.on_link_loss"),
                _u8(item.get("reserved0", 0), f"{prefix}.reserved0"),
                _u16(item["max_consecutive_fail"], f"{prefix}.max_consecutive_fail"),
                _u16(item["recover_good_cycles"], f"{prefix}.recover_good_cycles"),
                _u16(item["safe_profile_id"], f"{prefix}.safe_profile_id"),
                _u16(item.get("reserved1", 0), f"{prefix}.reserved1"),
            )
        )
    return b"".join(rows)


def _ensure_unique(items: Iterable[Dict[str, Any]], key: str, group_name: str) -> None:
    seen = set()
    for idx, item in enumerate(items):
        value = _parse_int(item[key], f"{group_name}[{idx}].{key}")
        if value in seen:
            raise TopologyPackError(f"{group_name}: duplicate {key}={value}")
        seen.add(value)


def _ensure_references(modules: List[Dict[str, Any]],
                       requests: List[Dict[str, Any]],
                       points: List[Dict[str, Any]],
                       commands: List[Dict[str, Any]],
                       policies: List[Dict[str, Any]]) -> None:
    module_ids = {_parse_int(item["module_id"], "module_id") for item in modules}
    req_ids = {_parse_int(item["req_id"], "req_id") for item in requests}

    for idx, item in enumerate(requests):
        module_id = _parse_int(item["module_id"], f"requests[{idx}].module_id")
        if module_id not in module_ids:
            raise TopologyPackError(f"requests[{idx}].module_id={module_id}: module not found")

    for idx, item in enumerate(points):
        module_id = _parse_int(item["module_id"], f"points[{idx}].module_id")
        req_id = _parse_int(item["req_id"], f"points[{idx}].req_id")
        if module_id not in module_ids:
            raise TopologyPackError(f"points[{idx}].module_id={module_id}: module not found")
        if req_id not in req_ids:
            raise TopologyPackError(f"points[{idx}].req_id={req_id}: request not found")

    for idx, item in enumerate(commands):
        module_id = _parse_int(item["module_id"], f"commands[{idx}].module_id")
        if module_id not in module_ids:
            raise TopologyPackError(f"commands[{idx}].module_id={module_id}: module not found")

    for idx, item in enumerate(policies):
        module_id = _parse_int(item["module_id"], f"policies[{idx}].module_id")
        if module_id not in module_ids:
            raise TopologyPackError(f"policies[{idx}].module_id={module_id}: module not found")


def _ensure_supported_bus_types(modules: List[Dict[str, Any]]) -> None:
    for idx, item in enumerate(modules):
        bus_type = _parse_int(item["bus_type"], f"modules[{idx}].bus_type")
        if bus_type == BUS_RTU2:
            raise TopologyPackError(
                f"modules[{idx}].bus_type=2: temporarily unsupported by firmware (RTU2 routing not implemented)"
            )
        if bus_type not in (BUS_RTU1, BUS_TCP):
            raise TopologyPackError(f"modules[{idx}].bus_type={bus_type}: unsupported bus_type")


def _ensure_supported_policy_actions(policies: List[Dict[str, Any]]) -> None:
    for idx, item in enumerate(policies):
        on_timeout = _parse_int(item["on_timeout"], f"policies[{idx}].on_timeout")
        on_crc_error = _parse_int(item["on_crc_error"], f"policies[{idx}].on_crc_error")
        on_link_loss = _parse_int(item["on_link_loss"], f"policies[{idx}].on_link_loss")
        if on_timeout < 0 or on_timeout > POLICY_ACTION_MAX:
            raise TopologyPackError(
                f"policies[{idx}].on_timeout={on_timeout}: unsupported action (allowed 0..{POLICY_ACTION_MAX})"
            )
        if on_crc_error < 0 or on_crc_error > POLICY_ACTION_MAX:
            raise TopologyPackError(
                f"policies[{idx}].on_crc_error={on_crc_error}: unsupported action (allowed 0..{POLICY_ACTION_MAX})"
            )
        if on_link_loss < 0 or on_link_loss > POLICY_ACTION_MAX:
            raise TopologyPackError(
                f"policies[{idx}].on_link_loss={on_link_loss}: unsupported action (allowed 0..{POLICY_ACTION_MAX})"
            )


def build_topology_blob(config: Dict[str, Any]) -> bytes:
    modules = _expect_list(config, "modules")
    requests = _expect_list(config, "requests")
    points = _expect_list(config, "points")
    commands = _expect_list(config, "commands")
    policies = _expect_list(config, "policies")

    if len(modules) > GH_TOPOLOGY_V2_MAX_MODULES:
        raise TopologyPackError(f"modules: {len(modules)} > {GH_TOPOLOGY_V2_MAX_MODULES}")
    if len(requests) > GH_TOPOLOGY_V2_MAX_REQ_PROFILES:
        raise TopologyPackError(f"requests: {len(requests)} > {GH_TOPOLOGY_V2_MAX_REQ_PROFILES}")
    if len(points) > GH_TOPOLOGY_V2_MAX_POINTS:
        raise TopologyPackError(f"points: {len(points)} > {GH_TOPOLOGY_V2_MAX_POINTS}")
    if len(commands) > GH_TOPOLOGY_V2_MAX_COMMANDS:
        raise TopologyPackError(f"commands: {len(commands)} > {GH_TOPOLOGY_V2_MAX_COMMANDS}")
    if len(policies) > GH_TOPOLOGY_V2_MAX_POLICIES:
        raise TopologyPackError(f"policies: {len(policies)} > {GH_TOPOLOGY_V2_MAX_POLICIES}")

    _ensure_unique(modules, "module_id", "modules")
    _ensure_unique(requests, "req_id", "requests")
    _ensure_unique(points, "point_id", "points")
    _ensure_unique(commands, "cmd_id", "commands")
    _ensure_supported_bus_types(modules)
    _ensure_supported_policy_actions(policies)
    _ensure_references(modules, requests, points, commands, policies)

    modules_blob = _pack_modules(modules)
    requests_blob = _pack_requests(requests)
    points_blob = _pack_points(points)
    commands_blob = _pack_commands(commands)
    policies_blob = _pack_policies(policies)

    offset = HEADER_SIZE
    off_modules = offset if modules else 0
    offset += len(modules_blob)
    off_requests = offset if requests else 0
    offset += len(requests_blob)
    off_points = offset if points else 0
    offset += len(points_blob)
    off_commands = offset if commands else 0
    offset += len(commands_blob)
    off_policies = offset if policies else 0
    offset += len(policies_blob)

    total_size = offset
    if total_size > TOPOLOGY_MAX_BLOB_SIZE:
        raise TopologyPackError(f"topology blob size {total_size} > {TOPOLOGY_MAX_BLOB_SIZE}")
    if total_size % 2 != 0:
        raise TopologyPackError("topology blob size must be even (Modbus word transport)")

    body = modules_blob + requests_blob + points_blob + commands_blob + policies_blob
    body_crc32 = zlib.crc32(body) & 0xFFFFFFFF

    header_zero_crc = struct.pack(
        HEADER_FMT,
        GH_TOPOLOGY_V2_MAGIC,
        GH_TOPOLOGY_V2_VERSION_MAJOR,
        _u16(config.get("ver_minor", 0), "ver_minor"),
        _u32(total_size, "total_size"),
        _u32(config["generation"], "generation"),
        _u32(config["topology_id"], "topology_id"),
        _u32(config["created_unix_s"], "created_unix_s"),
        _u32(config.get("flags", 0), "flags"),
        _u16(len(modules), "module_count"),
        _u16(len(requests), "req_count"),
        _u16(len(points), "point_count"),
        _u16(len(commands), "cmd_count"),
        _u16(len(policies), "policy_count"),
        0,
        _u32(off_modules, "off_modules"),
        _u32(off_requests, "off_requests"),
        _u32(off_points, "off_points"),
        _u32(off_commands, "off_commands"),
        _u32(off_policies, "off_policies"),
        body_crc32,
        0,
    )
    header_crc32 = zlib.crc32(header_zero_crc) & 0xFFFFFFFF
    header = struct.pack(
        HEADER_FMT,
        GH_TOPOLOGY_V2_MAGIC,
        GH_TOPOLOGY_V2_VERSION_MAJOR,
        _u16(config.get("ver_minor", 0), "ver_minor"),
        _u32(total_size, "total_size"),
        _u32(config["generation"], "generation"),
        _u32(config["topology_id"], "topology_id"),
        _u32(config["created_unix_s"], "created_unix_s"),
        _u32(config.get("flags", 0), "flags"),
        _u16(len(modules), "module_count"),
        _u16(len(requests), "req_count"),
        _u16(len(points), "point_count"),
        _u16(len(commands), "cmd_count"),
        _u16(len(policies), "policy_count"),
        0,
        _u32(off_modules, "off_modules"),
        _u32(off_requests, "off_requests"),
        _u32(off_points, "off_points"),
        _u32(off_commands, "off_commands"),
        _u32(off_policies, "off_policies"),
        body_crc32,
        header_crc32,
    )

    return header + body


def build_upload_chunks(blob: bytes,
                        generation: int,
                        start_token: int = 1,
                        chunk_words: int = TOPOLOGY_UPLOAD_CHUNK_WORDS) -> List[Dict[str, Any]]:
    if not blob:
        raise TopologyPackError("blob is empty")
    if len(blob) > TOPOLOGY_MAX_BLOB_SIZE:
        raise TopologyPackError(f"blob size {len(blob)} > {TOPOLOGY_MAX_BLOB_SIZE}")
    if len(blob) % 2 != 0:
        raise TopologyPackError("blob size must be even")

    chunk_words = _range_check(chunk_words, 1, TOPOLOGY_UPLOAD_CHUNK_WORDS, "chunk_words")
    generation = _u32(generation, "generation")
    start_token = _range_check(start_token, 1, 0xFFFF, "start_token")

    chunk_size = chunk_words * 2
    count = int(math.ceil(len(blob) / float(chunk_size)))
    chunks: List[Dict[str, Any]] = []

    for index in range(count):
        begin = index * chunk_size
        end = min(begin + chunk_size, len(blob))
        payload = blob[begin:end]
        if len(payload) % 2 != 0:
            raise TopologyPackError(f"chunk {index}: odd payload length {len(payload)}")

        words = []
        for word_i in range(0, len(payload), 2):
            hi = payload[word_i]
            lo = payload[word_i + 1]
            words.append((hi << 8) | lo)

        flags = 0
        if index == 0:
            flags |= TOPOLOGY_UPLOAD_FLAG_RESET
        if index == (count - 1):
            flags |= TOPOLOGY_UPLOAD_FLAG_COMMIT

        token = start_token + index
        if token > 0xFFFF:
            raise TopologyPackError("token overflow, lower start_token or number of chunks")

        chunks.append(
            {
                "submit_token": token,
                "chunk_index": index,
                "chunk_words": len(words),
                "flags": flags,
                "total_size": len(blob),
                "chunk_crc32": zlib.crc32(payload) & 0xFFFFFFFF,
                "generation": generation,
                "chunk_data_words": words,
            }
        )

    return chunks


def load_json_file(path: Path) -> Dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise TopologyPackError(f"input file not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise TopologyPackError(f"invalid JSON: {exc}") from exc
    if not isinstance(raw, dict):
        raise TopologyPackError("topology root must be an object")
    return raw


def _write_json(path: Path, data: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def _build_cli_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="Path to topology JSON")
    parser.add_argument("--output-bin", required=True, type=Path, help="Output payload (.bin)")
    parser.add_argument("--output-chunks", required=True, type=Path, help="Output chunks (.json)")
    parser.add_argument("--start-token", type=int, default=1, help="First submit token (1..65535)")
    parser.add_argument(
        "--chunk-words",
        type=int,
        default=TOPOLOGY_UPLOAD_CHUNK_WORDS,
        help=f"Chunk words (1..{TOPOLOGY_UPLOAD_CHUNK_WORDS})",
    )
    return parser


def main() -> int:
    parser = _build_cli_parser()
    args = parser.parse_args()

    try:
        cfg = load_json_file(args.input)
        blob = build_topology_blob(cfg)
        chunks = build_upload_chunks(
            blob=blob,
            generation=_u32(cfg["generation"], "generation"),
            start_token=args.start_token,
            chunk_words=args.chunk_words,
        )
    except TopologyPackError as exc:
        print(f"topology_packer error: {exc}")
        return 1

    args.output_bin.parent.mkdir(parents=True, exist_ok=True)
    args.output_bin.write_bytes(blob)
    _write_json(
        args.output_chunks,
        {
            "format": "topology_upload_v2",
            "blob_size": len(blob),
            "chunk_words_max": args.chunk_words,
            "chunks_total": len(chunks),
            "chunks": chunks,
        },
    )

    print(f"Packed topology blob: {len(blob)} bytes")
    print(f"Prepared chunk requests: {len(chunks)}")
    print(f"Binary output: {args.output_bin}")
    print(f"Chunk script output: {args.output_chunks}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
