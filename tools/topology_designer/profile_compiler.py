#!/usr/bin/env python3
"""Compile human-friendly greenhouse profile into topology_config v2 JSON."""

from __future__ import annotations

import json
import math
import time
from pathlib import Path
from typing import Any, Dict, List, Tuple

SCHEMA_NAME = "greenhouse_profile_v1"

GH_TOPOLOGY_V2_MAX_MODULES = 48
GH_TOPOLOGY_V2_MAX_REQ_PROFILES = 192
GH_TOPOLOGY_V2_MAX_POINTS = 640
GH_TOPOLOGY_V2_MAX_POLICIES = 64

SENSOR_COUNT = 180
MODBUS_MAX_SLAVES = 20

MODBUS_UART_TX_TIMEOUT_MS = 100
MODBUS_INTER_SLAVE_DELAY_MS = 1
MODBUS_DIAG_BASE_REG = 9
MODBUS_DIAG_REG_COUNT = 6

MOD_TYPE_ZONE_CTRL = 1
BUS_RTU1 = 1
REQ_FC_READ_HOLDING = 3
POINT_TYPE_U16 = 1
POINT_TYPE_S16 = 2

POLICY_ACTION_KEEP_LAST = 0
POLICY_ACTION_SAFE_DEFAULT = 1
POLICY_ACTION_FORCE_OFFLINE = 2

CHANNEL_ORDER = [
    "AIR_TEMP",
    "AIR_HUM",
    "WATER_RAIL",
    "WATER_GROW",
    "WATER_UNDERTRAY",
    "WATER_UPPER_HEAT",
    "WINDOWS_POS_A",
    "WINDOWS_POS_B",
    "CURTAIN_POS",
]
CHANNEL_TO_INDEX = {name: idx for idx, name in enumerate(CHANNEL_ORDER)}

CHANNEL_POINT_META = {
    "AIR_TEMP": {"point_type": POINT_TYPE_S16, "scale_pow10": -1, "alarm_low": -300, "alarm_high": 500},
    "AIR_HUM": {"point_type": POINT_TYPE_U16, "scale_pow10": -1, "alarm_low": 0, "alarm_high": 1000},
    "WATER_RAIL": {"point_type": POINT_TYPE_S16, "scale_pow10": -1, "alarm_low": -200, "alarm_high": 800},
    "WATER_GROW": {"point_type": POINT_TYPE_S16, "scale_pow10": -1, "alarm_low": -200, "alarm_high": 800},
    "WATER_UNDERTRAY": {"point_type": POINT_TYPE_S16, "scale_pow10": -1, "alarm_low": -200, "alarm_high": 800},
    "WATER_UPPER_HEAT": {"point_type": POINT_TYPE_S16, "scale_pow10": -1, "alarm_low": -200, "alarm_high": 800},
    "WINDOWS_POS_A": {"point_type": POINT_TYPE_U16, "scale_pow10": 0, "alarm_low": 0, "alarm_high": 100},
    "WINDOWS_POS_B": {"point_type": POINT_TYPE_U16, "scale_pow10": 0, "alarm_low": 0, "alarm_high": 100},
    "CURTAIN_POS": {"point_type": POINT_TYPE_U16, "scale_pow10": 0, "alarm_low": 0, "alarm_high": 100},
}

MIN_READ_SPAN_WORDS = MODBUS_DIAG_BASE_REG + MODBUS_DIAG_REG_COUNT


class ProfileError(ValueError):
    """Raised when profile is invalid or cannot be compiled."""


def _parse_int(value: Any, name: str) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        text = value.strip().lower()
        base = 16 if text.startswith("0x") else 10
        try:
            return int(text, base)
        except ValueError as exc:
            raise ProfileError(f"{name}: invalid integer '{value}'") from exc
    raise ProfileError(f"{name}: integer expected")


def _parse_range(value: Any, name: str, min_val: int, max_val: int) -> int:
    parsed = _parse_int(value, name)
    if parsed < min_val or parsed > max_val:
        raise ProfileError(f"{name}: value {parsed} out of range [{min_val}..{max_val}]")
    return parsed


def _pack_user_param0(local_module_no: int, greenhouse_no: int, site_no: int) -> int:
    return ((site_no & 0xFF) << 24) | ((greenhouse_no & 0xFF) << 16) | (local_module_no & 0xFFFF)


def _pack_user_param1(profile_id: int, safety_profile_id: int, publish_group_id: int) -> int:
    return (publish_group_id & 0xFF) << 16 | (safety_profile_id & 0xFF) << 8 | (profile_id & 0xFF)


def _normalize_channels(raw_channels: Any, prefix: str) -> List[str]:
    if not isinstance(raw_channels, list) or not raw_channels:
        raise ProfileError(f"{prefix}.channels: non-empty list expected")

    seen = set()
    channels: List[str] = []
    for idx, raw_name in enumerate(raw_channels):
        if not isinstance(raw_name, str):
            raise ProfileError(f"{prefix}.channels[{idx}]: string expected")
        name = raw_name.strip().upper()
        if name not in CHANNEL_TO_INDEX:
            raise ProfileError(f"{prefix}.channels[{idx}]='{raw_name}': unknown channel")
        if name in seen:
            raise ProfileError(f"{prefix}.channels: duplicate channel '{name}'")
        seen.add(name)
        channels.append(name)
    channels.sort(key=lambda item: CHANNEL_TO_INDEX[item])
    return channels


def _compute_poll_util_permille(zones: List[Dict[str, Any]]) -> int:
    util_permille = 0
    for zone in zones:
        retries = zone["retries"]
        timeout_ms = zone["timeout_ms"]
        backoff_ms = zone["backoff_ms"]
        period_ms = zone["poll_period_ms"]
        req_worst_ms = retries * (MODBUS_UART_TX_TIMEOUT_MS + timeout_ms)
        if retries > 1:
            req_worst_ms += (retries - 1) * backoff_ms
        req_worst_ms += MODBUS_INTER_SLAVE_DELAY_MS
        util_permille += math.ceil((req_worst_ms * 1000) / period_ms)
    return util_permille


def _normalize_profile(profile: Dict[str, Any]) -> Dict[str, Any]:
    if not isinstance(profile, dict):
        raise ProfileError("profile root must be an object")

    schema = profile.get("schema", "")
    if schema != SCHEMA_NAME:
        raise ProfileError(f"schema: expected '{SCHEMA_NAME}'")

    generation = _parse_range(profile.get("generation"), "generation", 1, 0xFFFFFFFF)
    topology_id = _parse_range(profile.get("topology_id"), "topology_id", 1, 0xFFFFFFFF)
    created_unix_s = _parse_range(
        profile.get("created_unix_s", int(time.time())),
        "created_unix_s",
        1,
        0xFFFFFFFF,
    )
    flags = _parse_range(profile.get("flags", 0), "flags", 0, 0xFFFFFFFF)
    site_no = _parse_range(profile.get("site_no", 1), "site_no", 0, 255)
    greenhouse_no = _parse_range(profile.get("greenhouse_no", 1), "greenhouse_no", 0, 255)
    ver_minor = _parse_range(profile.get("ver_minor", 0), "ver_minor", 0, 0xFFFF)

    raw_zones = profile.get("zones")
    if not isinstance(raw_zones, list) or not raw_zones:
        raise ProfileError("zones: non-empty list expected")
    if len(raw_zones) > MODBUS_MAX_SLAVES:
        raise ProfileError(f"zones: {len(raw_zones)} > {MODBUS_MAX_SLAVES} (RTU slave limit)")

    zones: List[Dict[str, Any]] = []
    zone_nos = set()
    bus_slave_pairs = set()

    for idx, raw_zone in enumerate(raw_zones):
        prefix = f"zones[{idx}]"
        if not isinstance(raw_zone, dict):
            raise ProfileError(f"{prefix}: object expected")

        zone_no = _parse_range(raw_zone.get("zone_no"), f"{prefix}.zone_no", 1, 99)
        if zone_no in zone_nos:
            raise ProfileError(f"{prefix}.zone_no={zone_no}: duplicate zone number")
        zone_nos.add(zone_no)

        bus_index = _parse_range(raw_zone.get("bus_index", 0), f"{prefix}.bus_index", 0, 1)
        slave_id = _parse_range(raw_zone.get("slave_id"), f"{prefix}.slave_id", 1, MODBUS_MAX_SLAVES)
        bus_slave_key = (bus_index, slave_id)
        if bus_slave_key in bus_slave_pairs:
            raise ProfileError(
                f"{prefix}: duplicate bus/slave mapping bus_index={bus_index}, slave_id={slave_id}"
            )
        bus_slave_pairs.add(bus_slave_key)

        channels = _normalize_channels(raw_zone.get("channels"), prefix)
        poll_period_ms = _parse_range(raw_zone.get("poll_period_ms", 5000), f"{prefix}.poll_period_ms", 100, 600000)
        timeout_ms = _parse_range(raw_zone.get("timeout_ms", 300), f"{prefix}.timeout_ms", 50, 10000)
        retries = _parse_range(raw_zone.get("retries", 2), f"{prefix}.retries", 1, 10)
        backoff_ms = _parse_range(raw_zone.get("backoff_ms", 20), f"{prefix}.backoff_ms", 0, 2000)
        if timeout_ms >= poll_period_ms:
            raise ProfileError(f"{prefix}: timeout_ms must be smaller than poll_period_ms")

        start_reg = _parse_range(raw_zone.get("start_reg", 0), f"{prefix}.start_reg", 0, 0xFFFF)
        if start_reg != 0:
            raise ProfileError(f"{prefix}.start_reg={start_reg}: only start_reg=0 is supported in profile v1")

        for channel_name in channels:
            publish_index = (slave_id - 1) * len(CHANNEL_ORDER) + CHANNEL_TO_INDEX[channel_name]
            if publish_index >= SENSOR_COUNT:
                raise ProfileError(
                    f"{prefix}: channel '{channel_name}' publish_index={publish_index} >= SENSOR_COUNT({SENSOR_COUNT})"
                )

        zones.append(
            {
                "zone_no": zone_no,
                "bus_index": bus_index,
                "slave_id": slave_id,
                "channels": channels,
                "poll_period_ms": poll_period_ms,
                "timeout_ms": timeout_ms,
                "retries": retries,
                "backoff_ms": backoff_ms,
                "start_reg": start_reg,
                "priority": _parse_range(raw_zone.get("priority", 1), f"{prefix}.priority", 0, 3),
                "offline_reprobe_ms": _parse_range(
                    raw_zone.get("offline_reprobe_ms", 30000),
                    f"{prefix}.offline_reprobe_ms",
                    1000,
                    600000,
                ),
                "heartbeat_timeout_ms": _parse_range(
                    raw_zone.get("heartbeat_timeout_ms", 5000),
                    f"{prefix}.heartbeat_timeout_ms",
                    100,
                    600000,
                ),
                "quality_policy": _parse_range(raw_zone.get("quality_policy", 1), f"{prefix}.quality_policy", 0, 255),
                "stale_timeout_s": _parse_range(
                    raw_zone.get("stale_timeout_s", 20),
                    f"{prefix}.stale_timeout_s",
                    1,
                    0xFFFF,
                ),
                "capability_mask": _parse_range(raw_zone.get("capability_mask", 0x00000005), f"{prefix}.capability_mask", 0, 0xFFFFFFFF),
                "profile_id": _parse_range(raw_zone.get("profile_id", 0), f"{prefix}.profile_id", 0, 255),
                "safety_profile_id": _parse_range(
                    raw_zone.get("safety_profile_id", 0), f"{prefix}.safety_profile_id", 0, 255
                ),
                "publish_group_id": _parse_range(
                    raw_zone.get("publish_group_id", 0), f"{prefix}.publish_group_id", 0, 255
                ),
                "policy_on_timeout": _parse_range(
                    raw_zone.get("policy_on_timeout", POLICY_ACTION_SAFE_DEFAULT),
                    f"{prefix}.policy_on_timeout",
                    POLICY_ACTION_KEEP_LAST,
                    POLICY_ACTION_FORCE_OFFLINE,
                ),
                "policy_on_crc_error": _parse_range(
                    raw_zone.get("policy_on_crc_error", POLICY_ACTION_SAFE_DEFAULT),
                    f"{prefix}.policy_on_crc_error",
                    POLICY_ACTION_KEEP_LAST,
                    POLICY_ACTION_FORCE_OFFLINE,
                ),
                "policy_on_link_loss": _parse_range(
                    raw_zone.get("policy_on_link_loss", POLICY_ACTION_FORCE_OFFLINE),
                    f"{prefix}.policy_on_link_loss",
                    POLICY_ACTION_KEEP_LAST,
                    POLICY_ACTION_FORCE_OFFLINE,
                ),
                "policy_max_consecutive_fail": _parse_range(
                    raw_zone.get("policy_max_consecutive_fail", 3),
                    f"{prefix}.policy_max_consecutive_fail",
                    0,
                    0xFFFF,
                ),
                "policy_recover_good_cycles": _parse_range(
                    raw_zone.get("policy_recover_good_cycles", 2),
                    f"{prefix}.policy_recover_good_cycles",
                    0,
                    0xFFFF,
                ),
                "policy_safe_profile_id": _parse_range(
                    raw_zone.get("policy_safe_profile_id", 1),
                    f"{prefix}.policy_safe_profile_id",
                    0,
                    0xFFFF,
                ),
            }
        )

    zones.sort(key=lambda item: item["zone_no"])

    if len(zones) > GH_TOPOLOGY_V2_MAX_MODULES:
        raise ProfileError(f"zones/modules: {len(zones)} > {GH_TOPOLOGY_V2_MAX_MODULES}")
    if len(zones) > GH_TOPOLOGY_V2_MAX_REQ_PROFILES:
        raise ProfileError(f"zones/requests: {len(zones)} > {GH_TOPOLOGY_V2_MAX_REQ_PROFILES}")
    if len(zones) > GH_TOPOLOGY_V2_MAX_POLICIES:
        raise ProfileError(f"zones/policies: {len(zones)} > {GH_TOPOLOGY_V2_MAX_POLICIES}")

    point_count = sum(len(zone["channels"]) for zone in zones)
    if point_count > GH_TOPOLOGY_V2_MAX_POINTS:
        raise ProfileError(f"points: {point_count} > {GH_TOPOLOGY_V2_MAX_POINTS}")

    util_permille = _compute_poll_util_permille(zones)
    if util_permille > 1000:
        raise ProfileError(f"poll budget overflow: util={util_permille} permille > 1000")

    return {
        "schema": schema,
        "ver_minor": ver_minor,
        "generation": generation,
        "topology_id": topology_id,
        "created_unix_s": created_unix_s,
        "flags": flags,
        "site_no": site_no,
        "greenhouse_no": greenhouse_no,
        "zones": zones,
        "poll_util_permille": util_permille,
    }


def compile_profile(profile: Dict[str, Any]) -> Dict[str, Any]:
    normalized = _normalize_profile(profile)
    zones = normalized["zones"]

    modules = []
    requests = []
    points = []
    policies = []

    for req_idx, zone in enumerate(zones):
        zone_no = zone["zone_no"]
        module_id = 100 + zone_no
        if module_id > 199:
            raise ProfileError(f"zone_no={zone_no}: module_id={module_id} exceeds zone module range 100..199")

        req_id = module_id * 10
        channels = zone["channels"]
        highest_channel = CHANNEL_TO_INDEX[channels[-1]]
        req_reg_count = max(highest_channel + 1, MIN_READ_SPAN_WORDS)

        modules.append(
            {
                "module_id": module_id,
                "module_type": MOD_TYPE_ZONE_CTRL,
                "bus_type": BUS_RTU1,
                "bus_index": zone["bus_index"],
                "slave_id": zone["slave_id"],
                "zone_id": zone_no,
                "req_first": req_idx,
                "req_count": 1,
                "cmd_first": 0,
                "cmd_count": 0,
                "offline_reprobe_ms": zone["offline_reprobe_ms"],
                "heartbeat_timeout_ms": zone["heartbeat_timeout_ms"],
                "capability_mask": zone["capability_mask"],
                "user_param0": _pack_user_param0(
                    local_module_no=zone_no,
                    greenhouse_no=normalized["greenhouse_no"],
                    site_no=normalized["site_no"],
                ),
                "user_param1": _pack_user_param1(
                    profile_id=zone["profile_id"],
                    safety_profile_id=zone["safety_profile_id"],
                    publish_group_id=zone["publish_group_id"],
                ),
            }
        )

        point_first = len(points)
        requests.append(
            {
                "req_id": req_id,
                "module_id": module_id,
                "fc": REQ_FC_READ_HOLDING,
                "priority": zone["priority"],
                "start_reg": zone["start_reg"],
                "reg_count": req_reg_count,
                "period_ms": zone["poll_period_ms"],
                "timeout_ms": zone["timeout_ms"],
                "retries": zone["retries"],
                "backoff_ms": zone["backoff_ms"],
                "point_first": point_first,
                "point_count": len(channels),
                "flags": 0,
            }
        )

        for channel_name in channels:
            channel_idx = CHANNEL_TO_INDEX[channel_name]
            point_meta = CHANNEL_POINT_META[channel_name]
            points.append(
                {
                    "point_id": module_id * 100 + channel_idx,
                    "module_id": module_id,
                    "req_id": req_id,
                    "reg_offset": channel_idx,
                    "point_type": point_meta["point_type"],
                    "scale_pow10": point_meta["scale_pow10"],
                    "bit_index": 0,
                    "quality_policy": zone["quality_policy"],
                    "publish_index": (zone["slave_id"] - 1) * len(CHANNEL_ORDER) + channel_idx,
                    "stale_timeout_s": zone["stale_timeout_s"],
                    "alarm_low": point_meta["alarm_low"],
                    "alarm_high": point_meta["alarm_high"],
                }
            )

        policies.append(
            {
                "module_id": module_id,
                "on_timeout": zone["policy_on_timeout"],
                "on_crc_error": zone["policy_on_crc_error"],
                "on_link_loss": zone["policy_on_link_loss"],
                "max_consecutive_fail": zone["policy_max_consecutive_fail"],
                "recover_good_cycles": zone["policy_recover_good_cycles"],
                "safe_profile_id": zone["policy_safe_profile_id"],
            }
        )

    return {
        "ver_minor": normalized["ver_minor"],
        "generation": normalized["generation"],
        "topology_id": normalized["topology_id"],
        "created_unix_s": normalized["created_unix_s"],
        "flags": normalized["flags"],
        "modules": modules,
        "requests": requests,
        "points": points,
        "commands": [],
        "policies": policies,
    }


def profile_summary(profile: Dict[str, Any]) -> Dict[str, int]:
    normalized = _normalize_profile(profile)
    zones = normalized["zones"]
    return {
        "zones": len(zones),
        "points": sum(len(zone["channels"]) for zone in zones),
        "poll_util_permille": normalized["poll_util_permille"],
    }


def load_profile(path: Path) -> Dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ProfileError(f"input file not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ProfileError(f"invalid JSON: {exc}") from exc
    if not isinstance(raw, dict):
        raise ProfileError("profile root must be an object")
    return raw


def write_json(path: Path, payload: Dict[str, Any], indent: int = 2) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=indent), encoding="utf-8")


def compile_profile_file(input_path: Path, output_path: Path, indent: int = 2) -> Tuple[Dict[str, Any], Dict[str, int]]:
    profile = load_profile(input_path)
    topology = compile_profile(profile)
    write_json(output_path, topology, indent=indent)
    return topology, profile_summary(profile)
