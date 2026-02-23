# Modbus Slave Table

Date: 2026-02-23 baseline.

Actual sources in code:
- `Core/Src/gh_modbus_master.c` (RS485/RTU polling)
- `Core/Src/gh_modbus_map.c` (Modbus TCP holding map)
- `Core/Inc/gh_runtime_state.h` (system limits)

Limits:
- max slaves: `20`
- sensors per slave in current RTU polling: `9` (`reg 0..8`)
- total sensors in system memory: `180`

Current RTU polling profile:
- slave IDs: `1..20`
- telemetry read: `start_reg=0`, `count=9`
- diagnostics read: `start_reg=128`, `count=6`

Indexing rule for `g_sensors`:
- `sensor_id = (slave_id - 1) * 9 + channel`
- `channel` in `0..8`
- value is written only if `sensor_id < SENSOR_COUNT`
