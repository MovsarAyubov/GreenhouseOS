# Quality Gate

Date: `2026-03-08`

## Commands

Run full gate:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-QualityGate.ps1
```

Run without host unit tests (if `gcc` unavailable):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-QualityGate.ps1 -SkipUnitTests
```

Run Python integration/fault tests only:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-PythonTests.ps1
```

Run long TCP soak test (24-48h), with reconnect flap and optional background topology uploads:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-TcpSoakTest.ps1 `
  -Host 192.168.1.50 `
  -DurationHours 24 `
  -ReconnectEveryCycles 60 `
  -ReconnectProbability 0.05 `
  -Chunks .\build\topology\one_zone_chunks.json
```

Run bench integration probe for data-driven command path and fault probes:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-TcpIntegrationTest.ps1 `
  -Host 192.168.1.50 `
  -RunCommand `
  -CmdSlaveId 1 `
  -CmdModuleId 101 `
  -CmdProfileId 5001 `
  -CmdPayload "11,22,33" `
  -RunBusyProbe `
  -RunInvalidTopologyProbe
```

## Gate criteria

1. Firmware build passes in both `Debug` and `Release` profiles.
2. Static-analysis compile passes (`Run-StaticAnalysis.ps1`).
3. Unit tests pass:
- CRC32 vectors
- Modbus map submit/queue/result path
- Config request validation (version/CRC/range)
4. Python integration/fault tests pass:
- topology packer/uploader and Modbus TCP client transport behavior
- soak harness fault injection and retry semantics
5. HIL checklist scenarios are present in `docs/hil_scenarios.md`.
6. Runtime status includes stack and heap watermark telemetry.
7. Bench integration/fault campaign passes:
- command submit via generic ingress (`trigger` commit model)
- `REJECT_BUSY` single in-flight behavior
- invalid topology upload reject (`REJECT_TOPOLOGY_BOUNDS`)

## Notes

- Unit tests are host-side and use shim headers/stubs under `tests/unit/`.
- HIL scenarios are executed on hardware bench and tracked separately in release reports.
