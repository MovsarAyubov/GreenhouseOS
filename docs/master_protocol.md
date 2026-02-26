# Master Protocol Baseline (STM32F407)

Date: `2026-02-26`.

## Transport (implemented)
- `Modbus TCP` server on port `502`.
- Implementation: `Core/Src/gh_modbus_tcp_server.c` + `Core/Src/Modbus.c`.
- Build requires `GH_USE_LWIP_NETCONN`.

## Modbus map
- Holding register backend: `Core/Src/gh_modbus_map.c`.
- Addressing supports both zero-based offsets and `41000`-style SCADA offsets (normalized in `Core/Src/Modbus.c`).

Windows:
- Points window: `GH_MB_POINTS_REGS = SENSOR_COUNT * 6 = 1080` (`0..1079`).
- Slave status window: `GH_MB_SLAVE_STATUS_REGS = 20 * 8 = 160` (`1080..1239`).
- Command ingress window: `GH_MB_CMD_REGS = 20 * 10 = 200` (`1240..1439`).
- Directory window: `GH_MB_DIR_REGS = 32` (`1440..1471`).
- Legacy config window: `GH_MB_CFG_REGS = 80` (`1472..1551`).
- Diagnostics window: `GH_MB_DIAG_REGS = 32` (`1552..1583`).
- Topology upload window: `GH_MB_TOPO_REGS = 144` (`1584..1727`).
- Total: `GH_MB_TOTAL_REGS = 1728`.

## Supported Modbus TCP operations
- Holding read (`FC=3`).
- Single holding write (`FC=6`).
- Multiple holding write (`FC=16`).
- TCP requests are routed through `GH_ModbusMap_*` hooks (no direct raw buffer write from protocol stack).

## Points window (`GH_MB_POINTS_BASE`)
- Base index: `0` (SCADA `41000`).
- Row stride: `6` registers per `publish_index`.

Register map (`row_base + off`):
- `+0..+1` `VALUE` (`float32`, raw bits hi/lo).
- `+2` `QUALITY` (`SENSOR_QUALITY_*`).
- `+3` `AGE_SEC`.
- `+4` `MODULE_ID`.
- `+5` `FLAGS` (bit0=`valid`).

## Slave status window (`GH_MB_SLAVE_STATUS_BASE`)
- Base index: `1080` (SCADA `42080`).
- Row stride: `8` registers per slave (`id=1..20`).

Register map (`row_base + off`):
- `+0` `STATUS` (bit0=`online`, bit1=`stale`).
- `+1` `LAST_OK_AGE_SEC`.
- `+2` `ERR_TIMEOUT`.
- `+3` `ERR_CRC`.
- `+4` `ERR_EXCEPTION`.
- `+5` `DATA_VERSION`.
- `+6` `VALID_MASK`.
- `+7` `OUT_STATE_MASK`.

## Command ingress window (`GH_MB_CMD_BASE`)
- Base index: `1240` (SCADA `42240`).
- Row stride: `10` registers per slave.

Register map (`row_base + off`):
- `+0` `MODE`.
- `+1` `SET_TEMP_X10`.
- `+2` `SET_HUM_X10`.
- `+3` `HYST_TEMP_X10`.
- `+4` `HYST_HUM_X10`.
- `+5` `MIN_ON_SEC`.
- `+6` `MIN_OFF_SEC`.
- `+7` `OUT_CMD_MASK`.
- `+8` `APPLY_TRIGGER`.
- `+9` `LAST_APPLIED_TRIGGER`.

Runtime behavior:
- In topology mode, this block is interpreted as command payload for `commands[]` of the module.
- `FC6` command uses payload word `+0`.
- `FC16` command uses first `max_reg_count` words from payload (`+0..+7`, max `8` words).
- If no topology command is bound to slave, legacy hardcoded apply mapping is used.

## Directory window (`GH_MB_DIR_BASE`)
- Base index: `1440` (SCADA `42440`).
- Size: `32` registers.

Register map (`base + off`):
- `+0` `MAP_VERSION` (`2` for current layout).
- `+1` `MAP_FLAGS` (bit0=`dir_valid`, bit1=`topology_active`).
- `+2..+3` `TOPOLOGY_GENERATION` (hi/lo).
- `+4` `POINT_COUNT`.
- `+5` `POINT_STRIDE`.
- `+6` `POINTS_BASE`.
- `+7` `SLAVE_STATUS_BASE`.
- `+8` `CMD_BASE`.
- `+9..+10` `DATA_VERSION` (hi/lo).
- `+11` `MAX_POINTS`.
- `+12` `CMD_BLOCK_SIZE`.
- `+13` `STATUS_BLOCK_SIZE`.

## Legacy config pipeline window (`GH_MB_CFG_BASE`)
- Base index: `1472` (SCADA `42472`).
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
- Base index: `1552` (SCADA `42552`).
- Size: `32` registers.
- Exposes boot/reset/error/network counters and key runtime diagnostics.
- Detailed meaning of diagnostics and codes: `docs/error_codes.md`.

## Topology upload window (`GH_MB_TOPO_BASE`)
- Base index: `1584` (SCADA `42584`).
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
- Points/status/directory/diagnostics windows are runtime-owned; clients should treat them as read-only.
- Command ingress window is client-written (`FC=6/16`) and consumed by `ModbusMasterTask` via `GH_ModbusMap_GetApplyRequest`.
- Legacy config window (`1472..1551`) request fields are written from TCP and consumed by `ConfigStorageTask`.
- Topology window (`1584..1727`) request fields are written from TCP and consumed by `ConfigStorageTask`.
- Config/topology result fields are written by backend tasks and read by TCP clients.

## Master to slave RS485/RTU cycle
- Master polls slave IDs `1..20`.
- One full poll cycle runs every `5` seconds (cycle budget control).

## Important
- Custom framed TCP protocol (`HELLO/STATUS/SNAPSHOT/EVENT`) is not implemented in current firmware.
- This file describes current firmware behavior, not a future target protocol.
