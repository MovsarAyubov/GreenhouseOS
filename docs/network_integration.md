# Ethernet/LwIP Integration (Current Firmware)

Date: `2026-02-23`.

## Implemented
- Ethernet + LwIP (`netconn`) + FreeRTOS.
- `Modbus TCP` server on port `502`.
- Server initialization is in `Core/Src/gh_modbus_tcp_server.c`.

## Build requirements
1. `greenhouseOS.ioc` must enable:
- `ETH` (RMII),
- `LwIP`,
- FreeRTOS.

2. Preprocessor defines must include:
- `GH_USE_LWIP_NETCONN`.

3. Build must include:
- `Core/Src/gh_modbus_tcp_server.c`.

## Runtime behavior
- TCP task waits for `gnetif` readiness, then starts `ModbusInit/ModbusStart`.
- Listening port is fixed to `502`.
- Holding map buffer is provided by `GH_ModbusMap_GetBackingStore()`.

## Important
- `gh_net_adapter.h/.c` does not exist in the current project.
- Any docs referencing port `5000` and `NetAdapter` describe an older or planned variant, not current firmware.
