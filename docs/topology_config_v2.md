# Topology Config v2

`topology_config v2` describes greenhouse topology as data consumed by
`greenhouse-hub` and topology tooling.

## Purpose

The schema describes:

- module instances and Modbus addresses;
- polling requests per module;
- telemetry point mapping;
- command profiles;
- safety/fallback policies.

The hub reads the JSON form directly. `tools/topology/topology_packer.py` can
also pack the same JSON into a deterministic binary/chunk artifact for offline
tooling and compatibility workflows.

## Root Fields

- `ver_minor`: schema minor version.
- `generation`: monotonic deployment generation.
- `topology_id`: deployment/site identifier.
- `created_unix_s`: topology creation timestamp.
- `flags`: reserved feature flags.
- `modules`: module table.
- `requests`: polling request table.
- `points`: telemetry point table.
- `commands`: command profile table.
- `policies`: fault policy table.

## Core Constants

- Module types: `1` zone controller, `2` weather station.
- Bus types: `1` RTU1, `2` RTU2 reserved, `3` TCP.
- Function codes: `3` read holding, `4` read input, `6` write single, `16` write multiple.
- Point types: `1` u16, `2` s16, `3` u32, `4` s32, `5` float, `6` bit.

Current tooling accepts `BUS_RTU1` and `BUS_TCP`; `BUS_RTU2` remains reserved.

## Table Contracts

`modules[]` defines each logical device:

- `module_id`, `module_type`, `bus_type`, `bus_index`, `slave_id`;
- `zone_id`;
- request and command table ranges;
- heartbeat/offline timing;
- capability and user parameters.

`requests[]` defines poll operations:

- `req_id`, `module_id`, `fc`;
- `start_reg`, `reg_count`;
- `period_ms`, `timeout_ms`, `retries`, `backoff_ms`;
- point table range.

`points[]` maps raw register values to published telemetry:

- `point_id`, `module_id`, `req_id`, `reg_offset`;
- `point_type`, `scale_pow10`, `bit_index`;
- `publish_index`, stale timeout, alarm bounds.

`commands[]` defines setpoint/write profiles:

- `cmd_id`, `module_id`, `fc`;
- `start_reg`, `max_reg_count`, `payload_offset`;
- `timeout_ms`, `ack_point_id`, `flags`.

`policies[]` defines fallback behavior:

- `module_id`;
- `on_timeout`, `on_crc_error`, `on_link_loss`;
- `max_consecutive_fail`, `recover_good_cycles`, `safe_profile_id`.

## Validation

Topology tooling checks numeric bounds, uniqueness, supported bus types,
command payload limits, policy actions, and cross-table references. The hub
also rejects missing module data and mismatched topology/semantics identifiers.
