# SCADA Apply Runbook

Date: `2026-03-24`.

## Scope
- Safe client-side write sequence for `GH_MB_CMD_BASE` command transactions.
- Light-setpoints profile for current zone-slave contract with two independent relay channels.

## Preconditions
- Slave command row is selected by `slave_id`.
- `TRIGGER` is treated as transaction commit and must be written last.
- Generic command ingress payload budget is `16` words.
- Topology contract for current light-setpoints profile:
- `cmd_kind=generic`, one step: `FC16(start_reg=110, reg_count<=13, payload_offset=0)`.
- Separate `APPLY` register for this profile is not used.
- Recommended mode is a full `13`-word payload write.

## Light-Setpoints Algorithm (`CMD_KIND=0`)
1. Write `PAYLOAD[0]` (`LIGHT_RELAY_1_ENABLE`).
2. Write `PAYLOAD[1]` (`LIGHT_RELAY_1_ON_HHMM`).
3. Write `PAYLOAD[2]` (`LIGHT_RELAY_1_OFF_HHMM`).
4. Write `PAYLOAD[3]` (`LIGHT_RELAY_1_THRESHOLD_WM2`).
5. Write `PAYLOAD[4]` (`LIGHT_RELAY_1_RESERVED=0`).
6. Write `PAYLOAD[5]` (`LIGHT_RELAY_1_DLI_LIMIT`).
7. Write `PAYLOAD[6]` (`LIGHT_RELAY_2_ENABLE`).
8. Write `PAYLOAD[7]` (`LIGHT_RELAY_2_ON_HHMM`).
9. Write `PAYLOAD[8]` (`LIGHT_RELAY_2_OFF_HHMM`).
10. Write `PAYLOAD[9]` (`LIGHT_RELAY_2_THRESHOLD_WM2`).
11. Write `PAYLOAD[10]` (`LIGHT_RELAY_2_RESERVED=0`).
12. Write `PAYLOAD[11]` (`LIGHT_RELAY_2_DLI_LIMIT`).
13. Write `PAYLOAD[12]` (`LIGHT_HYST_SEC`).
14. Set `PAYLOAD_LEN=13`.
15. Write `TRIGGER` last.
16. Read back `LAST_APPLIED_TRIGGER/RESULT/IO_ERR`.

## Partial Update Mode
- Partial writes are allowed while `PAYLOAD_LEN <= 13`.
- Slave auto-applies a full `110..122` multi-write immediately.
- Slave auto-applies partial updates only after `250 ms` without new writes.
- Registers `114` and `120` are reserved; send `0` on full writes.
- Because the command profile always starts at slave reg `110`, partial writes update only the leading prefix `110..(110+PAYLOAD_LEN-1)`.
- If the client needs to change `relay_2` fields, use a full `13`-word write.
- Master acknowledges successful delivery of the write, not the exact delayed apply moment on slave.
- For deterministic behavior use full `13`-word writes.

## Diagnostics watchlist
- `RESULT=10` `REJECT_BOUNDS`: client sent `PAYLOAD_LEN > 13`.
- `RESULT=13` `REJECT_BUSY`: previous command still in flight on master.
- `RESULT=15` `TRANSPORT_FAIL`: RTU write to slave failed.
- `RESULT=16` `ACK_FAIL`: master did not finish command verification path.

## Readiness criteria
- No false-positive `REJECT_BOUNDS` for light-setpoints payload `LEN=13`.
- Full writes of `110..122` complete without transport errors.
- Partial writes apply on slave after the expected `250 ms` idle window.
- Client does not use register `122` as a standalone `APPLY` command.
