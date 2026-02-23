$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $repoRoot "build\unit"
$isWindows = ($env:OS -eq "Windows_NT")
$exeName = "unit_tests"
if ($isWindows)
{
  $exeName = "unit_tests.exe"
}
$exePath = Join-Path $buildDir $exeName

if (!(Get-Command gcc -ErrorAction SilentlyContinue))
{
  throw "gcc is required for host unit tests"
}

if (Test-Path $buildDir)
{
  Remove-Item -Recurse -Force $buildDir
}
New-Item -ItemType Directory -Force $buildDir | Out-Null

$sources = @(
  "$repoRoot/tests/unit/stubs/os_stubs.c",
  "$repoRoot/tests/unit/stubs/runtime_stubs.c",
  "$repoRoot/Core/Src/gh_crc32.c",
  "$repoRoot/Core/Src/gh_modbus_map.c",
  "$repoRoot/Core/Src/gh_config_storage.c",
  "$repoRoot/tests/unit/test_crc32.c",
  "$repoRoot/tests/unit/test_modbus_map.c",
  "$repoRoot/tests/unit/test_config_storage.c",
  "$repoRoot/tests/unit/test_main.c"
)

$includeFlags = @(
  "-I$repoRoot/tests/unit/shim",
  "-I$repoRoot/tests/unit/include",
  "-I$repoRoot/tests/unit",
  "-I$repoRoot/Core/Inc"
)

& gcc @sources @includeFlags -include "$repoRoot/tests/unit/shim/main.h" -std=c11 -Wall -Wextra -Werror -DGH_USE_LWIP_NETCONN -o $exePath -lm
& $exePath
