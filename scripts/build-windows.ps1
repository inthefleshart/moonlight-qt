[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateSet('x64')]
    [string]$Architecture = 'x64',
    [switch]$Clean,
    [switch]$BuildInstaller,
    [switch]$EnableLocalWintab
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.9.2\msvc2022_64' }
$qmake = Join-Path $qtRoot 'bin\qmake.exe'
if (-not (Test-Path -LiteralPath $qmake)) { throw "Qt qmake not found at $qmake. Set QT_ROOT if Qt is elsewhere." }

$sevenZip = 'C:\Program Files\7-Zip'
if (-not (Test-Path -LiteralPath (Join-Path $sevenZip '7z.exe'))) { throw '7-Zip was not found.' }

if ($EnableLocalWintab) {
    if (-not $env:WACOM_SDK_DIR -or -not (Test-Path -LiteralPath $env:WACOM_SDK_DIR)) {
        throw 'Set WACOM_SDK_DIR to a locally licensed Wacom SDK checkout before using -EnableLocalWintab.'
    }
}

$buildRoot = $root
if ($root.Contains(' ')) {
    $junction = Join-Path $env:TEMP 'moonlight-wacom-src'
    if (Test-Path -LiteralPath $junction) {
        $item = Get-Item -LiteralPath $junction -Force
        $junctionTarget = if ($item.Target) { [IO.Path]::GetFullPath([string]$item.Target) } else { $null }
        if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            $junctionTarget -ne [IO.Path]::GetFullPath($root)) {
            throw "The build junction $junction exists but does not point to this repository. Remove it manually after verifying its target."
        }
    } else {
        New-Item -ItemType Junction -Path $junction -Target $root | Out-Null
    }
    $buildRoot = $junction
}

$env:PATH = (Join-Path $qtRoot 'bin') + ';' + $sevenZip + ';' + $env:PATH
# Some managed shells expose an inherited PATH that jom's worker processes do
# not preserve. nmake is serial but reliable and remains inside vcvarsall.
$env:MOONLIGHT_USE_NMAKE = '1'
if ($Clean) { $env:MOONLIGHT_CLEAN = '1' } else { Remove-Item Env:MOONLIGHT_CLEAN -ErrorAction SilentlyContinue }
if ($BuildInstaller) { Remove-Item Env:MOONLIGHT_SKIP_MSI -ErrorAction SilentlyContinue } else { $env:MOONLIGHT_SKIP_MSI = '1' }
Push-Location $buildRoot
try {
    & cmd.exe /d /c scripts\build-arch.bat $Configuration.ToLowerInvariant()
    if ($LASTEXITCODE -ne 0) { throw "Moonlight $Configuration build failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
}
