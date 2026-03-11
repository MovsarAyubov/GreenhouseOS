# SCADA Apply Runbook

Date: `2026-03-08`.

## Scope
- Safe client-side write sequence for `GH_MB_CMD_BASE` apply transactions.
- Migration rules for `EXPECTED_ACTIVE_CTRL_VERSION`.
- Rollout readiness checks for schedule apply.

## Preconditions
- Slave command row is selected by `slave_id`.
- `APPLY_TRIGGER` is treated as transaction commit and must be written last.
- Generic command ingress payload budget is `16` words.
- Topology contract for schedule is deployed on target slaves:
- `cmd_kind=schedule`, step 1: `FC16(start_reg=110, reg_count=12, payload_offset=0)`.
- `cmd_kind=schedule`, step 2: `FC6(start_reg=122, reg_count=1, payload_offset=12)`.

## Legacy algorithm (`CMD_KIND=0`)
1. Write legacy payload fields (`PAYLOAD[0..7]` and optional extension fields if used).
2. Optionally write `PAYLOAD[15]=0`.
3. Write `APPLY_TRIGGER` (last write in transaction).
4. Read back `LAST_APPLIED_TRIGGER` and diagnostics if needed.

## Schedule algorithm (`CMD_KIND=1`)
1. Write `PAYLOAD[0..11]` (`SCH0..SCH3`: `EN`, `ON_HHMM`, `OFF_HHMM`).
2. Write `PAYLOAD[12]` (`APPLY_VALUE` for slave reg `122`).
3. Write `PAYLOAD[13..14]` (`EXPECTED_ACTIVE_CTRL_VERSION` hi/lo).
4. Write `PAYLOAD[15]=1`.
5. Write `APPLY_TRIGGER` (last write in transaction).
6. Monitor result via `LAST_APPLIED_TRIGGER` and diagnostics.

## Migration mode for expected version
- Temporary mode: `PAYLOAD[13..14] = 0x00000000` disables version check.
- Firmware switch: `GH_SCHEDULE_VERSION_CHECK_ALLOW_DISABLED`.
- Strict mode target: set switch to `0`, require exact `ACTIVE_CTRL_VERSION == EXPECTED_ACTIVE_CTRL_VERSION`.

## Diagnostics watchlist
- `1301` `CTRL_SYNC_ACK_STATUS_FAIL`.
- `1302` `CTRL_SYNC_VERSION_MISMATCH`.
- `1303` `CTRL_SYNC_INVALID_CMD_KIND`.
- `1304` `CTRL_SYNC_INVALID_SCHEDULE`.
- `1305` `CTRL_SYNC_TOPOLOGY_CONTRACT`.
- `1306` `CTRL_SYNC_TRANSPORT_TIMEOUT`.

## Readiness criteria
- No `1302` caused by old clients during migration window.
- No `1305` on target slaves (topology contract deployed correctly).
- Equivalent schedule apply outcomes in topology and legacy environments under same payload and bus conditions.

## Rollout gates
1. Stand test: mixed old/new clients, verify zero false-positive applies.
2. Pilot slave: check error-code trend for 24h.
3. Group rollout: monitor `1301/1302/1305/1306` rates.
4. Full rollout: disable migration mode (`GH_SCHEDULE_VERSION_CHECK_ALLOW_DISABLED=0`), keep strict version check.
