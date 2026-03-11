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

## Weather Station Profile (`slave_id=20`)

Assumed use:
- dedicated weather module on RTU1
- current telemetry window fits the existing master poll contract: `start_reg=0`, `count=9`

Register map (`start_reg + off`):

| Reg | Name | Type / scale | Source |
|---|---|---|---|
| `0` | `OUT_TEMP` | `int16`, `x0.1 degC` | `meteo.ino#L63`, `meteo.ino#L272` |
| `1` | `OUT_HUM` | `uint16`, `x0.1 %RH` | `meteo.ino#L64`, `meteo.ino#L273` |
| `2` | `WIND_SPEED` | `uint16`, `x0.1 m/s` | `meteo.ino#L65`, `meteo.ino#L274` |
| `3` | `WIND_DIR` | `uint16`, `deg` | `meteo.ino#L66`, `meteo.ino#L275` |
| `4` | `RAIN_FLAG` | `uint16`, `0/1` | `meteo.ino#L67`, `meteo.ino#L276` |
| `5` | `SOLAR_RAD` | `uint16`, `W/m^2` | `meteo.ino#L68`, `meteo.ino#L277` |
| `6` | `BARO_PRESS` | `uint16`, `x0.1 hPa` | `meteo.ino#L69`, `meteo.ino#L278` |
| `7` | `DEW_POINT` | `int16`, `x0.1 degC` | `meteo.ino#L70`, `meteo.ino#L279` |
| `8` | `STATUS_BITS` | `uint16`, bit mask | `meteo.ino#L71`, `meteo.ino#L280` |

Suggested topology mapping:
- module: `module_id=201`, `module_type=2`, `bus_type=1`, `bus_index=0`, `slave_id=20`
- request: `fc=3`, `start_reg=0`, `reg_count=9`
- points:
  - `OUT_TEMP`: `point_type=S16`, `scale_pow10=-1`
  - `OUT_HUM`: `point_type=U16`, `scale_pow10=-1`
  - `WIND_SPEED`: `point_type=U16`, `scale_pow10=-1`
  - `WIND_DIR`: `point_type=U16`, `scale_pow10=0`
  - `RAIN_FLAG`: `point_type=U16`, `scale_pow10=0`
  - `SOLAR_RAD`: `point_type=U16`, `scale_pow10=0`
  - `BARO_PRESS`: `point_type=U16`, `scale_pow10=-1`
  - `DEW_POINT`: `point_type=S16`, `scale_pow10=-1`
  - `STATUS_BITS`: `point_type=U16`, `scale_pow10=0`
