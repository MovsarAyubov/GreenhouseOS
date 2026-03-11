# Topology Designer Runbook

Date: `2026-03-08`.

## Purpose

Fast workflow for generating `topology_config v2` from `greenhouse_profile_v1`.

## 1. Validate profile

```powershell
python tools/topology_designer/cli.py validate `
  --input .\build\topology\greenhouse_profile.json
```

## 2. Compile topology JSON

```powershell
python tools/topology_designer/cli.py compile `
  --input .\build\topology\greenhouse_profile.json `
  --output .\build\topology\generated_topology.json
```

## 3. Compile + pack in one command

```powershell
python tools/topology_designer/cli.py pack `
  --input .\build\topology\greenhouse_profile.json `
  --output-topology .\build\topology\generated_topology.json `
  --output-bin .\build\topology\generated_topology.bin `
  --output-chunks .\build\topology\generated_topology_chunks.json `
  --start-token 100 `
  --chunk-words 120
```

## 4. Upload chunks

```powershell
python tools/topology/topology_uploader.py `
  --host 192.168.50.20 `
  --port 502 `
  --unit-id 1 `
  --chunks .\build\topology\generated_topology_chunks.json
```

## Notes

- Profile format reference: `docs/greenhouse_profile_v1.md`
- Topology format reference: `docs/topology_config_v2.md`
- Upload protocol reference: `docs/topology_upload_protocol.md`

