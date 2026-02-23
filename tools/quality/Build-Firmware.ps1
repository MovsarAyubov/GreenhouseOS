param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [string]$OutRoot = "build"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$objListPath = Join-Path $repoRoot "Debug\objects.list"
$linkerScript = Join-Path $repoRoot "STM32F407VETX_FLASH.ld"
$outDir = Join-Path $repoRoot (Join-Path $OutRoot $Configuration)

if (!(Test-Path $objListPath))
{
  throw "Missing objects list: $objListPath"
}
if (!(Test-Path $linkerScript))
{
  throw "Missing linker script: $linkerScript"
}

if (Test-Path $outDir)
{
  Remove-Item -Recurse -Force $outDir
}
New-Item -ItemType Directory -Force $outDir | Out-Null

$optFlags = @("-g3", "-DDEBUG", "-O0")
if ($Configuration -eq "Release")
{
  $optFlags = @("-g0", "-DNDEBUG", "-O2")
}

$archFlags = @("-mcpu=cortex-m4", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-mthumb")
$commonDefs = @("-DUSE_HAL_DRIVER", "-DSTM32F407xx", "-DGH_USE_LWIP_NETCONN")
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

$cCommon = @(
  "-std=gnu11",
  "-ffunction-sections",
  "-fdata-sections",
  "-Wall",
  "-fstack-usage",
  "--specs=nano.specs"
)

$objLines = Get-Content $objListPath | Where-Object { $_.Trim().Length -gt 0 }
$builtObjects = @()

foreach ($line in $objLines)
{
  $relObj = $line.Trim().Trim('"')
  if ($relObj.StartsWith("./"))
  {
    $relObj = $relObj.Substring(2)
  }
  $relObj = $relObj -replace '/', '\'
  $objOut = Join-Path $outDir $relObj
  $objParent = Split-Path -Parent $objOut
  if (!(Test-Path $objParent))
  {
    New-Item -ItemType Directory -Force $objParent | Out-Null
  }

  $srcRelC = [System.IO.Path]::ChangeExtension($relObj, ".c")
  $srcRelS = [System.IO.Path]::ChangeExtension($relObj, ".s")
  $srcRelSUpper = [System.IO.Path]::ChangeExtension($relObj, ".S")

  $srcPath = Join-Path $repoRoot $srcRelC
  $srcType = "c"
  if (!(Test-Path $srcPath))
  {
    $srcPath = Join-Path $repoRoot $srcRelS
    $srcType = "s"
  }
  if (!(Test-Path $srcPath))
  {
    $srcPath = Join-Path $repoRoot $srcRelSUpper
    $srcType = "s"
  }
  if (!(Test-Path $srcPath))
  {
    throw "Cannot resolve source for object: $relObj"
  }

  if ($srcType -eq "c")
  {
    & arm-none-eabi-gcc $srcPath @archFlags @optFlags @commonDefs @includeFlags @cCommon -c -o $objOut
  }
  else
  {
    & arm-none-eabi-gcc -x assembler-with-cpp $srcPath @archFlags @optFlags @commonDefs @includeFlags -c -o $objOut
  }

  $builtObjects += $objOut
}

$objectsRsp = Join-Path $outDir "objects.rsp"
$objectsRspLines = $builtObjects | ForEach-Object { '"' + $_ + '"' }
Set-Content -Path $objectsRsp -Value $objectsRspLines

$elfPath = Join-Path $outDir "greenhouseOS.elf"
$mapPath = Join-Path $outDir "greenhouseOS.map"

$linkArgs = @(
  "-o", $elfPath,
  "@$objectsRsp"
)
$linkArgs += $archFlags
$linkArgs += @(
  "-T$linkerScript",
  "--specs=nosys.specs",
  "-Wl,-Map=$mapPath",
  "-Wl,--gc-sections",
  "-static",
  "--specs=nano.specs",
  "-Wl,--start-group",
  "-lc",
  "-lm",
  "-Wl,--end-group"
)

& arm-none-eabi-gcc @linkArgs

& arm-none-eabi-size $elfPath
