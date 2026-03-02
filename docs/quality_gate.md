# Quality Gate

Date: `2026-02-23`

## Commands

Run full gate:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-QualityGate.ps1
```

Run without host unit tests (if `gcc` unavailable):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\quality\Run-QualityGate.ps1 -SkipUnitTests
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

## Gate criteria

1. Firmware build passes in both `Debug` and `Release` profiles.
2. Static-analysis compile passes (`Run-StaticAnalysis.ps1`).
3. Unit tests pass:
- CRC32 vectors
- Modbus map submit/queue/result path
- Config request validation (version/CRC/range)
4. HIL checklist scenarios are present in `docs/hil_scenarios.md`.
5. Runtime status includes stack and heap watermark telemetry.

## Notes

- Unit tests are host-side and use shim headers/stubs under `tests/unit/`.
- HIL scenarios are executed on hardware bench and tracked separately in release reports.
