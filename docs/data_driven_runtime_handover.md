# Data-Driven Runtime Handover (as of March 8, 2026)

## 1. Context

Project goal is to move Modbus master runtime to an only data-driven model based on `topology v2` (`poll/points/commands/policies`), with deterministic behavior when topology is invalid or missing.

Original runtime had mixed behavior:

- topology path for polling and part of commands
- legacy fallback cycle (`hardcoded` polling/apply)
- schedule-specific ingress window and pipeline

This handover summarizes implemented migration stages and current contract.

## 2. Migration Status

Current state:

- Stage 1 implemented:
  - topology is mandatory for runtime path by default
  - automatic runtime fallback to legacy cycle disabled (safe mode path by default)
- Stage 2 implemented:
  - unified generic command ingress `SCADA -> master`
  - map-level request staging and trigger-based commit
- Stage 3 implemented:
  - topology command schema extended with `payload_offset`
  - command payload budget decoupled from legacy map window (`TOPOLOGY_CMD_PAYLOAD_BUDGET_WORDS`)
  - stricter topology validation for command payload bounds and command window collisions
- Stage 4 implemented:
  - schedule execution moved into generic topology command dispatcher
  - no separate schedule runtime pipeline in master task
  - schedule runs as `cmd_kind=schedule` command profile sequence
- Stage 5 implemented:
  - legacy runtime path removed from `gh_modbus_master.c`
  - obsolete hardcoded apply chain removed from `gh_modbus_io.c`
  - obsolete legacy runtime constants/types removed from `gh_runtime_state.h`
- Stage 6 implemented:
  - legacy schedule compatibility window removed from map layout
  - deprecated legacy map ingress APIs removed
  - `MAP_VERSION` bumped to `4`
- Stage 7 (software scope) implemented:
  - host unit/integration/fault regression suite extended
  - quality gate updated to include Python integration/fault stage
  - map-contract defaults in host tools aligned with `MAP_VERSION=4` addresses
  - bench integration/soak runbooks and templates added

Still pending:

- Stage 7 hardware scope (bench integration + 24h soak evidence)
- Stage 8 release/rollout execution
- command ingress is still single in-flight request (`REJECT_BUSY`), no queueing policy yet

## 3. What Was Implemented

### 3.1 Runtime gating and safe mode (Stage 1)

File: `Core/Src/gh_modbus_master.c`

- `gh_run_topology_cycle(...)` explicit states:
  - `GH_TOPOLOGY_CYCLE_OK`
  - `GH_TOPOLOGY_CYCLE_INVALID`
  - `GH_TOPOLOGY_CYCLE_EMPTY`
- `gh_run_safe_mode(...)` for invalid/empty topology:
  - heartbeat continues
  - fail streaks increase deterministically
  - quality switches to `STALE/OFFLINE` by threshold
  - timeout diagnostics are updated
  - diagnostic event emitted on entry/reason change (`1305`)

### 3.2 Unified generic command ingress (Stage 2)

Files:

- `Core/Inc/gh_modbus_map.h`
- `Core/Src/gh_modbus_map.c`
- `Core/Src/gh_modbus_master.c`

Contract:

- single global command block:
  - `GH_MB_CMD_REGS = GH_MB_CMD_BLOCK_SIZE`
  - `GH_MB_CMD_BLOCK_SIZE = 4 + GH_MB_CMD_PAYLOAD_WORDS + 4`
- fields:
  - request: `trigger`, `slave_id`, `module_id`, `cmd_profile_id`, `payload_len`, `payload[]`
  - result: `last_applied_trigger`, `result`, `io_err`
- currently `GH_MB_CMD_PAYLOAD_WORDS = 16`

Commit model:

- client writes request fields
- client writes `trigger` last
- map snapshots pending request
- runtime processes request and writes result fields

Guards:

- command-window bounds validation
- runtime-owned result fields are read-only for client writes
- `payload_len` bounds validation
- duplicate trigger suppression
- single in-flight request (`REJECT_BUSY`)

### 3.3 Topology schema/runtime extension and strict validation (Stage 3)

Files:

- `Core/Inc/gh_topology_v2.h`
- `Core/Inc/gh_topology_runtime.h`
- `Core/Src/gh_config_storage.c`
- `Core/Src/gh_modbus_master.c`

Implemented:

- command schema extended:
  - `gh_topology_v2_cmd_t.payload_offset`
- runtime command binding extended:
  - `gh_topology_cmd_binding_t.payload_offset`
- command payload budget is now independent runtime constant:
  - `TOPOLOGY_CMD_PAYLOAD_BUDGET_WORDS`
- validator rules:
  - `payload_offset + max_reg_count <= TOPOLOGY_CMD_PAYLOAD_BUDGET_WORDS`
  - command register windows must not overlap within same module (`start_reg/max_reg_count`)

Execution behavior:

- command payload slice is selected by profile (`payload_offset`, `reg_count`)
- FC6/FC16 execution uses that slice, not hardcoded payload start

### 3.4 Schedule via generic command dispatcher (Stage 4)

Files:

- `Core/Src/gh_modbus_master.c`
- `Core/Src/gh_config_storage.c`
- `Core/Inc/gh_topology_runtime.h`

Command kind model:

- `cmd_kind` encoded in command flags bits `[15:14]`:
  - `0` = generic command
  - `1` = schedule command
- helper macros:
  - `GH_TOPOLOGY_CMD_KIND_FROM_FLAGS(...)`
  - `GH_TOPOLOGY_CMD_FLAG_KIND_MASK/SHIFT`

Schedule contract in topology profiles:

- step 1:
  - `cmd_kind=schedule`, `FC16`, `start_reg=110`, `max_reg_count=12`, `payload_offset=0`
- step 2:
  - `cmd_kind=schedule`, `FC6`, `start_reg=122`, `max_reg_count=1`, `payload_offset=12`
- extra request words:
  - `payload[13..14]` expected active version hi/lo
  - `payload[15]` command kind marker (`1`)

Runtime behavior:

- generic dispatcher detects `cmd_kind=schedule`
- validates schedule payload and HHMM slots
- executes schedule sequence using topology steps
- reads apply status (`reg 125..127`) and validates:
  - apply status == `APPLY_APPLIED`
  - active version matches expected (with `GH_SCHEDULE_VERSION_CHECK_ALLOW_DISABLED` semantics)

Validator additions:

- `cmd_kind` must be in supported range
- for schedule `FC16`, `max_reg_count` must be `12`

Result:

- schedule-specific runtime executor path removed from master runtime logic
- schedule now flows through same generic data-driven command executor

### 3.5 Stage 5/6 cleanup result

- `gh_run_legacy_cycle(...)` removed from runtime task path
- legacy apply chain helpers removed from `gh_modbus_io.c`
- legacy schedule map window removed from layout (`GH_MB_TOTAL_REGS` no longer includes schedule block)
- legacy map ingress APIs removed from `gh_modbus_map.h/.c`
- directory now reports `MAP_VERSION=4`
- map constants after Stage 6:
  - `GH_MB_MAP_VERSION = 4`
  - `GH_MB_TOTAL_REGS = 1552`
  - removed from public contract: `GH_MB_SCHED_*` and `GH_MB_DIR_OFF_SCHED_*`

## 4. Result Codes (Data-Driven Command)

Defined in `Core/Inc/gh_modbus_map.h`:

- `GH_MB_DCMD_RESULT_IDLE`
- `GH_MB_DCMD_RESULT_QUEUED`
- `GH_MB_DCMD_RESULT_APPLIED`
- `GH_MB_DCMD_RESULT_REJECT_BOUNDS`
- `GH_MB_DCMD_RESULT_REJECT_TOPOLOGY`
- `GH_MB_DCMD_RESULT_REJECT_FC`
- `GH_MB_DCMD_RESULT_REJECT_BUSY`
- `GH_MB_DCMD_RESULT_REJECT_PARTIAL`
- `GH_MB_DCMD_RESULT_TRANSPORT_FAIL`
- `GH_MB_DCMD_RESULT_ACK_FAIL`

## 5. Current Runtime Contract for SCADA

Command submit sequence:

1. Write request fields into `GH_MB_CMD_BASE + offsets`.
2. Write `trigger` last.
3. Poll `last_applied_trigger/result/io_err`.
4. Match by trigger.

Required fields:

- `slave_id`, `module_id`, `cmd_profile_id`, `payload_len`, `payload[...]`

Current payload limits:

- `1 <= payload_len <= 16`

Schedule payload contract (generic ingress):

- `payload[0..11]`: schedule slots (`EN`, `ON_HHMM`, `OFF_HHMM`)
- `payload[12]`: apply value
- `payload[13..14]`: expected active version hi/lo
- `payload[15]`: `cmd_kind=1`

Notes:

- only one command can be pending at once (`REJECT_BUSY`)
- if topology runtime is not ready, command is rejected (`REJECT_TOPOLOGY`)
- `MAP_VERSION` in directory (`+0`) is now `4`
- legacy schedule compatibility window and its directory offsets are removed from map contract

## 6. Testing Already Performed

Executed locally after Stage 5/6 updates:

- `python -m unittest tools/topology/tests/test_topology_packer.py`
- `tools/quality/Run-UnitTests.ps1`
- `tools/quality/Build-Firmware.ps1`

Status:

- topology packer tests passed
- unit tests passed
- firmware build succeeded

Updated test coverage includes:

- `tests/unit/test_modbus_map.c` (MAP_VERSION=4 directory check, dynamic command ingress, single in-flight behavior)
- `tests/unit/test_config_storage.c` (payload offset bounds, command collisions, schedule command schema checks)
- `tools/topology/tests/test_topology_packer.py` (`cmd_kind`, schedule FC16 constraints, payload window bounds)

## 6.1 Stage 7 Software Completion (current changes)

Added regression artifacts for Stage 7 test/fault coverage:

- C unit coverage extensions:
  - `tests/unit/test_modbus_map.c`
  - added checks for duplicate trigger suppression, reject partial/bounds command submit, and read-only protection for runtime-owned command result fields.
- Python integration/fault tests:
  - `tools/topology/tests/test_modbus_tcp_client.py`
  - validates `ModbusTcpClient` wire behavior over real localhost socket, including roundtrip FC3/FC6/FC16 and fault injection (`tx_id` mismatch, peer close).
  - `tools/quality/tests/test_tcp_soak_test.py`
  - validates soak harness behavior for success path, transient transport fault with recovery, persistent fault exit code, and CLI argument guards.
  - `tools/quality/tests/test_tcp_integration_probe.py`
  - validates integration probe logic for directory contract checks, command path, busy pressure behavior, and invalid topology reject probe.
- Quality automation:
  - new `tools/quality/Run-PythonTests.ps1`
  - `tools/quality/Run-QualityGate.ps1` now includes Python test stage.
  - new hardware-bench wrapper `tools/quality/Run-TcpIntegrationTest.ps1` for stage-7 integration/fault campaign.
- Tooling contract alignment:
  - `tools/topology/topology_uploader.py` default `GH_MB_TOPO_BASE` corrected to `1408` (MAP_VERSION=4 layout).
  - `tools/quality/tcp_soak_test.py` diagnostics default base corrected to `1376`.

Local verification status (`2026-03-08`):

- `powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-QualityGate.ps1` passed end-to-end.
- includes firmware build (`Debug` + `Release`), static-analysis compile, host unit tests, Python integration/fault tests, and HIL scenarios checklist presence.
- `powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-PythonTests.ps1` passed:
  - topology tests: `15`
  - quality tests: `8` (includes soak harness and integration probe tests)

Stage 7 remaining execution items (hardware-dependent):

- Full integration chain on bench: `SCADA -> TCP -> map -> master -> RTU` (telemetry + command + schedule).
- Long soak run `24h` with mixed workload and watchdog/heap/stack HWM evidence in release report.

## 7. Known Gaps / Technical Debt

1. Command ingress is single in-flight request (no queueing policy yet).
2. Stage 7 hardware evidence is not yet attached (bench traces + completed 24h soak report).
3. Stage 8 release/rollout artifacts are not yet executed/published.

## 8. Recommended Next Steps

1. Complete Stage 7 hardware campaign:
   - run end-to-end schedule-through-profile scenarios and archive traces
   - execute 24h soak campaign and publish watchdog/heap/stack HWM report
2. Start Stage 8 release/rollout:
   - release A package (topology-only runtime already in repo)
   - rollout/cutover checklist with rollback to release A
   - release B planning gate after hardware evidence is accepted

## 9. Quick File Guide for Next Developer

- Runtime loop, safe mode, generic/schedule dispatcher:
  - `Core/Src/gh_modbus_master.c`
- Command ingress map contract and validation:
  - `Core/Inc/gh_modbus_map.h`
  - `Core/Src/gh_modbus_map.c`
- Topology validator/runtime binding:
  - `Core/Inc/gh_topology_v2.h`
  - `Core/Inc/gh_topology_runtime.h`
  - `Core/Src/gh_config_storage.c`
- Topology host tooling:
  - `tools/topology/topology_packer.py`
  - `tools/topology/topology_uploader.py`
- Quality/integration tooling:
  - `tools/quality/Run-QualityGate.ps1`
  - `tools/quality/Run-PythonTests.ps1`
  - `tools/quality/Run-TcpIntegrationTest.ps1`
  - `tools/quality/Run-TcpSoakTest.ps1`
  - `tools/quality/tcp_integration_probe.py`
  - `docs/soak_report_template.md`
- Tests:
  - `tests/unit/test_modbus_map.c`
  - `tests/unit/test_config_storage.c`
  - `tools/topology/tests/test_topology_packer.py`
  - `tools/topology/tests/test_modbus_tcp_client.py`
  - `tools/quality/tests/test_tcp_soak_test.py`
  - `tools/quality/tests/test_tcp_integration_probe.py`
