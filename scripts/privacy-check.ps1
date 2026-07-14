[CmdletBinding(DefaultParameterSetName = 'WorkingTree')]
param(
    [Parameter(ParameterSetName = 'Staged')]
    [switch]$Staged,

    [Parameter(ParameterSetName = 'Range', Mandatory = $true)]
    [string]$Range,

    [string]$PrivatePatternFile
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    if ($Staged) {
        $files = @(git diff --cached --name-only --diff-filter=ACMR)
        $contentCommand = { param($file) git show ":$file" 2>$null | Out-String }
    } elseif ($Range) {
        git rev-parse --verify $Range.Split('..')[0] 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Invalid Git range: $Range" }
        $files = @(git diff --name-only --diff-filter=ACMR $Range)
        $contentCommand = { param($file) git show "HEAD:$file" 2>$null | Out-String }
    } else {
        $files = @(git ls-files --cached --others --exclude-standard)
        $contentCommand = {
            param($file)
            if (Test-Path -LiteralPath $file -PathType Leaf) {
                Get-Content -LiteralPath $file -Raw -ErrorAction SilentlyContinue
            }
        }
    }

    $bannedExtensions = @('.dmp', '.etl', '.pcap', '.pcapng', '.pdb', '.zip', '.7z', '.tar', '.gz')
    $rules = [ordered]@{
        'private-ipv4' = '(?<![0-9])(?:10\.(?:[0-9]{1,3}\.){2}[0-9]{1,3}|192\.168\.(?:[0-9]{1,3}\.)[0-9]{1,3}|172\.(?:1[6-9]|2[0-9]|3[01])\.(?:[0-9]{1,3}\.)[0-9]{1,3})(?![0-9])'
        'windows-user-path' = '(?i)[A-Z]:\\Users\\[^\\\s]+'
        'unc-path' = '(?i)\\\\[^\\\s]+\\[^\\\s]+'
        'private-key' = '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'
        'github-token' = '(?i)\b(?:gh[opusr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,})\b'
        'generic-secret' = '(?i)\b(?:api[_-]?key|access[_-]?token|client[_-]?secret|password)\s*[:=]\s*["''][^"'']{8,}["'']'
    }

    $privatePatterns = @()
    if ($PrivatePatternFile -and (Test-Path -LiteralPath $PrivatePatternFile)) {
        $privatePatterns = @(Get-Content -LiteralPath $PrivatePatternFile |
            Where-Object { $_ -and -not $_.StartsWith('#') })
    }

    $failures = 0
    foreach ($file in $files | Sort-Object -Unique) {
        if (-not $file) { continue }

        $extension = [IO.Path]::GetExtension($file).ToLowerInvariant()
        if ($bannedExtensions -contains $extension) {
            Write-Error "[banned-artifact] $file" -ErrorAction Continue
            $failures++
            continue
        }

        if ($file -match '(^|/)(?:\.private|vendor-local|tools-local|diagnostics|captures|logs)(/|$)') {
            Write-Error "[local-only-path] $file" -ErrorAction Continue
            $failures++
            continue
        }

        $content = & $contentCommand $file
        if ($null -eq $content) { continue }

        foreach ($rule in $rules.GetEnumerator()) {
            if ($content -match $rule.Value) {
                Write-Error "[$($rule.Key)] $file" -ErrorAction Continue
                $failures++
            }
        }

        $emailMatches = [regex]::Matches($content, '(?i)\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b')
        foreach ($email in $emailMatches) {
            if (-not $email.Value.EndsWith('@users.noreply.github.com', [StringComparison]::OrdinalIgnoreCase)) {
                Write-Error "[email-address] $file" -ErrorAction Continue
                $failures++
                break
            }
        }

        foreach ($privatePattern in $privatePatterns) {
            if ($content.IndexOf($privatePattern, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                Write-Error "[local-private-pattern] $file" -ErrorAction Continue
                $failures++
                break
            }
        }
    }

    if ($failures -gt 0) {
        throw "Privacy check failed with $failures finding(s). Values were intentionally redacted."
    }

    Write-Host "Privacy check passed for $($files.Count) file(s)."
} finally {
    Pop-Location
}
