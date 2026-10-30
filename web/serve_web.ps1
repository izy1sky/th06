# Serves the web build in dist\ over HTTP (browsers block file:// loads).
# Usage: .\web\serve_web.ps1 [port]   (default 8000)
param(
    [int]$Port = 8000
)

$Dist = Join-Path (Split-Path -Parent $PSScriptRoot) "dist"
if (-not (Test-Path (Join-Path $Dist "index.html"))) {
    Write-Error "$Dist\index.html not found. Build first: .\web\build_web.ps1"
}

# The Windows Python launcher is "py"; plain "python" also works when on PATH.
$Python = if (Get-Command py -ErrorAction SilentlyContinue) { "py" } else { "python" }
Write-Host "Open http://localhost:$Port/"
Set-Location $Dist
& $Python -m http.server $Port
