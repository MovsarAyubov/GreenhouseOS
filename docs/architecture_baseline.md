# greenhouseOS Architecture Baseline

Date: `2026-02-23`

This baseline records decisions confirmed for the current firmware line.

## Confirmed system constraints
- Max RS485 slave count: `20`
- Modbus TCP server port: `502`
- Master polling period: one full cycle every `5 seconds`
- Sensor buffer size (`SENSOR_COUNT`): `180`

## Recovery and watchdog baseline
- `Error_Handler` stores reset reason into RTC backup register and triggers software reset.
- Health watchdog supervises `CONTROL`, `MODBUS`, `CONFIG`, and `TCP` tasks.
- If watchdog miss is persistent, firmware stops refreshing IWDG and allows automatic reset.

## RS485/Modbus baseline
- RTU master uses deterministic 5-second full-cycle timing.
- Each Modbus read/write operation uses bounded retry (`MODBUS_RETRY_COUNT`) with linear backoff (`MODBUS_RETRY_BACKOFF_MS`).
- RTU transport uses UART interrupt completion (TX/RX/Error) with RTOS event flags instead of blocking byte waits.
- TX failures and RX frame integrity failures are accounted in `g_status` counters.

## Config pipeline baseline
- Config updates are submitted through Modbus map global config window (`GH_MB_CFG_BASE`).
- Pipeline is tokenized (`SUBMIT_TOKEN`) and reports explicit result codes (`RESULT_CODE`, `RESULT_TOKEN`).
- Storage task validates `version`, `CRC32`, and payload float ranges before flash write.
- Flash writes use retry policy and A/B fallback before reject.

## Modbus map concurrency baseline
- Map read/write API (`GH_ModbusMap_ReadRange/WriteSingle/WriteRange/...`) is protected by internal RTOS mutex.
- TCP Modbus requests use map API hooks (no direct data-path writes for `FC=3/6/16`).
- Modbus TCP function set is restricted to `FC=3`, `FC=6`, `FC=16` to keep register ownership contract deterministic.

## Source of truth in code
- Runtime constants: `Core/Inc/gh_runtime_state.h`
- RTU master loop: `Core/Src/gh_modbus_master.c`
- TCP server startup: `Core/Src/gh_modbus_tcp_server.c`
- Holding map backend: `Core/Src/gh_modbus_map.c`

## Notes
- If any document contradicts these values, this baseline and the code listed above take precedence.
- Future protocol extensions must update this file in the same change set.
