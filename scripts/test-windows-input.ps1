[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.9.2\msvc2022_64' }
$env:PATH = (Join-Path $qtRoot 'bin') + ';' + $env:PATH
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ tools were not found.' }

$build = Join-Path $root 'build\tests-windows-input'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$qmake = Join-Path $qtRoot 'bin\qmake.exe'
$jom = Join-Path $root 'scripts\jom.exe'
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
$project = Join-Path $root 'tests\windows-input\windows-input.pro'
$testExe = Join-Path $build 'release\windows-input-tests.exe'

$command = 'call "{0}" x64 && cd /d "{1}" && "{2}" "{3}" && "{4}" release' -f $vcvars, $build, $qmake, $project, $jom
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw 'Windows input test build failed.' }
& $testExe -txt
if ($LASTEXITCODE -ne 0) { throw 'Windows input tests failed.' }
