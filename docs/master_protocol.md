# Master Protocol Baseline (STM32F407)

Date: `2026-02-23`.

## Transport (implemented)
- `Modbus TCP` server on port `502`.
- Implementation: `Core/Src/gh_modbus_tcp_server.c` + `Core/Src/Modbus.c`.
- Build requires `GH_USE_LWIP_NETCONN`.

## Modbus map
- Holding register backend: `Core/Src/gh_modbus_map.c`.
- Map size: `GH_MB_TOTAL_REGS = GH_MB_MAX_SLAVES * GH_MB_BLOCK_SIZE = 20 * 64`.
- Addressing supports both zero-based offsets and `41000`-style SCADA offsets (normalized in `Core/Src/Modbus.c`).

## Supported Modbus TCP operations
- Holding read (`FC=3`).
- Single holding write (`FC=6`).
- Multiple holding write (`FC=16`).
- For TCP requests, read/write operations are routed through `GH_ModbusMap_*` hooks (not direct raw array writes).

## Config Pipeline Registers (global window)
- Base index: `GH_MB_CFG_BASE = 1280` (SCADA offset: `42280`).
- Window size: `GH_MB_CFG_REGS = 80`.

Register map (`base + off`):
- `+0` `SUBMIT_TOKEN` (W): non-zero changing token triggers enqueue to `qConfigStoreHandle`.
- `+1` `RESULT_CODE` (R): pipeline result code.
- `+2` `RESULT_TOKEN` (R): token of last reported result.
- `+3..+4` `ACTIVE_VERSION` (R, hi/lo).
- `+5..+6` `LAST_REQ_VERSION` (R, hi/lo).
- `+7..+8` `LAST_REQ_CRC32` (R, hi/lo).
- `+10..+11` `REQ_VERSION` (W, hi/lo).
- `+12..+13` `REQ_CRC32` (W, hi/lo).
- `+16..+79` `REQ_PAYLOAD_WORDS[64]` (W).

Result codes:
- `0` `IDLE`
- `1` `QUEUED`
- `2` `APPLIED`
- `10` `REJECT_BAD_VERSION`
- `11` `REJECT_BAD_CRC`
- `12` `REJECT_RANGE`
- `13` `REJECT_QUEUE_FULL`
- `14` `FLASH_FAIL`
- `15` `APPLY_QUEUE_FAIL`

## Register ownership contract
- Per-slave telemetry/diag windows (`0..1279`) are produced by `ModbusMasterTask` via `GH_ModbusMap_Update*`.
- Per-slave setpoint/apply fields in same windows are written from TCP (`FC=6/16`) and consumed by master via `GH_ModbusMap_GetApplyRequest`.
- Global config window (`1280..1359`) request fields are written from TCP and consumed by `ConfigStorageTask`.
- Global config result fields are written by `ConfigStorageTask`/`ControlTask` and read by TCP clients.

## Master to slave RS485/RTU cycle
- Master polls slave IDs `1..20`.
- One full poll cycle runs every `5 seconds` (waits for remaining cycle budget).

## Important
- Custom framed TCP protocol (`HELLO/STATUS/SNAPSHOT/EVENT`) is not implemented in current firmware.
- This file describes current firmware behavior, not a future target protocol.
