# Master Protocol Baseline (STM32F407)

Date: `2026-03-08`.

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
- Command ingress window: `GH_MB_CMD_REGS = 24` (`1240..1263`).
- Directory window: `GH_MB_DIR_REGS = 32` (`1264..1295`).
- Config window: `GH_MB_CFG_REGS = 80` (`1296..1375`).
- Diagnostics window: `GH_MB_DIAG_REGS = 32` (`1376..1407`).
- Topology upload window: `GH_MB_TOPO_REGS = 144` (`1408..1551`).
- Total: `GH_MB_TOTAL_REGS = 1552`.

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
- Block size: `24` registers.

Register map (`base + off`):
- `+0` `TARGET_SLAVE_ID` (W).
- `+1` `TARGET_MODULE_ID` (W).
- `+2` `CMD_PROFILE_ID` (W).
- `+3` `PAYLOAD_LEN` (W).
- `+4..+19` `PAYLOAD[16]` (W).
- `+20` `TRIGGER` (W, commit field; write last).
- `+21` `LAST_APPLIED_TRIGGER` (R).
- `+22` `RESULT` (R).
- `+23` `IO_ERR` (R).

Runtime behavior:
- Single in-flight request model: second trigger while pending yields `REJECT_BUSY`.
- Runtime consumes request through `GH_ModbusMap_GetDataDrivenCommandRequest(...)`.
- Result path writes `LAST_APPLIED_TRIGGER/RESULT/IO_ERR` via `GH_ModbusMap_MarkDataDrivenCommandResult(...)`.

## Directory window (`GH_MB_DIR_BASE`)
- Base index: `1264` (SCADA `42264`).
- Size: `32` registers.

Register map (`base + off`):
- `+0` `MAP_VERSION` (`4` for current layout).
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
- `+14` `RTC_HOUR` (`0..23`).
- `+15` `RTC_MINUTE` (`0..59`, updated at most once per minute).
- `+16` `RTC_SET_HOUR` (`0..23`, W).
- `+17` `RTC_SET_MINUTE` (`0..59`, W).
- `+18` `RTC_SET_TOKEN` (W): non-zero changed token triggers RTC set.
- `+19` `RTC_SET_APPLIED_TOKEN` (R): last processed token.
- `+20` `RTC_SET_RESULT` (R): `0=IDLE`, `1=QUEUED`, `2=APPLIED`, `3=REJECT_RANGE`, `4=FAILED`.
- `+21..+22` `RTC_SYNC_ATTEMPT_COUNT` (R, hi/lo, master -> slave sync attempts).
- `+23..+24` `RTC_SYNC_SUCCESS_COUNT` (R, hi/lo).
- `+25..+26` `RTC_SYNC_FAIL_COUNT` (R, hi/lo).
- `+27` `RTC_SYNC_LAST_SLAVE_ID` (R).
- `+28` `RTC_SYNC_LAST_TOKEN` (R).
- `+29` `RTC_SYNC_LAST_RESULT` (R): last slave result code, or internal master error marker (`0xFFFE` write fail, `0xFFFD` ack timeout).

## Config pipeline window (`GH_MB_CFG_BASE`)
- Base index: `1296` (SCADA `42296`).
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
- Base index: `1376` (SCADA `42376`).
- Size: `32` registers.
- Exposes boot/reset/error/network counters and key runtime diagnostics.
- Detailed meaning of diagnostics and codes: `docs/error_codes.md`.

Register map (`base + off`):
- `+0..+1` `BOOT_COUNT`.
- `+2..+3` `POWERON_COUNT`.
- `+4..+5` `ERROR_HANDLER_COUNT`.
- `+6..+7` `WDG_MISS_COUNT`.
- `+8..+9` `FAULT_RESET_COUNT`.
- `+10..+11` `LAST_EVENT_CODE`.
- `+12..+13` `LAST_RESET_REASON`.
- `+14..+15` `LAST_ERROR_CODE`.
- `+16..+17` `MODBUS_TIMEOUTS[0]`.
- `+18..+19` `MODBUS_TIMEOUTS[1]`.
- `+20..+21` `TCP_ACCEPT_ERR_COUNT`.
- `+22..+23` `TCP_RECV_TIMEOUT_COUNT`.
- `+24..+25` `TCP_STALE_CLOSE_COUNT`.
- `+26..+27` `TCP_MALFORMED_MBAP_COUNT`.
- `+28..+29` `TCP_SEND_ERR_COUNT`.
- `+30..+31` `TCP_LAST_ERR` (signed `int32`, `lastRecvErr` with fallback to `lastSendErr`; `-1001` means partial TCP write detected on response send).

## Topology upload window (`GH_MB_TOPO_BASE`)
- Base index: `1408` (SCADA `42408`).
- Size: `144` registers.
- Used for chunked upload of `topology_config v2` blob.

Register map (`base + off`):
- `+0` `SUBMIT_TOKEN` (W): non-zero changed token enqueues one chunk request to `qTopologyStoreHandle`.
- `+1` `RESULT_CODE` (R): last topology pipeline result.
- `+2` `RESULT_TOKEN` (R): token for the last processed topology request.
- `+3` `ACTIVE_FLAGS` (R): bit0=`topology_active`, bit1=`commit_in_progress`.
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

## TCP trace window (`GH_MB_TCP_TRACE_BASE`)
- Base index: `1552` (SCADA `42552`).
- Size: `121` registers.
- Read-only debug window with the latest Modbus TCP server trace entries, newest first.
- Intended for bench diagnostics of persistent-connection issues; not required for normal SCADA polling.

Register map (`base + off`):
- `+0` `ENTRY_COUNT` (`0..6` valid entries).
- `+1..+20` `ENTRY[0]` (newest).
- `+21..+40` `ENTRY[1]`.
- `+41..+60` `ENTRY[2]`.
- `+61..+80` `ENTRY[3]`.
- `+81..+100` `ENTRY[4]`.
- `+101..+120` `ENTRY[5]` (oldest retained).

Each `ENTRY[n]` block (`20` registers):
- `+0` `SEQ`.
- `+1` `EVENT`: `1=accept`, `2=recv`, `3=frame`, `4=send`, `5=malformed`, `6=close`.
- `+2` `CONN_INDEX`.
- `+3` `TRANSACTION_ID`.
- `+4` `RX_LEN_BEFORE`.
- `+5` `RX_LEN_AFTER`.
- `+6` `MBAP_LENGTH`.
- `+7` `FC`.
- `+8` `START_REG`.
- `+9` `QTY`.
- `+10..+11` `TICK_MS`.
- `+12..+13` `CONN_PTR`.
- `+14..+15` `RECV_ERR` (signed `int32`).
- `+16..+17` `SEND_ERR` (signed `int32`).
- `+18..+19` `IO_LEN`: bytes received in `recv` or bytes written in `send`.

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
- Points/status/diagnostics windows are runtime-owned; clients should treat them as read-only.
- Directory window is runtime-owned except `RTC_SET_HOUR`, `RTC_SET_MINUTE`, `RTC_SET_TOKEN` (`+16..+18`), which are client-written.
- Command ingress window is client-written (`FC=6/16`) and consumed by `ModbusMasterTask` via `GH_ModbusMap_GetDataDrivenCommandRequest`.
- Config window (`1296..1375`) request fields are written from TCP and consumed by `ConfigStorageTask`.
- Topology window (`1408..1551`) request fields are written from TCP and consumed by `ConfigStorageTask`.
- Config/topology result fields are written by backend tasks and read by TCP clients.

## Master to slave RS485/RTU cycle
- Master executes topology-driven RTU1 `requests[]` and only polls slaves present in the active poll plan.
- One full poll cycle runs every `5` seconds (cycle budget control).

## Important
- Custom framed TCP protocol (`HELLO/STATUS/SNAPSHOT/EVENT`) is not implemented in current firmware.
- This file describes current firmware behavior, not a future target protocol.
