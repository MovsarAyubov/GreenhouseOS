# greenhouseOS Project Documentation

Date: `2026-03-08`

This document is a consolidated project overview built from:
- current repository docs in `docs/`
- current firmware sources in `Core/Inc` and `Core/Src`

Use this file as the entry point. Use linked docs for full protocol details.

## 1. Project Goal

`greenhouseOS` is STM32F407 firmware for a greenhouse master controller.

Primary goals:
- poll and control RS485 Modbus RTU slave blocks
- expose unified telemetry and control over Modbus TCP (port `502`) to SCADA/operator software
- move from hardcoded register logic to data-driven topology (`topology v2`) uploaded at runtime
- keep deterministic behavior and recovery on MCU level (watchdog, bounded retries, A/B flash fallback)

## 2. System Context

Implemented system shape:
- MCU: `STM32F407VETx`
- RTOS: `FreeRTOS` (CMSIS-RTOS2 API)
- Network stack: `LwIP netconn`
- Field bus: `Modbus RTU master` over RS485 (UART2)
- Northbound interface: `Modbus TCP server` on Ethernet

Current runtime is not based on custom `HELLO/STATUS/SNAPSHOT` TCP frames. Active protocol is Modbus TCP map access (`FC=3/6/16`).

## 3. Runtime Architecture

Main runtime tasks created in `main.c`:
- `StartControlTask`: control loop + config apply queue consumer
- `StartModbusMasterTask`: deterministic Modbus polling and command dispatch
- `StartModbusTcpServerTask`: LwIP-based Modbus TCP server
- `StartConfigStorageTask`: config/topology validation and flash persistence
- `StartHealthWatchdogTask`: heartbeat supervision and IWDG refresh policy

Inter-task channels:
- `qConfigApplyHandle`
- `qConfigStoreHandle`
- `qTopologyStoreHandle`

Safety model:
- periodic task heartbeats
- watchdog miss streak detection
- optional IWDG refresh suppression on persistent miss (forces reset)
- diagnostic counters and last-event codes exposed through Modbus diagnostics window

## 4. Data Model and Limits

System limits from runtime headers:
- `SENSOR_COUNT = 180`
- `MODBUS_MAX_SLAVES = 20`
- `MODBUS_POLL_PERIOD_MS = 5000`
- `configTOTAL_HEAP_SIZE = 49152` bytes

Holding map windows (`GH_ModbusMap`):
- points: `0..1079` (`1080` regs)
- slave status: `1080..1239` (`160` regs)
- command ingress: `1240..1263` (`24` regs)
- directory: `1264..1295` (`32` regs)
- config pipeline: `1296..1375` (`80` regs)
- diagnostics: `1376..1407` (`32` regs)
- topology upload pipeline: `1408..1551` (`144` regs)
- total map size: `1552` registers

Addressing supports both:
- zero-based offsets (`0..1887`)
- `41000`-style offsets (normalized in protocol stack)

## 5. Control and Data Flow

High-level flow:
1. Boot initializes hardware, loads config and active topology, initializes map, starts RTOS tasks.
2. Modbus master task runs each cycle (target `5 s`), updates ages, polls slaves, updates map.
3. TCP server exposes map for reads and command/config/topology writes.
4. Config storage task consumes submit tokens from map, validates payloads, writes flash, reports result tokens/codes.
5. Watchdog task supervises health telemetry and reset policy.

Topology mode contract:
- runtime execution path is topology-driven (`poll/points/commands/policies`)
- if topology runtime is invalid/empty, firmware enters deterministic safe mode (`STALE/OFFLINE` quality) and rejects command execution by topology contract result codes

## 6. Configuration and Topology Persistence

Config pipeline:
- client writes request fields into `GH_MB_CFG_BASE` window
- `SUBMIT_TOKEN` change enqueues request
- storage task validates version/CRC/ranges
- accepted config is written to flash with retry and A/B fallback
- result code/token and active version are published back to map

Topology v2 chunked pipeline:
- client sends chunks through `GH_MB_TOPO_BASE` request fields
- supports reset staging and commit flags
- each chunk is CRC-checked, bounds-checked, assembled in staging buffer
- on commit, full payload schema/semantic/budget validation is applied
- successful activation writes topology to flash A/B slots and updates runtime bindings atomically

A/B behavior:
- both config and topology payload are persisted with slot fallback
- active payload is loaded from the newest valid slot on boot

## 7. Tooling and Operations

Host tooling included in repository:
- `tools/topology/topology_packer.py`: build binary topology blob and chunk requests
- `tools/topology/topology_uploader.py`: upload chunks via Modbus TCP and poll results

Quality tooling:
- `tools/quality/Run-QualityGate.ps1`
- `tools/quality/Run-UnitTests.ps1`
- `tools/quality/Run-PythonTests.ps1`
- `tools/quality/Run-StaticAnalysis.ps1`
- `tools/quality/Build-Firmware.ps1`
- `tools/quality/Run-TcpSoakTest.ps1`
- `tools/quality/Run-TcpIntegrationTest.ps1`

CI:
- `.github/workflows/quality-gate.yml` runs the PowerShell quality gate in GitHub Actions

## 8. Current Known Gaps and Alignment Items

Important implementation notes observed in session data and current code:
- `publish_event(...)` currently stores event code/counters; severity/source/value are ignored in runtime storage path.
- Master UART2 is configured for `19200`, while some slave specification text still references `9600`; docs and firmware settings should be aligned.
- Topology schema includes `BUS_RTU2`, but current validator/runtime accept RTU1 and TCP only.
- Large runtime topology caches are placed in `.ccmram`; startup/linker assumptions for CCM initialization must stay consistent.

## 9. Source of Truth Documents

Detailed references:
- `docs/architecture_baseline.md`
- `docs/master_protocol.md`
- `docs/topology_config_v2.md`
- `docs/topology_upload_protocol.md`
- `docs/network_integration.md`
- `docs/quality_gate.md`
- `docs/soak_report_template.md`
- `docs/error_codes.md`
- `docs/event_codes.md`

If any high-level statement in this file conflicts with runtime behavior, the following take precedence:
1. firmware code in `Core/Src` and `Core/Inc`
2. `docs/architecture_baseline.md`
