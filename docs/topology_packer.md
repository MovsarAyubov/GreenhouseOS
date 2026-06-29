# Topology Packer Host Tool

Date: `2026-02-25`.

## Purpose

`tools/topology/topology_packer.py` converts JSON topology into:

- binary `topology_config v2` payload (`.bin`)
- pre-split Modbus upload chunk script (`.json`)

This provides deterministic topology artifacts for offline tooling and
compatibility workflows. The Ubuntu hub reads topology JSON directly.

## Input JSON schema (baseline)

Root fields:
- `ver_minor` (u16, optional, default `0`)
- `generation` (u32, required)
- `topology_id` (u32, required)
- `created_unix_s` (u32, required)
- `flags` (u32, optional, default `0`)
- `modules[]`
- `requests[]`
- `points[]`
- `commands[]`
- `policies[]`

Field names inside each table match the JSON schema described in `docs/topology_config_v2.md`.

## Run

```powershell
python tools/topology/topology_packer.py `
  --input .\topology.json `
  --output-bin .\build\topology\topology_v2.bin `
  --output-chunks .\build\topology\topology_chunks.json `
  --start-token 100 `
  --chunk-words 120
```

## Output chunk format

Each chunk entry contains:
- `submit_token`
- `chunk_index`
- `chunk_words`
- `flags` (`RESET`/`COMMIT`)
- `total_size`
- `chunk_crc32`
- `generation`
- `chunk_data_words[]`

## Unit tests

Tests are in `tools/topology/tests/test_topology_packer.py`.

Run:

```powershell
python -m unittest discover -s tools/topology/tests -p "test_*.py"
```
