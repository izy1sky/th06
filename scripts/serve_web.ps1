# Serves dist\ at http://localhost:8000/ (browsers won't load the game from a file:// page).
#
#   powershell -ExecutionPolicy Bypass -File scripts\serve_web.ps1 [-Port 8000]
param(
    [int]$Port = 8000
)

$ErrorActionPreference = "Stop"
$Dist = Join-Path (Split-Path -Parent $PSScriptRoot) "dist"
if (-not (Test-Path (Join-Path $Dist "index.html"))) {
    Write-Host "error: $Dist\index.html not found. Build first with scripts\build_web.ps1" -ForegroundColor Red
    exit 1
}

# "py" is the Python launcher that python.org installs; "python" works when it is on PATH.
$Python = if (Get-Command py -ErrorAction SilentlyContinue) { "py" } else { "python" }
Write-Host "Open http://localhost:$Port/  (Ctrl+C stops the server)"
& $Python -m http.server $Port --directory $Dist
