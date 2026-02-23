# Master Protocol Baseline (STM32F407)

Date: `2026-02-23`.

## Transport (implemented)
- `Modbus TCP` server on port `502`.
- Implementation: `Core/Src/gh_modbus_tcp_server.c` + `Core/Src/Modbus.c`.
- Build requires `GH_USE_LWIP_NETCONN`.

## Modbus map
- Holding register backend: `Core/Src/gh_modbus_map.c`.
- Map size: `GH_MB_TOTAL_REGS = GH_MB_MAX_SLAVES * GH_MB_BLOCK_SIZE = 20 * 64`.
- Addressing supports both zero-based offsets and `41000`-style SCADA offsets (normalized in `Core/Src/Modbus.c`).

## Supported Modbus TCP operations
- Holding read (`FC=3`).
- Single holding write (`FC=6`).
- Multiple holding write (`FC=16`).

## Master to slave RS485/RTU cycle
- Master polls slave IDs `1..20`.
- One full poll cycle runs every `5 seconds` (waits for remaining cycle budget).

## Important
- Custom framed TCP protocol (`HELLO/STATUS/SNAPSHOT/EVENT`) is not implemented in current firmware.
- This file describes current firmware behavior, not a future target protocol.
