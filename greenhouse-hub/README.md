# Greenhouse Hub

Linux service for a greenhouse PC hub.

The hub owns the RS485 bus, polls Modbus RTU slaves, keeps the latest system
state, routes SCADA setpoints to slaves, and exposes an HTTP API for the
operator UI. Climate control decisions stay inside the slaves.

## Build

Install Go 1.22 or newer on the target Linux machine, then run:

```bash
cd greenhouse-hub
go mod tidy
go build -o greenhouse-hub ./cmd/greenhouse-hub
```

Docker usage is documented in `../docs/docker.md`.

## Run With Mock Transport

Mock mode does not require RS485 hardware:

```bash
./greenhouse-hub \
  --mock \
  --topology ../topology/one_zone_one_weather_all_points_schedule_topology.json \
  --semantics ../topology/one_zone_one_weather_all_points_schedule_semantics.json \
  --slave-maps ./slave_maps \
  --listen :8080
```

## Run With USB-RS485

```bash
./greenhouse-hub \
  --serial /dev/ttyUSB0 \
  --baud 115200 \
  --topology /etc/greenhouse/topology.json \
  --semantics /etc/greenhouse/semantics.json \
  --slave-maps /etc/greenhouse/slave_maps \
  --listen 0.0.0.0:8080
```

The Linux user running the service usually needs access to the serial device:

```bash
sudo usermod -aG dialout greenhouse
```

## API

```text
GET  /api/health
GET  /api/state
GET  /api/slaves
GET  /api/slaves/{slave_id}/setpoints
GET  /api/slave-maps
GET  /api/points
GET  /api/scan
POST /api/scan?from=1&to=40
POST /api/setpoints
```

Operator-facing setpoint metadata for one slave:

```text
GET /api/slaves/1/setpoints
GET /api/slaves/1/setpoints?include_unsupported=1
```

Primary setpoint request, by key from the slave-map catalog:

```json
{
  "slave_id": 1,
  "key": "weather_out_temp",
  "value": -52
}
```

Another setpoint request:

```json
{
  "slave_id": 1,
  "key": "water_rail_setpoint",
  "value": 220
}
```

Window mode writes should use the key-based API so the hub owns the map lookup:

```json
{
  "slave_id": 1,
  "key": "windows_ctrl_mode",
  "value": 1
}
```

Legacy topology command-profile writes are still accepted:

```json
{
  "slave_id": 1,
  "module_id": 101,
  "cmd_profile_id": 5002,
  "values": [600, 600]
}
```

For the legacy windows profile `cmd_profile_id=5004`, payload index `0` maps to
slave register `1030` (`windows_ctrl_mode`) and payload index `1` maps to slave
register `1031` (`windows_force_safe_cmd`). Do not put `windows_ctrl_mode` in
the second payload word.

Autoscan reads holding registers `0..15` from each address and matches
`device_type + modbus_map_version` against the local slave-map catalog:

```text
0 device_type
1 firmware_version
2 modbus_map_version
3 zone_id
4 capability_low
5 capability_high
6 status
7 fault_code
8..9 uptime seconds hi/lo
10 last_master_age_s
11 restart_counter
12 config_version
13..14 serial hi/lo
15 identity_crc
```

## Current Scope

Implemented:

- topology and semantics loading;
- slave-map catalog loading;
- single Modbus operation queue;
- expected slave list from topology;
- periodic polling from slave-map telemetry registers;
- state cache for SCADA with decoded slave-map keys;
- setpoint routing by slave-map key with write-policy checks;
- legacy setpoint routing through topology command profiles;
- identity autoscan;
- mock transport for development.

Next steps:

- keep the RTU port open between operations;
- add WebSocket or SSE live updates;
- persist events/history to SQLite;
- add topology editing and operator approval flow for scan results.
