param()

$ErrorActionPreference = "Stop"

if (!(Get-Command python -ErrorAction SilentlyContinue))
{
  throw "python is required"
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

Push-Location $repoRoot
try
{
  & python -m unittest discover -s tools/topology/tests -p "test_*.py"
  if ($LASTEXITCODE -ne 0)
  {
    throw "topology Python tests failed with exit code $LASTEXITCODE"
  }

  & python -m unittest discover -s tools/topology_designer/tests -p "test_*.py"
  if ($LASTEXITCODE -ne 0)
  {
    throw "topology_designer Python tests failed with exit code $LASTEXITCODE"
  }

  & python -m unittest discover -s tools/quality/tests -p "test_*.py"
  if ($LASTEXITCODE -ne 0)
  {
    throw "quality Python tests failed with exit code $LASTEXITCODE"
  }
}
finally
{
  Pop-Location
}
