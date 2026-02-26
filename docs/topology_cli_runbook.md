# Topology CLI Runbook

Date: `2026-02-26`

This runbook contains the exact PowerShell commands for:
- packing topology JSON
- uploading topology to controller
- verifying `generation` and diagnostics

## 1. Prerequisites

- Controller firmware is flashed and running.
- Controller IP is reachable (example: `192.168.50.20`).
- Python is installed and available in PATH.
- Workdir is project root:
  - `C:\Users\AUTcomp\STM32CubeIDE\workspace_1.17.0\greenhouseOS`

## 2. Network check

```powershell
Test-NetConnection 192.168.50.20 -Port 502
```

Expected: `TcpTestSucceeded : True`.

## 3. Build topology artifacts (packer)

```powershell
python tools/topology/topology_packer.py `
  --input .\build\topology\one_zone_topology.json `
  --output-bin .\build\topology\one_zone_topology.bin `
  --output-chunks .\build\topology\one_zone_chunks.json `
  --start-token 100 `
  --chunk-words 120
```

Expected output includes:
- `Packed topology blob: ... bytes`
- `Prepared chunk requests: ...`

## 4. Upload topology (uploader)

```powershell
python tools/topology/topology_uploader.py `
  --host 192.168.50.20 `
  --port 502 `
  --unit-id 1 `
  --chunks .\build\topology\one_zone_chunks.json `
  --timeout-s 10 `
  --poll-interval-ms 300 `
  --chunk-timeout-ms 15000 `
  --commit-timeout-ms 120000 `
  --io-retries 15
```

Expected output includes:
- `result=2 (APPLIED)`
- `Upload complete.`
- `Active flags: 0x0001`

## 5. Verify active generation

```powershell
@'
from tools.topology.topology_uploader import ModbusTcpClient, GH_MB_TOPO_BASE
with ModbusTcpClient("192.168.50.20", 502, 1, 5.0) as c:
    rc = c.read_holding_registers(GH_MB_TOPO_BASE + 1, 1)[0]
    af = c.read_holding_registers(GH_MB_TOPO_BASE + 3, 1)[0]
    ghi, glo = c.read_holding_registers(GH_MB_TOPO_BASE + 6, 2)
    gen = (ghi << 16) | glo
print("result_code =", rc)
print("active_flag =", af & 1)
print("generation =", gen)
'@ | python -
```

Interpretation:
- `active_flag = 1` and expected `generation` means topology is active.
- `result_code` may be `0` (`IDLE`) outside upload window; this is normal.

## 6. Verify with diagnostics (extended)

```powershell
@'
from tools.topology.topology_uploader import ModbusTcpClient, GH_MB_TOPO_BASE
GH_MB_DIAG_BASE = 1360
with ModbusTcpClient("192.168.50.20", 502, 1, 5.0) as c:
    rc = c.read_holding_registers(GH_MB_TOPO_BASE + 1, 1)[0]
    rt = c.read_holding_registers(GH_MB_TOPO_BASE + 2, 1)[0]
    af = c.read_holding_registers(GH_MB_TOPO_BASE + 3, 1)[0]
    ghi, glo = c.read_holding_registers(GH_MB_TOPO_BASE + 6, 2)
    b_hi, b_lo = c.read_holding_registers(GH_MB_DIAG_BASE + 0, 2)     # boot_count
    rr_hi, rr_lo = c.read_holding_registers(GH_MB_DIAG_BASE + 12, 2)  # last_reset_reason
print("result_code =", rc, "result_token =", rt)
print("active_flag =", af & 1, "generation =", (ghi<<16)|glo)
print("boot_count =", (b_hi<<16)|b_lo, "last_reset_reason =", (rr_hi<<16)|rr_lo)
'@ | python -
```

## 7. Power-cycle persistence check

1. Power off controller.
2. Wait 5-10 seconds.
3. Power on controller.
4. Run section 5 command again.

Pass criteria:
- `active_flag = 1`
- `generation` unchanged.

## 8. Common failure patterns

- `timed out` during upload:
  - keep `--io-retries 15`
  - keep long `--commit-timeout-ms`
  - re-run upload command
- `result != APPLIED`:
  - inspect reject code in output
  - re-pack JSON and upload again
