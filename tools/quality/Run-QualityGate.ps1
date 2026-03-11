param(
  [switch]$SkipUnitTests
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "Build-Firmware.ps1") -Configuration Debug
& (Join-Path $PSScriptRoot "Build-Firmware.ps1") -Configuration Release
& (Join-Path $PSScriptRoot "Run-StaticAnalysis.ps1")

if (!$SkipUnitTests)
{
  & (Join-Path $PSScriptRoot "Run-UnitTests.ps1")
}

& (Join-Path $PSScriptRoot "Run-PythonTests.ps1")

& (Join-Path $PSScriptRoot "Check-HILScenarios.ps1")

Write-Host "Quality gate passed."
