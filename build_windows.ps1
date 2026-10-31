# Builds th06.exe and th06_config.exe with Visual Studio, then puts the SDL2
# DLLs and msgothic.ttc next to them.
#
#   .\build_windows.ps1 -ToolsDir C:\path\to\tools
#
# ToolsDir holds premake5.exe and the unzipped SDL2, SDL2_image and SDL2_ttf
# "devel ... VC" packages (folders named SDL2-2.x, SDL2_image-2.x and
# SDL2_ttf-2.x). Instead of -ToolsDir you can set TH06_TOOLS_DIR, or set
# SDL2_DIR, SDL2_IMAGE_DIR and SDL2_TTF_DIR and put premake5 on PATH.
#
# Needs Visual Studio 2022 or 2026 with "Desktop development with C++".

param(
    [string]$ToolsDir = $env:TH06_TOOLS_DIR,
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot

function Find-SdlPackage([string]$EnvName, [string]$Pattern) {
    $dir = [Environment]::GetEnvironmentVariable($EnvName)
    if (-not $dir -and $ToolsDir) {
        $found = Get-ChildItem $ToolsDir -Directory -Filter $Pattern -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($found) { $dir = $found.FullName }
    }
    if (-not $dir -or -not (Test-Path (Join-Path $dir "lib\x64"))) {
        throw "Can't find the ${Pattern} package. Pass -ToolsDir or set ${EnvName}."
    }
    # premake reads these.
    [Environment]::SetEnvironmentVariable($EnvName, $dir)
    return $dir
}

$Sdl2 = Find-SdlPackage "SDL2_DIR" "SDL2-*"
$Sdl2Image = Find-SdlPackage "SDL2_IMAGE_DIR" "SDL2_image-*"
$Sdl2Ttf = Find-SdlPackage "SDL2_TTF_DIR" "SDL2_ttf-*"

$Premake = Get-Command premake5 -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $Premake -and $ToolsDir -and (Test-Path (Join-Path $ToolsDir "premake5.exe"))) {
    $Premake = Join-Path $ToolsDir "premake5.exe"
}
if (-not $Premake) {
    throw "Can't find premake5.exe. Put it in -ToolsDir or on PATH (https://premake.github.io/download)."
}

$VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $VsWhere)) {
    throw "Can't find Visual Studio. Install it with 'Desktop development with C++'."
}
$VsVersion = & $VsWhere -latest -prerelease -requires Microsoft.Component.MSBuild -property installationVersion
$MsBuild = & $VsWhere -latest -prerelease -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\amd64\MSBuild.exe" |
    Select-Object -First 1
if (-not $MsBuild) {
    throw "Can't find MSBuild. Install Visual Studio with 'Desktop development with C++'."
}

Push-Location $Root
try {
    # premake's vs2022 action asks for the VS 2022 toolset (v143); VS 2026 ships v145.
    & $Premake vs2022
    if ($LASTEXITCODE -ne 0) { throw "premake5 failed with exit code $LASTEXITCODE" }

    $MsBuildArgs = @("build\th06.sln", "/p:Configuration=$Configuration", "/p:Platform=x64", "/m", "/nologo", "/v:minimal")
    if ([int]($VsVersion.Split(".")[0]) -ge 18) {
        $MsBuildArgs += "/p:PlatformToolset=v145"
    }
    & $MsBuild @MsBuildArgs
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }

    Copy-Item (Join-Path $Sdl2 "lib\x64\SDL2.dll"), (Join-Path $Sdl2Image "lib\x64\SDL2_image.dll"),
        (Join-Path $Sdl2Ttf "lib\x64\SDL2_ttf.dll") $Root
    $Font = Join-Path $env:WINDIR "Fonts\msgothic.ttc"
    if (-not (Test-Path (Join-Path $Root "msgothic.ttc")) -and (Test-Path $Font)) {
        Copy-Item $Font $Root
    }
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "Built th06.exe and th06_config.exe in $Root"
if (-not (Get-ChildItem $Root -Filter "*.DAT" -ErrorAction SilentlyContinue)) {
    Write-Host "Copy the six .DAT files from your copy of the game into this folder, run th06_config.exe"
    Write-Host "once to pick Windowed if you don't want full screen, then start th06.exe."
}
