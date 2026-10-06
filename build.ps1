# Build web UI via Docker; output goes to front/web/dist
$ErrorActionPreference = 'Stop'
Set-Location "$PSScriptRoot/front/web"

docker info *> $null
if ($LASTEXITCODE) {
    Write-Host 'Starting Docker Desktop...'
    Start-Process "$env:ProgramFiles\Docker\Docker\Docker Desktop.exe"
    do { Start-Sleep 2; docker info *> $null } while ($LASTEXITCODE)
}

docker build -t netlab-web .
if (-not $LASTEXITCODE) { docker run --rm -v "${PWD}/dist:/app/dist" netlab-web }
$code = $LASTEXITCODE
Set-Location $PSScriptRoot
exit $code
