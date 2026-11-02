# Builds th06.exe and th06_config.exe with Visual Studio.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Game C:\games\th06
#
# -Game    your game folder (the one with the DAT files); th06.exe, th06_config.exe and the SDL
#          DLLs are copied there. Without it they stay in the repository folder.
# -Config  Release (default) or Debug
#
# Needs Visual Studio 2022 or 2026 with "Desktop development with C++". premake and the SDL2
# libraries are downloaded into deps\ the first time.
param(
    [string]$Game,
    [ValidateSet("Release", "Debug")][string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue" # the progress bar makes Invoke-WebRequest very slow
$Root = Split-Path -Parent $PSScriptRoot
$Deps = Join-Path $Root "deps"

function Fail($Message) { Write-Host "error: $Message" -ForegroundColor Red; exit 1 }

if ($Game -and -not (Test-Path $Game -PathType Container)) { Fail "$Game is not a folder" }

function Get-Zip($Url, $Dest) {
    $zip = "$Dest.zip"
    New-Item -ItemType Directory -Force (Split-Path -Parent $zip) | Out-Null
    Write-Host "  downloading $Url"
    Invoke-WebRequest -UseBasicParsing $Url -OutFile $zip
    Expand-Archive -Force $zip $Dest
    Remove-Item $zip
}

# premake writes the Visual Studio solution.
$Premake = Join-Path $Deps "premake\premake5.exe"
if (-not (Test-Path $Premake)) {
    Get-Zip "https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-windows.zip" (Join-Path $Deps "premake")
}

# The code includes <SDL2/SDL.h>, but the SDL release zips keep their headers directly in include\.
# Put the headers of all three libraries in deps\sdl\include\SDL2\ and their x64 .lib and .dll files
# in deps\sdl\lib\x64\, then point premake's SDL2_DIR, SDL2_IMAGE_DIR and SDL2_TTF_DIR there.
$Sdl = Join-Path $Deps "sdl"
if (-not (Test-Path (Join-Path $Sdl "lib\x64\SDL2_ttf.dll"))) {
    $Packages = @(
        "https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-VC.zip",
        "https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.8/SDL2_image-devel-2.8.8-VC.zip",
        "https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-devel-2.24.0-VC.zip"
    )
    if (Test-Path $Sdl) { Remove-Item -Recurse -Force $Sdl }
    New-Item -ItemType Directory -Force (Join-Path $Sdl "include\SDL2"), (Join-Path $Sdl "lib\x64") | Out-Null
    $Tmp = Join-Path $Deps "download"
    foreach ($url in $Packages) {
        if (Test-Path $Tmp) { Remove-Item -Recurse -Force $Tmp }
        Get-Zip $url $Tmp
        $pkg = Get-ChildItem -Directory $Tmp | Select-Object -First 1
        Copy-Item (Join-Path $pkg.FullName "include\*.h") (Join-Path $Sdl "include\SDL2")
        Copy-Item (Join-Path $pkg.FullName "lib\x64\*") (Join-Path $Sdl "lib\x64") -Include *.lib, *.dll
    }
    Remove-Item -Recurse -Force $Tmp
}
$env:SDL2_DIR = $Sdl
$env:SDL2_IMAGE_DIR = $Sdl
$env:SDL2_TTF_DIR = $Sdl

Write-Host "Generating the Visual Studio solution"
Push-Location $Root
try { & $Premake vs2022 } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { Fail "premake failed" }

$Vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $Vswhere)) { Fail "Visual Studio not found. Install it with 'Desktop development with C++'" }
$MSBuild = & $Vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $MSBuild) { Fail "MSBuild not found. Add 'Desktop development with C++' in the Visual Studio Installer" }

# premake's vs2022 projects ask for the v143 build tools, which Visual Studio 2026 doesn't install
# by default, so build with the toolset of the Visual Studio that is installed.
$VsMajor = [int](& $Vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationVersion).Split(".")[0]
$Toolset = switch ($VsMajor) { 17 { "v143" } 18 { "v145" } default { Fail "Visual Studio $VsMajor is not supported; use 2022 or 2026" } }

Write-Host "Building ($Config, toolset $Toolset)"
& $MSBuild (Join-Path $Root "build\th06.sln") "/p:Configuration=$Config" "/p:Platform=x64" "/p:PlatformToolset=$Toolset" /m /nologo /v:minimal "/clp:ErrorsOnly"
if ($LASTEXITCODE -ne 0) { Fail "the build failed" }

# th06.exe and th06_config.exe are built into the repository folder; they need the SDL DLLs next to them.
$Built = @("th06.exe", "th06_config.exe") | ForEach-Object { Join-Path $Root $_ }
$Dlls = Get-ChildItem (Join-Path $Sdl "lib\x64\*.dll") | ForEach-Object { $_.FullName }
Copy-Item $Dlls $Root
$Dest = $Root
if ($Game) {
    $Dest = (Resolve-Path $Game).Path
    # th06_config.exe draws its window with this font.
    Copy-Item ($Built + $Dlls + (Join-Path $Root "NotoSans-Regular.ttf")) $Dest
}

Write-Host ""
Write-Host "Done: th06.exe and th06_config.exe are in $Dest" -ForegroundColor Green
if (-not $Game) { Write-Host "Copy them and the SDL*.dll files into your game folder, or run again with -Game <game folder>." }
