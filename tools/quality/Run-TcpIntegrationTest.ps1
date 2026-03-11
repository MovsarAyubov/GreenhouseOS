param(
  [Parameter(Mandatory = $true)][string]$Host,
  [int]$Port = 502,
  [int]$UnitId = 1,
  [double]$TimeoutSeconds = 3,
  [int]$ExpectedMapVersion = 4,
  [int]$DirBase = 1264,
  [int]$PointsBase = 0,
  [int]$PointsQty = 72,
  [int]$DiagBase = 1376,
  [int]$DiagQty = 32,
  [switch]$RunCommand,
  [int]$CmdSlaveId = 1,
  [int]$CmdModuleId = 101,
  [int]$CmdProfileId = 5001,
  [string]$CmdPayload = "1",
  [int]$CmdTrigger = 100,
  [string]$AcceptCommandResults = "2",
  [double]$CommandTimeoutSeconds = 8,
  [double]$PollIntervalSeconds = 0.2,
  [switch]$RunBusyProbe,
  [int]$BusyTriggerA = 200,
  [int]$BusyTriggerB = 201,
  [switch]$RunInvalidTopologyProbe,
  [int]$TopoBase = 1408,
  [int]$TopoProbeToken = 300
)

$ErrorActionPreference = "Stop"

if (!(Get-Command python -ErrorAction SilentlyContinue))
{
  throw "python is required"
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$scriptPath = Join-Path $repoRoot "tools\quality\tcp_integration_probe.py"

$args = @(
  $scriptPath,
  "--host", $Host,
  "--port", $Port,
  "--unit-id", $UnitId,
  "--timeout-s", $TimeoutSeconds,
  "--expected-map-version", $ExpectedMapVersion,
  "--dir-base", $DirBase,
  "--points-base", $PointsBase,
  "--points-qty", $PointsQty,
  "--diag-base", $DiagBase,
  "--diag-qty", $DiagQty,
  "--cmd-slave-id", $CmdSlaveId,
  "--cmd-module-id", $CmdModuleId,
  "--cmd-profile-id", $CmdProfileId,
  "--cmd-payload", $CmdPayload,
  "--cmd-trigger", $CmdTrigger,
  "--accept-command-results", $AcceptCommandResults,
  "--command-timeout-s", $CommandTimeoutSeconds,
  "--poll-interval-s", $PollIntervalSeconds,
  "--busy-trigger-a", $BusyTriggerA,
  "--busy-trigger-b", $BusyTriggerB,
  "--topo-base", $TopoBase,
  "--topo-probe-token", $TopoProbeToken
)

if ($RunCommand)
{
  $args += @("--run-command")
}
if ($RunBusyProbe)
{
  $args += @("--run-busy-probe")
}
if ($RunInvalidTopologyProbe)
{
  $args += @("--run-invalid-topology-probe")
}

Push-Location $repoRoot
try
{
  & python @args
}
finally
{
  Pop-Location
}
