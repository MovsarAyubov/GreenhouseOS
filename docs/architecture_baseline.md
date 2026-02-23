# greenhouseOS Architecture Baseline

Date: `2026-02-23`

This baseline records decisions confirmed for the current firmware line.

## Confirmed system constraints
- Max RS485 slave count: `20`
- Modbus TCP server port: `502`
- Master polling period: one full cycle every `5 seconds`
- Sensor buffer size (`SENSOR_COUNT`): `180`

## Source of truth in code
- Runtime constants: `Core/Inc/gh_runtime_state.h`
- RTU master loop: `Core/Src/gh_modbus_master.c`
- TCP server startup: `Core/Src/gh_modbus_tcp_server.c`
- Holding map backend: `Core/Src/gh_modbus_map.c`

## Notes
- If any document contradicts these values, this baseline and the code listed above take precedence.
- Future protocol extensions must update this file in the same change set.
