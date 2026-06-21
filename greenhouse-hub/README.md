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
go build ./cmd/greenhouse-hub
```

## Run With Mock Transport

Mock mode does not require RS485 hardware:

```bash
./greenhouse-hub \
  --mock \
  --topology ../topology/one_zone_one_weather_all_points_schedule_topology.json \
  --semantics ../topology/one_zone_one_weather_all_points_schedule_semantics.json \
  --listen :8080
```

## Run With USB-RS485

```bash
./greenhouse-hub \
  --serial /dev/ttyUSB0 \
  --baud 115200 \
  --topology /etc/greenhouse/topology.json \
  --semantics /etc/greenhouse/semantics.json \
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
GET  /api/points
GET  /api/scan
POST /api/scan?from=1&to=40
POST /api/setpoints
```

Setpoint request:

```json
{
  "slave_id": 1,
  "module_id": 101,
  "cmd_profile_id": 5002,
  "values": [600, 600]
}
```

Autoscan reads holding registers `0..7` from each address:

```text
0 device_type
1 firmware_version
2 modbus_map_version
3 zone_id
4 capability_low
5 capability_high
6 status
7 fault_code
```

## Current Scope

Implemented:

- topology and semantics loading;
- single Modbus operation queue;
- periodic polling from topology requests;
- state cache for SCADA;
- setpoint routing through topology command profiles;
- identity autoscan;
- mock transport for development.

Next steps:

- keep the RTU port open between operations;
- add WebSocket or SSE live updates;
- persist events/history to SQLite;
- add topology editing and operator approval flow for scan results.
