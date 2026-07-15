[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath
)

$ErrorActionPreference = 'Stop'
$resolvedPackage = (Resolve-Path -LiteralPath $PackagePath).Path
Add-Type -AssemblyName System.IO.Compression.FileSystem

$archive = [IO.Compression.ZipFile]::OpenRead($resolvedPackage)
try {
    $rootExecutables = @($archive.Entries | Where-Object {
        $_.FullName -notmatch '[/\\]' -and $_.Name.EndsWith('.exe', [StringComparison]::OrdinalIgnoreCase)
    })
    if ($rootExecutables.Count -ne 1 -or
            $rootExecutables[0].Name -cne 'MoonlightArtist.exe') {
        throw 'Portable package must contain exactly one root executable named MoonlightArtist.exe.'
    }

    $entryNames = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\\', '/') })
    $forbiddenSuffixes = @('.pdb', '.log', '.dmp', '.etl', '.pcap', '.pcapng')
    foreach ($name in $entryNames) {
        foreach ($suffix in $forbiddenSuffixes) {
            if ($name.EndsWith($suffix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Portable package contains a forbidden artifact type: $suffix"
            }
        }
    }

    $requiredRuntimeFiles = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
    foreach ($runtime in $requiredRuntimeFiles) {
        if (-not ($entryNames -contains $runtime)) {
            throw "Portable package is missing required release runtime file: $runtime"
        }
    }

    $executableStream = $rootExecutables[0].Open()
    try {
        $memory = New-Object IO.MemoryStream
        $executableStream.CopyTo($memory)
        $peText = [Text.Encoding]::ASCII.GetString($memory.ToArray())
    } finally {
        $executableStream.Dispose()
        if ($memory) { $memory.Dispose() }
    }

    $debugRuntimeImports = @('MSVCP140D.dll', 'VCRUNTIME140D.dll',
                             'VCRUNTIME140_1D.dll', 'ucrtbased.dll')
    foreach ($runtime in $debugRuntimeImports) {
        if ($peText.IndexOf($runtime, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "MoonlightArtist.exe imports a non-redistributable debug runtime: $runtime"
        }
    }
} finally {
    $archive.Dispose()
}

Write-Host 'Portable package verification passed: one Release executable and no debug-runtime imports.'
