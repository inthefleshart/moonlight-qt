[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.9.2\msvc2022_64' }
$qmake = Join-Path $qtRoot 'bin\qmake.exe'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ tools were not found.' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
$build = Join-Path $root 'build\pen-diagnostics'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$project = Join-Path $root 'tools\pen-diagnostics\pen-diagnostics.pro'
$target = $Configuration.ToLowerInvariant()
$command = 'call "{0}" x64 && cd /d "{1}" && "{2}" "{3}" && nmake /f Makefile.{4}' -f $vcvars, $build, $qmake, $project, $Configuration
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'Pen diagnostics build failed.' }
$exe = Join-Path $build "$target\MoonlightPenDiagnostics.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "Expected diagnostic executable was not produced at $exe" }
Write-Output $exe
