[CmdletBinding()]
param(
    [switch]$Check,
    [switch]$InstallMissing
)

$ErrorActionPreference = 'Stop'
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.9.2\msvc2022_64' }
$winget = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\winget.exe'
$wingetAvailable = $null -ne (Get-Command winget -ErrorAction SilentlyContinue)
if (-not $wingetAvailable) {
    try { $wingetAvailable = Test-Path -LiteralPath $winget -ErrorAction Stop } catch { $wingetAvailable = $false }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

$tools = @(
    @{ Name = 'Git'; Command = 'git'; Args = @('--version'); Winget = 'Git.Git' },
    @{ Name = 'GitHub CLI'; Command = 'gh'; Args = @('--version'); Winget = 'GitHub.cli' },
    @{ Name = 'CMake'; Command = 'cmake'; Args = @('--version'); Winget = 'Kitware.CMake' },
    @{ Name = 'PowerShell 7'; Command = 'pwsh'; Args = @('--version'); Winget = 'Microsoft.PowerShell' },
    @{ Name = '7-Zip'; Command = '7z'; Args = @(); Winget = '7zip.7zip' },
    @{ Name = 'Gitleaks'; Command = 'gitleaks'; Args = @('version'); Winget = 'Gitleaks.Gitleaks' }
)

$missing = @()
foreach ($tool in $tools) {
    $command = Get-Command $tool.Command -ErrorAction SilentlyContinue
    if (-not $command -and $tool.Command -eq '7z') {
        $command = Get-Item 'C:\Program Files\7-Zip\7z.exe' -ErrorAction SilentlyContinue
    }
    if (-not $command -and $tool.Command -eq 'gitleaks') {
        $command = Get-ChildItem (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages') -Recurse -Filter gitleaks.exe -ErrorAction SilentlyContinue |
            Select-Object -First 1
    }
    if ($command) {
        Write-Host "[OK] $($tool.Name)"
    } else {
        Write-Warning "[MISSING] $($tool.Name)"
        $missing += $tool
    }
}

if (-not $wingetAvailable) {
    Write-Warning 'Winget is not visible. Open a fresh terminal and verify the App Installer execution alias.'
}

if (Test-Path -LiteralPath $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) { Write-Host "[OK] Visual Studio C++ tools" } else { Write-Warning '[MISSING] Visual Studio C++ tools' }
} else {
    Write-Warning '[MISSING] Visual Studio Installer/vswhere'
}

$qmake = Join-Path $qtRoot 'bin\qmake.exe'
if (Test-Path -LiteralPath $qmake) {
    Write-Host "[OK] Qt $(& $qmake -query QT_VERSION) MSVC x64"
} else {
    Write-Warning "[MISSING] Qt MSVC x64 at $qtRoot"
}

$sdkRoots = @(Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' -Directory -ErrorAction SilentlyContinue)
if ($sdkRoots) { Write-Host "[OK] Windows SDK $($sdkRoots.Name -join ', ')" } else { Write-Warning '[MISSING] Windows SDK' }

if ($InstallMissing -and $missing.Count -gt 0) {
    if (-not $wingetAvailable) { throw 'Winget is required for -InstallMissing.' }
    foreach ($tool in $missing) {
        & $winget show --exact --id $tool.Winget --accept-source-agreements
        if ($LASTEXITCODE -ne 0) { throw "Unable to inspect $($tool.Winget)." }
        & $winget install --exact --id $tool.Winget --accept-package-agreements --accept-source-agreements --disable-interactivity
        if ($LASTEXITCODE -ne 0) { throw "Unable to install $($tool.Winget)." }
    }
}

Write-Host ''
Write-Host 'Visual Studio components: Desktop development with C++, MSVC v143, and a Windows 10/11 SDK.'
Write-Host 'Qt component: Qt 6.7 or newer, MSVC 2022 64-bit.'
Write-Host 'Optional debug component: Windows Settings > Optional features > Graphics Tools.'

if ($missing.Count -gt 0 -and -not $InstallMissing) { exit 1 }
