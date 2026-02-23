# Block Channel Map (Master Side)

Date: 2026-02-23 baseline.

Channel order per slave (fixed, RTU registers `0..8`):
- 0 `AIR_TEMP`
- 1 `AIR_HUM`
- 2 `WATER_RAIL`
- 3 `WATER_GROW`
- 4 `WATER_UNDERTRAY`
- 5 `WATER_UPPER_HEAT`
- 6 `WINDOWS_POS_A`
- 7 `WINDOWS_POS_B`
- 8 `CURTAIN_POS`

Where it is applied:
- RS485 polling and decode: `Core/Src/gh_modbus_master.c`
- TCP holding map update: `Core/Src/gh_modbus_map.c` (`GH_ModbusMap_UpdateTelemetry`)

Sensor index in flat array:
- `sensor_id = (slave_id - 1) * 9 + channel_index`

Current system limits:
- `slave_id = 1..20`
- `SENSOR_COUNT = 180`

Important:
- Channels with index >= `SENSOR_COUNT` are ignored by current firmware.
- Legacy docs that reference `kModbusMap` in `main.c` are obsolete for current codebase.
