param(
  [switch]$WarningsAsErrors = $true
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

$files = @(
  "Core/Src/gh_config_storage.c",
  "Core/Src/gh_crc32.c",
  "Core/Src/gh_modbus_io.c",
  "Core/Src/gh_modbus_map.c",
  "Core/Src/gh_modbus_master.c",
  "Core/Src/gh_modbus_tcp_server.c",
  "Core/Src/main.c"
)

$archFlags = @("-mcpu=cortex-m4", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-mthumb")
$baseFlags = @(
  "-std=gnu11",
  "-DDEBUG",
  "-DUSE_HAL_DRIVER",
  "-DSTM32F407xx",
  "-DGH_USE_LWIP_NETCONN"
)
$includeFlags = @(
  "-I$repoRoot/Core/Inc",
  "-I$repoRoot/Drivers/STM32F4xx_HAL_Driver/Inc",
  "-I$repoRoot/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy",
  "-I$repoRoot/Drivers/CMSIS/Device/ST/STM32F4xx/Include",
  "-I$repoRoot/Drivers/CMSIS/Include",
  "-I$repoRoot/Middlewares/Third_Party/FreeRTOS/Source/include",
  "-I$repoRoot/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2",
  "-I$repoRoot/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F",
  "-I$repoRoot/LWIP/App",
  "-I$repoRoot/LWIP/Target",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/system",
  "-I$repoRoot/Drivers/BSP/Components/lan8742",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/netif/ppp",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/lwip",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/lwip/apps",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/lwip/priv",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/lwip/prot",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/netif",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/compat/posix",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/compat/posix/arpa",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/compat/posix/net",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/compat/posix/sys",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/src/include/compat/stdc",
  "-I$repoRoot/Middlewares/Third_Party/LwIP/system/arch"
)
$warnFlags = @(
  "-Wall",
  "-Wextra",
  "-Wshadow",
  "-Wdouble-promotion",
  "-Wformat=2",
  "-Wundef",
  "-Wno-unused-parameter"
)
if ($WarningsAsErrors)
{
  $warnFlags += "-Werror"
}

foreach ($file in $files)
{
  $srcPath = Join-Path $repoRoot $file
  Write-Host "Static analysis: $file"
  & arm-none-eabi-gcc $srcPath @archFlags @baseFlags @includeFlags @warnFlags -fsyntax-only --specs=nano.specs
}
