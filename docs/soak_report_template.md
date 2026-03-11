# Soak Report Template (24h)

Date: `2026-03-08`

Use this template for Stage 7 hardware soak evidence.

## 1. Run metadata

- Device/firmware build:
- Controller IP:
- Start UTC:
- End UTC:
- Duration hours:
- Operator:

## 2. Command line

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-TcpSoakTest.ps1 `
  -Host <controller-ip> `
  -DurationHours 24 `
  -ReconnectEveryCycles 60 `
  -ReconnectProbability 0.05 `
  -Chunks .\build\topology\one_zone_chunks.json
```

Optional bench integration probe before soak:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-TcpIntegrationTest.ps1 `
  -Host <controller-ip> `
  -RunCommand `
  -RunBusyProbe `
  -RunInvalidTopologyProbe
```

## 3. Summary metrics

- cycles:
- reads_ok:
- reads_failed:
- retry_ok:
- retry_failed:
- reconnect_forced:
- reconnect_random:
- upload_runs:
- upload_failed:

## 4. Pass/fail criteria

- `retry_failed == 0`
- no watchdog reset events during soak window
- no unexpected growth in `last_error_code` critical events
- no heap/stack watermark regression vs baseline
- no command pipeline stalls (`LAST_APPLIED_TRIGGER` keeps advancing in control checks)

## 5. Diagnostics snapshots

Capture at least:

- soak start
- every 6 hours
- soak end

For each snapshot:

- boot/power/reset counters
- watchdog miss count
- modbus timeout counters
- tcp accept/recv/stale/malformed/send counters
- last tcp error
- stack HWM metrics
- heap free/min-ever

## 6. Fault notes

Record injected conditions and observed behavior:

- network reconnect events
- topology upload during soak
- intentional command bursts (`REJECT_BUSY` behavior)
- any timeouts or recoveries

## 7. Conclusion

- Soak result: `PASS` / `FAIL`
- Blocking issues:
- Follow-up tasks:
