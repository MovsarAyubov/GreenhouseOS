param(
  [Parameter(Mandatory = $true)][string]$Host,
  [int]$Port = 502,
  [int]$UnitId = 1,
  [double]$DurationHours = 24,
  [double]$PollSeconds = 5,
  [double]$TimeoutSeconds = 4,
  [int]$ReconnectEveryCycles = 60,
  [double]$ReconnectProbability = 0.0,
  [double]$ReconnectDowntimeSeconds = 0.2,
  [double]$UploadIntervalSeconds = 900,
  [string]$Chunks
)

$ErrorActionPreference = "Stop"

if (!(Get-Command python -ErrorAction SilentlyContinue))
{
  throw "python is required"
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$scriptPath = Join-Path $repoRoot "tools\quality\tcp_soak_test.py"

$args = @(
  $scriptPath,
  "--host", $Host,
  "--port", $Port,
  "--unit-id", $UnitId,
  "--duration-h", $DurationHours,
  "--poll-s", $PollSeconds,
  "--timeout-s", $TimeoutSeconds,
  "--reconnect-every-cycles", $ReconnectEveryCycles,
  "--reconnect-probability", $ReconnectProbability,
  "--reconnect-downtime-s", $ReconnectDowntimeSeconds,
  "--upload-interval-s", $UploadIntervalSeconds
)

if ($Chunks)
{
  $args += @("--chunks", (Resolve-Path $Chunks))
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
