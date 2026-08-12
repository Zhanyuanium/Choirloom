#Requires -Version 5.1
# ============================================================================
#  dev.ps1 — Choirloom development control plane (thin entry point).
#  All logic lives in tools/dev/Dev.psm1; this script only parses arguments
#  and dispatches.  Windows PowerShell 5.1 compatible.
#
#  Usage:
#    ./dev.ps1 doctor
#    ./dev.ps1 build|test|verify [scope]
#    ./dev.ps1 remote setup|doctor|submit|status|logs|cancel|fetch-report|fetch-model [-Confirm]
#    ./dev.ps1 model validate|benchmark <path>
#
#  If execution policy blocks this script, run:
#    pwsh -NoProfile -ExecutionPolicy Bypass -File .\dev.ps1 doctor
# ============================================================================

$ErrorActionPreference = 'Stop'

function Show-DevUsage {
    $lines = @(
        'Choirloom dev control plane'
        ''
        'Usage:'
        '  dev.ps1 doctor'
        '  dev.ps1 build <scope>        (Implemented for scope: rational-time)'
        '  dev.ps1 test <scope>         (Implemented for scope: rational-time)'
        '  dev.ps1 verify <scope>       (Implemented for scope: rational-time)'
        '  dev.ps1 remote setup|doctor|submit|status|logs|cancel|fetch-report|fetch-model [-Confirm]'
        '  dev.ps1 model validate|benchmark <path>'
        ''
        'Notes:'
        '  * build/test/verify require exactly one scope argument. Only "rational-time"'
        '    is implemented (M0-002 harness). Missing, unknown, or extra scope'
        '    arguments exit non-zero (Usage / Unknown scope); there is no default'
        '    scope and no all/core/scoreir aliases.'
        '  * The rational-time harness invokes local CMake/CTest only (cmake >= 3.20,'
        '    ctest and a C++20 toolchain visible to cmake are required; Ninja is used'
        '    when available). It never downloads, installs, or uses remote/model/network.'
        '  * Mutating remote operations (setup/submit/cancel/fetch-model) require'
        '    -Confirm or interactive confirmation; refused in non-interactive shells.'
        '  * remote setup is an interactive wizard (local config only). It never'
        '    copies/transfers a private key and never writes SSH config; optional'
        '    key generation requires explicit per-layer confirmation.'
        '  * Remote config lives in .dev/remote.local.json (git-ignored, no secrets).'
        '  * If execution policy blocks this script:'
        '    pwsh -NoProfile -ExecutionPolicy Bypass -File .\dev.ps1 <command>'
    )
    $lines | ForEach-Object { Write-Host $_ }
}

function Get-DevArg {
    param([int]$Index)
    if ($script:positional.Count -gt $Index) { return $script:positional[$Index] }
    return $null
}

$script:confirmFlag = $false
$script:positional = @()

foreach ($a in $args) {
    if ($a -is [string]) {
        if ($a -eq '-Confirm' -or $a -eq '-confirm') { $script:confirmFlag = $true; continue }
        if ($a -match '^-Confirm:(.+)$') { $script:confirmFlag = ($Matches[1] -notmatch '^(false|0)$'); continue }
        if ($a -eq '-h' -or $a -eq '-?' -or $a -eq '--help' -or $a -eq 'help') { Show-DevUsage; exit 0 }
    }
    $script:positional += $a
}

if ($script:positional.Count -eq 0) {
    Show-DevUsage
    Write-Host ''
    Write-Host 'No command given.' -ForegroundColor Yellow
    exit 1
}

try {
    Import-Module (Join-Path $PSScriptRoot 'tools\dev\Dev.psm1') -Force
    $command = [string]$script:positional[0]
    switch ($command) {
        'doctor' { Invoke-DevDoctor }
        'build'  {
            if ($script:positional.Count -ne 2) {
                throw 'Usage: dev.ps1 build <scope>. Exactly one scope argument is required; missing or extra arguments are rejected. Only scope "rational-time" is implemented (M0-002 harness).'
            }
            Invoke-DevBuild -Scope (Get-DevArg 1)
        }
        'test'   {
            if ($script:positional.Count -ne 2) {
                throw 'Usage: dev.ps1 test <scope>. Exactly one scope argument is required; missing or extra arguments are rejected. Only scope "rational-time" is implemented (M0-002 harness).'
            }
            Invoke-DevTest -Scope (Get-DevArg 1)
        }
        'verify' {
            if ($script:positional.Count -ne 2) {
                throw 'Usage: dev.ps1 verify <scope>. Exactly one scope argument is required; missing or extra arguments are rejected. Only scope "rational-time" is implemented (M0-002 harness).'
            }
            Invoke-DevVerify -Scope (Get-DevArg 1)
        }
        'remote' {
            if ($script:positional.Count -lt 2) {
                throw 'Usage: dev.ps1 remote <setup|doctor|submit|status|logs|cancel|fetch-report|fetch-model> [-Confirm]'
            }
            Invoke-DevRemote -SubCommand ([string]$script:positional[1]) -Confirm:$script:confirmFlag -Argument (Get-DevArg 2)
        }
        'model' {
            if ($script:positional.Count -lt 2) {
                throw 'Usage: dev.ps1 model <validate|benchmark> <path>'
            }
            Invoke-DevModel -SubCommand ([string]$script:positional[1]) -Path (Get-DevArg 2)
        }
        default { throw ("Unknown command '{0}'. Run 'dev.ps1 help' for usage." -f $command) }
    }
    Write-Host ''
    Write-Host '[dev] OK' -ForegroundColor Green
    exit 0
} catch {
    Write-Host ''
    Write-Host ('[dev] error: {0}' -f $_.Exception.Message) -ForegroundColor Red
    exit 1
}
