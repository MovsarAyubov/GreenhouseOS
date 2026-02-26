# Master Protocol Baseline (STM32F407)

Date: `2026-02-25`.

## Transport (implemented)
- `Modbus TCP` server on port `502`.
- Implementation: `Core/Src/gh_modbus_tcp_server.c` + `Core/Src/Modbus.c`.
- Build requires `GH_USE_LWIP_NETCONN`.

## Modbus map
- Holding register backend: `Core/Src/gh_modbus_map.c`.
- Addressing supports both zero-based offsets and `41000`-style SCADA offsets (normalized in `Core/Src/Modbus.c`).

Windows:
- Slave data window: `GH_MB_DATA_REGS = 20 * 64 = 1280` registers (`0..1279`).
- Legacy config window: `GH_MB_CFG_REGS = 80` registers (`1280..1359`).
- Diagnostics window: `GH_MB_DIAG_REGS = 32` registers (`1360..1391`).
- Topology upload window: `GH_MB_TOPO_REGS = 144` registers (`1392..1535`).
- Total: `GH_MB_TOTAL_REGS = 1536`.

## Supported Modbus TCP operations
- Holding read (`FC=3`).
- Single holding write (`FC=6`).
- Multiple holding write (`FC=16`).
- TCP requests are routed through `GH_ModbusMap_*` hooks (no direct raw buffer write from protocol stack).

## Legacy config pipeline window (`GH_MB_CFG_BASE`)
- Base index: `1280` (SCADA offset `42280`).
- Size: `80` registers.

Register map (`base + off`):
- `+0` `SUBMIT_TOKEN` (W): non-zero changed token enqueues request to `qConfigStoreHandle`.
- `+1` `RESULT_CODE` (R).
- `+2` `RESULT_TOKEN` (R).
- `+3..+4` `ACTIVE_VERSION` (R, hi/lo).
- `+5..+6` `LAST_REQ_VERSION` (R, hi/lo).
- `+7..+8` `LAST_REQ_CRC32` (R, hi/lo).
- `+10..+11` `REQ_VERSION` (W, hi/lo).
- `+12..+13` `REQ_CRC32` (W, hi/lo).
- `+16..+79` `REQ_PAYLOAD_WORDS[64]` (W).

## Diagnostics window (`GH_MB_DIAG_BASE`)
- Base index: `1360` (SCADA offset `42360`).
- Size: `32` registers.
- Exposes boot/reset/error/network counters and key runtime diagnostics.
- Detailed meaning of diagnostics and codes: `docs/error_codes.md`.

## Topology upload window (`GH_MB_TOPO_BASE`)
- Base index: `1392` (SCADA offset `42392`).
- Size: `144` registers.
- Used for chunked upload of `topology_config v2` blob.

Register map (`base + off`):
- `+0` `SUBMIT_TOKEN` (W): non-zero changed token enqueues one chunk request to `qTopologyStoreHandle`.
- `+1` `RESULT_CODE` (R): last topology pipeline result.
- `+2` `RESULT_TOKEN` (R): token for the last processed topology request.
- `+3` `ACTIVE_FLAGS` (R): bit0=`topology_active`.
- `+4` `ACTIVE_VER_MAJOR` (R).
- `+5` `ACTIVE_VER_MINOR` (R).
- `+6..+7` `ACTIVE_GENERATION` (R, hi/lo).
- `+8..+9` `ACTIVE_SIZE_BYTES` (R, hi/lo).
- `+10` `REQ_CHUNK_INDEX` (W).
- `+11` `REQ_CHUNK_WORDS` (W): must be `<= TOPOLOGY_UPLOAD_CHUNK_WORDS` (`120`).
- `+12..+13` `REQ_TOTAL_SIZE_BYTES` (W, hi/lo).
- `+14..+15` `REQ_CHUNK_CRC32` (W, hi/lo), CRC over `chunk_words*2` bytes.
- `+16` `REQ_FLAGS` (W): bit0=`COMMIT`, bit1=`RESET_STAGING`.
- `+17..+18` `REQ_GENERATION` (W, hi/lo).
- `+20..+139` `REQ_CHUNK_DATA_WORDS[120]` (W), big-endian bytes inside each word.

Upload logic details and client algorithm: `docs/topology_upload_protocol.md`.

## Result codes
- `0` `IDLE`
- `1` `QUEUED`
- `2` `APPLIED`
- `10` `REJECT_BAD_VERSION`
- `11` `REJECT_BAD_CRC`
- `12` `REJECT_RANGE`
- `13` `REJECT_QUEUE_FULL`
- `14` `FLASH_FAIL`
- `15` `APPLY_QUEUE_FAIL`
- `20` `REJECT_TOPOLOGY_SCHEMA`
- `21` `REJECT_TOPOLOGY_BOUNDS`
- `22` `REJECT_TOPOLOGY_CRC`
- `23` `REJECT_TOPOLOGY_COLLISION`
- `24` `REJECT_TOPOLOGY_BUDGET`

## Register ownership contract
- Per-slave telemetry/apply windows (`0..1279`) are produced by `ModbusMasterTask` via `GH_ModbusMap_Update*`.
- Per-slave setpoint/apply fields in same windows are written from TCP (`FC=6/16`) and consumed by master via `GH_ModbusMap_GetApplyRequest`.
- Legacy config window (`1280..1359`) request fields are written from TCP and consumed by `ConfigStorageTask`.
- Diagnostics window (`1360..1391`) is runtime-owned and read-only from client perspective.
- Topology window (`1392..1535`) request fields are written from TCP and consumed by `ConfigStorageTask`.
- Config/topology result fields are written by backend tasks and read by TCP clients.

## Master to slave RS485/RTU cycle
- Master polls slave IDs `1..20`.
- One full poll cycle runs every `5` seconds (cycle budget control).

## Important
- Custom framed TCP protocol (`HELLO/STATUS/SNAPSHOT/EVENT`) is not implemented in current firmware.
- This file describes current firmware behavior, not a future target protocol.
