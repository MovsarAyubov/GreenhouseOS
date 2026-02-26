# Topology Uploader Host Tool

Date: `2026-02-25`.

## Purpose

`tools/topology/topology_uploader.py` uploads prepared topology chunks to controller over Modbus TCP (`port 502`).

Input format must match `tools/topology/topology_packer.py` output (`topology_chunks.json`).

## Usage

```powershell
python tools/topology/topology_uploader.py `
  --host 192.168.50.20 `
  --port 502 `
  --unit-id 1 `
  --chunks .\build\topology\topology_chunks.json
```

Optional timing controls:
- `--poll-interval-ms`
- `--chunk-timeout-ms`
- `--commit-timeout-ms`
- `--timeout-s` (socket timeout)
- `--io-retries` (retries per Modbus operation on transient timeout/network errors)

## Behavior

For each chunk, uploader writes:
- metadata (`chunk_index`, `chunk_words`, `total_size`, `chunk_crc32`, `flags`, `generation`)
- payload words
- `SUBMIT_TOKEN`

Then it polls:
- `RESULT_CODE`
- `RESULT_TOKEN`

Success rules:
- non-commit chunk: result `QUEUED`
- commit chunk: result `APPLIED`

At the end, uploader prints:
- `active_flags`
- `active_generation`
- `active_size`

## References

- Register map: `docs/topology_upload_protocol.md`
- Topology format: `docs/topology_config_v2.md`
- Packer: `docs/topology_packer.md`
- Full CLI sequence: `docs/topology_cli_runbook.md`
