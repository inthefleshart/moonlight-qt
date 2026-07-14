[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}

$configName = $Configuration.ToLowerInvariant()
$source = Get-ChildItem (Join-Path $root "build\installer-x64-$configName\MoonlightPortable-*.zip") |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $source) { throw 'Portable package was not produced.' }

$artifactDir = Join-Path $root 'artifacts'
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
$destination = Join-Path $artifactDir ($source.BaseName.Replace('MoonlightPortable', 'MoonlightWacomPortable') + $source.Extension)
Copy-Item -LiteralPath $source.FullName -Destination $destination -Force
Get-FileHash -Algorithm SHA256 -LiteralPath $destination | Format-List

$diagnosticExe = & (Join-Path $PSScriptRoot 'build-pen-diagnostics.ps1') -Configuration Release |
    Select-Object -Last 1
if (-not (Test-Path -LiteralPath $diagnosticExe)) { throw 'Pen diagnostic executable was not produced.' }
$diagnosticDestination = Join-Path $artifactDir 'MoonlightPenDiagnostics.exe'
Copy-Item -LiteralPath $diagnosticExe -Destination $diagnosticDestination -Force
Get-FileHash -Algorithm SHA256 -LiteralPath $diagnosticDestination | Format-List
