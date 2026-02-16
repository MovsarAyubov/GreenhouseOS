# Как применить патч `.ioc` под ETH + LwIP

Скрипт: `tools/apply_eth_lwip_ioc.ps1`

## Что он делает
- Добавляет IP блоки `ETH` и `LWIP` в `greenhouseOS.ioc`.
- Добавляет RMII-пины:
  - `PA1, PA2, PA7, PC1, PC4, PC5, PB11, PB12, PB13`
- Включает LwIP в static IP режиме:
  - `IP: 192.168.50.20`
  - `Mask: 255.255.255.0`
  - `Gateway: 192.168.50.1`
- Включает `Netconn`, отключает `Sockets`.
- Создает backup: `greenhouseOS.ioc.bak_eth_lwip`.

## Как выполнить
1. Закройте `greenhouseOS.ioc` в CubeIDE/CubeMX.
2. В корне проекта выполните в PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\apply_eth_lwip_ioc.ps1
```

3. Откройте `greenhouseOS.ioc` в CubeIDE.
4. Проверьте в GUI:
- `Connectivity -> ETH`: `RMII`, PHY Address = `1` (при необходимости изменить на ваш аппаратный адрес)
- `Middleware -> LwIP`: DHCP Off, static IP как выше, Netconn On, Sockets Off.
5. Нажмите `Generate Code`.

## После генерации
1. В `Project Properties -> C/C++ Build -> Settings -> MCU GCC Compiler -> Preprocessor` добавьте define:
- `GH_USE_LWIP_NETCONN`

2. Проверьте, что в проекте есть:
- `Core/Src/gh_net_adapter.c`
- `Core/Inc/gh_net_adapter.h`

3. Если `gh_net_adapter.c` не попал в build, добавьте файл в проект (Right click -> Resource Configurations -> Exclude from Build: снять).

## Важно
- На разных платах PHY address может быть `0` или `1`.
- Если сеть point-to-point без роутера, gateway можно поставить `0.0.0.0`.
- Если CubeMX подсветит конфликт пинов RMII, сначала исправьте разводку/вариант интерфейса платы.
