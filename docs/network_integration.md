# Ethernet/LwIP Integration

Current firmware has a transport adapter API in:
- `Core/Inc/gh_net_adapter.h`
- `Core/Src/gh_net_adapter.c`

## What is implemented
- Default build: safe stub (no client connected).
- Optional real TCP server via LwIP `netconn` API with single-client policy (new client replaces old).

## To enable real TCP server on STM32F407
1. In CubeMX (`greenhouseOS.ioc`) enable:
- `ETH` peripheral + RMII/MII pins for your board/PHY
- `LwIP` middleware with `Netconn API`
- Keep FreeRTOS enabled

2. Regenerate code.

3. Add compile define:
- `GH_USE_LWIP_NETCONN`

4. Ensure include paths contain LwIP headers (`lwip/api.h`, `lwip/err.h`).

5. Confirm `NetAdapter_Init(5000)` is called (already done in `main.c`).

## Behavior
- TCP server listens on port `5000`.
- New connect drops old active client.
- `Recv` supports partial reads through internal `netbuf` offset tracking.
- Any send/recv fatal error closes client and forces reconnect flow.
