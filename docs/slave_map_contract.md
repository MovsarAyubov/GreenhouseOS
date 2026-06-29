# Slave Map Contract

Slave maps describe the register layout and setpoint policy for each slave
device family. `greenhouse-hub` loads all `*.json` files from the configured
`--slave-maps` directory.

## Location

Default development catalog:

```text
greenhouse-hub/slave_maps/
```

Docker catalog path:

```text
/app/slave_maps
```

Systemd catalog path:

```text
/etc/greenhouse/slave_maps
```

## Purpose

The hub uses slave maps to:

- decode telemetry registers into stable keys;
- expose setpoint metadata through the HTTP API;
- validate operator writes;
- route key-based setpoint requests to the correct Modbus register.

## Runtime Matching

Autoscan reads the slave identity registers and matches `device_type` plus
`modbus_map_version` against the loaded catalog. The loaded catalog must
include every slave type expected by the active topology.

## API

Relevant endpoints:

```text
GET /api/slave-maps
GET /api/slaves/{slave_id}/setpoints
POST /api/setpoints
```
