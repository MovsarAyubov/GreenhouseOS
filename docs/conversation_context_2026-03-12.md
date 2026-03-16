# Conversation Context: SCADA, Topology, Modbus RTU

## Purpose

This document captures the key conclusions, decisions, changes, and current status from the recent investigation of SCADA client issues, topology rollout, and RS485/Modbus RTU transport instability.

It is intended to serve as a compact handover/context document that can be referenced later without reconstructing the full chat history.

## Date

- Primary work period: 2026-03-11 to 2026-03-12

## Scope

The discussion covered four major areas:

1. SCADA client reading wrong Modbus map addresses after firmware map layout changes.
2. SCADA schedule apply failures caused by topology contract mismatch.
3. Creation and activation of a new topology for one zone and one weather station.
4. Investigation of intermittent `frame receive error. 5` on the ESP32 slave during Modbus RTU communication.

## 1. Client map mismatch and RTC failures

### Problem

The client showed:

- `RTC_SET not confirmed ... result=IDLE`
- `MAP_VERSION=0`, `POINT_STRIDE=0`
- weather and server time not reading correctly

### Root cause

The client was using the old Modbus map layout:

- old `DIR_BASE=1440`
- old `RTC_SET=1456..1460`

The current firmware publishes `MAP_VERSION=4` with a new layout:

- `DIR_BASE=1264`
- `RTC_HOUR=1278`
- `RTC_MINUTE=1279`
- `RTC_SET=1280..1284`

As a result, the client was reading and writing the wrong register ranges and seeing zeros or stale data.

### Decision

The client must bootstrap from the current directory window and stop using hardcoded legacy addresses.

### Relevant references

- `Core/Inc/gh_modbus_map.h`
- `Core/Src/gh_modbus_map.c`
- `docs/master_protocol.md`
- `docs/data_driven_runtime_handover.md`

## 2. Schedule send failures and topology-driven client model

### Problem

When sending a schedule, the client received:

- `result=11`

### Root cause

`result=11` is `GH_MB_DCMD_RESULT_REJECT_TOPOLOGY`, meaning the command request did not match the active topology contract.

The most likely failure modes were:

- wrong `module_id`
- wrong `cmd_profile_id`
- missing `commands[]` in active topology
- assumption that `module_id == slave_id`

The firmware uses logical `module_id` values, not `slave_id` values. For the first zone, the expected logical zone module is `101`, not `1`.

### Decision

The client must be topology-driven:

- resolve `module_id`, `point_id`, `publish_index`, and `cmd_profile_id` from topology
- stop relying on fixed register windows and manual `module_id/profile_id` entry
- use generic `CMD_BASE` data-driven command ingress

### Relevant references

- `docs/scada_apply_runbook.md`
- `docs/data_driven_runtime_handover.md`
- `docs/topology_config_v2.md`
- `docs/topology_module_contract_v1.md`
- `docs/greenhouse_profile_v1.md`

## 3. Weather station Modbus map

### Problem

A stable RTU map for the weather station was needed in order to describe points in topology and support the client.

### Action taken

The weather station Modbus map was added to:

- `docs/modbus_slave_table.md`

### Added weather map

- slave id: `20`
- registers:
  - `0 OUT_TEMP`
  - `1 OUT_HUM`
  - `2 WIND_SPEED`
  - `3 WIND_DIR`
  - `4 RAIN_FLAG`
  - `5 SOLAR_RAD`
  - `6 BARO_PRESS`
  - `7 DEW_POINT`
  - `8 STATUS_BITS`

### Outcome

This made it possible to define the weather module and point bindings without guessing offsets or scaling.

## 4. Created topology: one zone + one weather station + schedule commands

### Input assumptions used

- zone slave id: `1`
- weather slave id: `20`
- all 9 zone telemetry points are used
- required weather points for first version:
  - temperature
  - humidity
  - wind speed
  - wind direction
  - solar radiation
  - barometric pressure
- schedule commands are required

### Created artifacts

- `build/topology/one_zone_one_weather_schedule_topology.json`
- `build/topology/one_zone_one_weather_schedule_topology.bin`
- `build/topology/one_zone_one_weather_schedule_topology_chunks.json`
- `build/topology/one_zone_one_weather_schedule_semantics.json`

### Topology summary

- modules: `2`
  - zone module: `module_id=101`, `slave_id=1`
  - weather module: `module_id=201`, `slave_id=20`
- requests: `2`
- points: `15`
  - zone points: `9`
  - weather points: `6`
- commands: `2`
  - `cmd_id=5001`, `FC16`, `start_reg=110`, `reg_count=12`
  - `cmd_id=5002`, `FC6`, `start_reg=122`, `reg_count=1`
- topology generation: `101`
- topology version: `2.0`
- blob size: `540 bytes`

### Outcome

The topology was packed successfully and later confirmed as active on the controller by metadata match:

- `active_flags = 0x1`
- `ver_major = 2`
- `ver_minor = 0`
- `generation = 101`
- `size_bytes = 540`

## 5. Files to provide to the client

### localTopology

The client should use:

- `build/topology/one_zone_one_weather_schedule_topology.json`

### semantic mapping

The client should also use:

- `build/topology/one_zone_one_weather_schedule_semantics.json`

### Purpose of each file

- `topology.json`: runtime structure of modules, requests, points, commands
- `semantics.json`: client-side semantic names and UI bindings such as:
  - `zone_1.air_temp`
  - `weather_1.out_temp`
  - `zone_1.schedule`

## 6. RS485 / Modbus RTU transport issue: `frame receive error. 5`

### Problem

The ESP32 slave intermittently logs:

- `frame receive error. 5`

This occurs not only during schedule apply, but also during normal telemetry operation.

### Meaning of the error

In `esp-modbus`, this log corresponds to:

- `status = MB_EIO`

This means the slave received a frame fragment or frame candidate but rejected it because:

- the frame length was invalid, or
- the CRC check failed

Relevant code path:

- `managed_components/espressif__esp-modbus/modbus/mb_objects/mb_slave.c`
- `managed_components/espressif__esp-modbus/modbus/mb_transports/rtu/rtu_slave.c`
- `managed_components/espressif__esp-modbus/modbus/mb_objects/common/mb_types.h`

### Confirmed facts

- master UART config: `19200 8N1`
- slave UART config: `19200 8N1`
- line length: about `50 cm`
- A/B polarity confirmed correct
- common GND was added later
- issue still persists intermittently

### Transport-related findings

#### 6.1 Inter-frame gap was too short

The master originally used:

- `MODBUS_INTER_SLAVE_DELAY_MS = 1`

At `19200 8N1`, the Modbus RTU minimum silent interval is about `3.5 chars`, which is about `1.82 ms`.

This could cause frame boundary problems, especially between successive commands.

#### Action taken

A dedicated frame gap constant was added:

- `MODBUS_RTU_FRAME_GAP_MS = 3`

And a delay was inserted after successful request/response transactions in:

- `Core/Inc/gh_runtime_state.h`
- `Core/Src/gh_modbus_io.c`

#### 6.2 Master RTU TX was initially interrupt-driven

The master originally transmitted via:

- `HAL_UART_Transmit_IT(...)`

This can produce intra-frame gaps if interrupt service is delayed while a long RTU frame is being sent.

On the ESP32 side, the `esp-modbus` serial port logic uses:

- `MB_SERIAL_TOUT = 3`

and treats this timeout as end-of-frame.

If the master introduces a pause inside a frame, the ESP32 may split one RTU request into multiple pieces and then reject CRC/length.

#### Action taken

The master RTU TX path was switched from interrupt-driven TX to blocking TX:

- replaced `HAL_UART_Transmit_IT(...)`
- with `HAL_UART_Transmit(...)`

The `DE/RE` pin is still released only after `UART_FLAG_TC`.

Modified file:

- `Core/Src/gh_modbus_io.c`

### Current status of the RTU issue

The problem became less coupled to obvious schedule operations, but intermittent receive errors are still reported by the slave after the patch and after adding common GND.

### Current leading hypothesis

The remaining errors are still likely transport-level and are most likely caused by one or both of the following:

1. The STM32 master TX path is still not sufficiently continuous for the ESP32 slave timeout sensitivity.
   - blocking polling TX improved the situation but may still allow observable inter-byte pauses
2. The ESP32 slave timeout is aggressive:
   - `MB_SERIAL_TOUT = 3`
   - this makes frame boundary detection sensitive to short pauses

### Secondary hardware risks still worth checking

- actual transceiver type on ESP32 side
- whether the RS485 module is a classic `MAX485`
- whether the module is powered from `3.3V` or `5V`
- whether the ESP32 RX is exposed to 5V `RO`
- presence or absence of bus biasing resistors

Short line length makes missing terminators a low-priority issue. Biasing and signaling levels are more relevant than termination in the current setup.

## 7. Code and documentation changes made

### Documentation added or updated

- updated `docs/modbus_slave_table.md` with weather station Modbus map
- created topology artifacts in `build/topology`
- created semantic mapping in `build/topology`
- this context document added in `docs/conversation_context_2026-03-12.md`

### Firmware transport changes

- added `MODBUS_RTU_FRAME_GAP_MS = 3` in `Core/Inc/gh_runtime_state.h`
- added post-transaction frame gap handling in `Core/Src/gh_modbus_io.c`
- replaced RTU TX `HAL_UART_Transmit_IT` with blocking `HAL_UART_Transmit` in `Core/Src/gh_modbus_io.c`

### Validation status

- topology artifacts were successfully packed
- topology metadata on controller matched the created topology
- `gh_modbus_io.c` was compiled as a single file after the TX change
- full project `make` was not completed in this environment because local MSYS/Cygwin `make.exe` failed before build execution

## 8. Recommended next steps

### For client side

1. Use `MAP_VERSION=4` directory bootstrap.
2. Use `build/topology/one_zone_one_weather_schedule_topology.json` as `localTopology`.
3. Use `build/topology/one_zone_one_weather_schedule_semantics.json` for semantic/UI binding.
4. Stop using legacy fixed map addresses and manual `module_id/profile_id` entry.

### For RTU stability

1. Increase slave-side `MB_SERIAL_TOUT` from `3` to `5` or `6` and retest.
2. If intermittent errors remain, move master RTU TX from CPU-driven blocking TX to UART TX DMA.
3. If needed, add extra diagnostics:
   - raw bad-frame length logging on ESP32
   - logic analyzer capture on STM32 TX / MAX485 DI

## 9. Current practical status summary

- SCADA client address mismatch cause was identified.
- Topology-driven schedule rejection cause was identified.
- A new one-zone + one-weather topology was created and matched against the active controller metadata.
- Client-side topology and semantic files are available.
- The RS485 `frame receive error. 5` problem is not fully resolved yet.
- The current unresolved area is the RTU transport layer, not topology or register map semantics.

## 10. Modbus TCP persistent-connection investigation update (2026-03-15)

### New symptom focus

After the topology and client-concurrency hypotheses were narrowed down, the remaining TCP issue was treated as a server-side persistent-session problem.

Observed pattern:

- requests are sequential
- the same TCP socket is reused
- the controller sometimes does not answer within one established TCP session

This means the next diagnostic step is not UI logic and not multi-request client overlap. The focus moved to Modbus TCP session state, request parsing, and response sending on the controller.

### Server-side areas selected for investigation

The investigation was centered around these code paths:

- TCP slave initialization in `Core/Src/gh_modbus_tcp_server.c`
- TCP stack limits in `Core/Inc/ModbusConfig.h`
- TCP receive / request parsing in `Core/Src/Modbus.c`
- MBAP frame extraction in `TCPextractFrame()`
- TCP response sending in `sendTxBuffer()`
- runtime diagnostics exposure in `Core/Src/gh_modbus_map.c`

### Instrumentation added to the firmware

Temporary trace instrumentation was added to the TCP server path so that one persistent-connection timeout can be localized to a specific stage.

The trace now records the following points:

- `accept`
- `recv`
- `frame`
- `send`
- `malformed`
- `close`

The stored per-event fields include:

- tick in ms
- connection index
- connection pointer
- transaction ID
- RX length before / after processing
- MBAP length
- function code
- start register
- register quantity
- receive error
- send error
- bytes written

Implementation notes:

- trace storage and per-connection request metadata were added in `Core/Inc/Modbus.h` and `Core/Src/Modbus.c`
- the runtime map now exposes a read-only TCP trace window at base address `1552`
- clearing TCP diagnostics now also clears the trace buffer
- `docs/master_protocol.md` was updated with the trace register layout and event codes

### Concrete defect found in the TCP send path

A real server-side defect was found by static inspection in the TCP response path.

Previous behavior:

- `netconn_write_vectors_partly()` could report success while writing fewer bytes than the full Modbus TCP response
- partial write was not treated as an error
- this could leave the connection in an ambiguous state while the client still believed the session was healthy

Current behavior after the fix:

- partial TCP write is treated as a send failure
- it increments `TCP_SEND_ERR_COUNT`
- it stores `TCP_LAST_ERR = -1001`
- it records `send` and `close` trace events
- it closes the TCP connection explicitly after the failed send

This does not prove that partial write is the only cause of the field timeout, but it removes one real defect from the persistent-session path and makes future failures classifiable.

### Minimal transport probe added

A dedicated client-side probe was added for a pure transport test without topology/UI behavior:

- `tools/quality/tcp_persistent_probe.py`

Default request cycle:

- `1264, 32`
- `1080, 125`
- `1205, 35`

Probe behavior:

- uses one persistent TCP socket by default
- logs request ID, transaction ID, and request duration
- after a timeout, reconnects and reads controller diagnostics
- after a timeout, also reads the new TCP trace window

Supported isolation modes:

- reconnect per request
- fixed gap between requests
- single repeated address
- small FC3-only runs

### Validation completed in the workspace

The following was validated locally:

- the new Python probe tests passed
- related Python utilities compiled successfully
- the modified firmware sources compiled successfully
- a full `Debug` firmware build succeeded with the bundled STM32CubeIDE GNU toolchain

One environment note remains important:

- the separate local ARM GCC installation failed on `-fcyclomatic-complexity`
- this was a toolchain compatibility issue, not a regression introduced by the TCP instrumentation patch

### What the next hardware run should decide

The next controller run with the new probe and trace should answer exactly one of these questions:

1. The server never received the next request.
2. The server received data but did not extract a valid MBAP frame.
3. The server extracted the frame but did not complete the send path.
4. The server sent the response and the remaining defect is on the client receive side.

At this point the issue is no longer described only as "persistent TCP bug suspected". The system now has enough diagnostics to localize it to:

- parser defect in `TCPextractFrame()`
- send defect in `sendTxBuffer()`
- receive-timeout / netconn configuration defect
- session-state defect in legacy `Modbus.c`
- client-side receive defect if the server trace shows a successful send
