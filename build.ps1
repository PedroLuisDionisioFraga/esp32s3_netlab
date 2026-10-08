# Build web UI via Docker; output goes to front/web/dist
# Usage: build.ps1 [-Clean]   (-Clean: delete dist and rebuild the image without cache)
param([switch]$Clean)
$ErrorActionPreference = 'Stop'
Set-Location "$PSScriptRoot/front/web"

docker info *> $null
if ($LASTEXITCODE) {
    Write-Host 'Starting Docker Desktop...'
    Start-Process "$env:ProgramFiles\Docker\Docker\Docker Desktop.exe"
    do { Start-Sleep 2; docker info *> $null } while ($LASTEXITCODE)
}

if ($Clean) {
    Remove-Item dist -Recurse -Force -ErrorAction SilentlyContinue
    docker build --no-cache -t netlab-web .
} else {
    docker build -t netlab-web .
}
if (-not $LASTEXITCODE) { docker run --rm -v "${PWD}/dist:/app/dist" netlab-web }
$code = $LASTEXITCODE
Set-Location $PSScriptRoot
exit $code
