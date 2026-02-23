# How to apply ETH + LwIP IOC patch

Script: `tools/apply_eth_lwip_ioc.ps1`

## What it does
- Adds `ETH` and `LWIP` IP blocks into `greenhouseOS.ioc`.
- Adds RMII pins:
  - `PA1, PA2, PA7, PC1, PC4, PC5, PB11, PB12, PB13`
- Forces LwIP static IP:
  - `IP: 192.168.50.20`
  - `Mask: 255.255.255.0`
  - `Gateway: 192.168.50.1`
- Enables `Netconn`, disables `Sockets`.
- Creates backup: `greenhouseOS.ioc.bak_eth_lwip`.

## How to run
1. Close `greenhouseOS.ioc` in CubeIDE/CubeMX.
2. Run in project root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\apply_eth_lwip_ioc.ps1
```

3. Re-open `greenhouseOS.ioc` in CubeIDE.
4. Verify in GUI:
- `Connectivity -> ETH`: `RMII`, PHY Address = `1` (or your board value)
- `Middleware -> LwIP`: DHCP Off, static IP as above, Netconn On, Sockets Off
5. Click `Generate Code`.

## After generation
1. Add define in `Project Properties -> C/C++ Build -> Settings -> MCU GCC Compiler -> Preprocessor`:
- `GH_USE_LWIP_NETCONN`

2. Verify file is included in build:
- `Core/Src/gh_modbus_tcp_server.c`

## Important
- Different boards may use PHY address `0` or `1`.
- For point-to-point link without router, gateway can be `0.0.0.0`.
- If CubeMX reports RMII pin conflicts, resolve board pinmux first.
- Current firmware baseline uses `Modbus TCP` on port `502`.
