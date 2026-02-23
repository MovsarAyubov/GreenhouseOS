$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$hilDoc = Join-Path $repoRoot "docs\hil_scenarios.md"

if (!(Test-Path $hilDoc))
{
  throw "Missing HIL scenarios doc: $hilDoc"
}

$requiredScenarios = @(
  "loss_of_network",
  "modbus_timeouts",
  "power_cycle_during_config_write",
  "watchdog_recovery",
  "config_crc_reject"
)

$content = Get-Content $hilDoc -Raw
foreach ($scenario in $requiredScenarios)
{
  if ($content -notmatch [regex]::Escape($scenario))
  {
    throw "HIL scenario not found: $scenario"
  }
}

Write-Host "HIL scenarios checklist OK."
