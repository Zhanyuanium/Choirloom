#Requires -Version 5.1
# ============================================================================
# tests/dev/dispatch.tests.ps1 - dev.ps1 scope-dispatch contract tests.
#
# Verifies, through the control plane itself (no bypass, no direct project
# build/test tooling):
#   * help exits 0;
#   * build/test/verify with a MISSING scope exit non-zero (Usage);
#   * build/test/verify with an UNKNOWN scope exit non-zero (Unknown scope);
#   * build/test/verify with EXTRA scope arguments exit non-zero, for BOTH
#     implemented scopes (rational-time, entity-revision);
#   * (default mode, toolchain present) build/test/verify rational-time and
#     entity-revision succeed, and each verify emits its scoped
#     "verification passed" language plus the full-M0-open statement.
#
# Two invocation modes:
#   * Standalone (no switch): runs all of the above; the success cases run only
#     when cmake + ctest are discoverable, otherwise they are skipped.
#   * -SkipSuccessCases (also honored via env CHOIRLOOM_DISPATCH_SKIP_SUCCESS=1):
#     runs only the negative-path cases (help/missing/unknown/extra). This is
#     the mode invoked by `dev.ps1 verify <scope>`; it never re-enters the
#     success paths, so there is no recursion. Any failure there fails the
#     verify gate.
#
# Run from anywhere:
#   pwsh -NoProfile -ExecutionPolicy Bypass -File tests/dev/dispatch.tests.ps1
#
# Note: this is a control-plane dispatch test, not a product test; it is not
# part of either CTest scope.
# ============================================================================

param(
    [switch]$SkipSuccessCases
)
if ($env:CHOIRLOOM_DISPATCH_SKIP_SUCCESS -eq '1') {
    $SkipSuccessCases = $true
}

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$devPs1   = Join-Path $repoRoot 'dev.ps1'
$scopes   = @('rational-time', 'entity-revision')

$failures = 0
$skipped  = 0
$checks   = 0

function Assert-Exit {
    param(
        [int]$Expected,
        [int]$Actual,
        [string]$What
    )
    $script:checks++
    if ($Actual -ne $Expected) {
        $script:failures++
        Write-Host ("FAIL dispatch: {0} - expected exit {1}, got {2}" -f $What, $Expected, $Actual)
    } else {
        Write-Host ("ok   dispatch: {0} (exit {1})" -f $What, $Actual)
    }
}

# Note: parameter named $DevArgs (never $Args, which collides with the
# PowerShell automatic variable). *>&1 merges all streams so output is
# suppressed on success and host output is capturable.
function Invoke-DevPs1Silently {
    param([string[]]$DevArgs)
    $null = & $devPs1 @DevArgs *>&1
    return $LASTEXITCODE
}

# True when the process already carries a usable MSVC C++ environment or an
# installed Visual Studio Build Tools vcvars64.bat is discoverable (the harness
# bootstraps the latter automatically). Used to skip the machine-specific
# success cases when no local MSVC toolchain is available at all.
function Test-LocalToolchainFeasible {
    if (-not [string]::IsNullOrWhiteSpace($env:INCLUDE)) {
        return $true
    }
    if ($env:VSINSTALLDIR -and (Test-Path -LiteralPath (Join-Path $env:VSINSTALLDIR 'VC\Auxiliary\Build\vcvars64.bat'))) {
        return $true
    }
    $pf86 = ${env:ProgramFiles(x86)}
    if ($pf86) {
        $vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $lines = @(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
            if ($LASTEXITCODE -eq 0 -and $lines.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace($lines[0])) {
                return $true
            }
        }
    }
    return $false
}

# --- Help ----------------------------------------------------------------
$rc = Invoke-DevPs1Silently -DevArgs @('-h')
Assert-Exit -Expected 0 -Actual $rc -What 'help'

# --- Missing scope must fail (no default success) --------------------------
foreach ($verb in @('build', 'test', 'verify')) {
    $rc = Invoke-DevPs1Silently -DevArgs @($verb)
    Assert-Exit -Expected 1 -Actual $rc -What ("{0} (missing scope)" -f $verb)
}

# --- Unknown scope must fail ------------------------------------------------
foreach ($verb in @('build', 'test', 'verify')) {
    $rc = Invoke-DevPs1Silently -DevArgs @($verb, 'not-a-scope')
    Assert-Exit -Expected 1 -Actual $rc -What ("{0} not-a-scope (unknown scope)" -f $verb)
    $rc = Invoke-DevPs1Silently -DevArgs @($verb, 'all')
    Assert-Exit -Expected 1 -Actual $rc -What ("{0} all (no alias)" -f $verb)
}

# --- Extra scope arguments must fail (exactly one scope), both scopes --------
foreach ($verb in @('build', 'test', 'verify')) {
    foreach ($scope in $scopes) {
        $rc = Invoke-DevPs1Silently -DevArgs @($verb, $scope, 'unexpected')
        Assert-Exit -Expected 1 -Actual $rc -What ("{0} {1} unexpected (extra args)" -f $verb, $scope)
    }
}

# --- Success scope paths (skipped in verify-embedded mode) --------------------
if ($SkipSuccessCases) {
    Write-Host 'note dispatch: success cases skipped (negative-path mode, invoked by dev.ps1 verify <scope>).'
    $script:skipped += 6
} else {
    $cmake = Get-Command -Name 'cmake' -CommandType Application -ErrorAction SilentlyContinue
    $ctest = Get-Command -Name 'ctest' -CommandType Application -ErrorAction SilentlyContinue
    if ($null -eq $cmake -or $null -eq $ctest) {
        Write-Host 'warn dispatch: cmake/ctest not on PATH - skipping success cases (toolchain prerequisite not met).'
        $script:skipped += 6
    } elseif (-not (Test-LocalToolchainFeasible)) {
        Write-Host 'warn dispatch: no local MSVC toolchain discoverable and no MSVC C++ env present - skipping success cases (machine-specific bootstrap unavailable).'
        $script:skipped += 6
    } else {
        foreach ($scope in $scopes) {
            $rc = Invoke-DevPs1Silently -DevArgs @('build', $scope)
            Assert-Exit -Expected 0 -Actual $rc -What ("build {0}" -f $scope)

            $rc = Invoke-DevPs1Silently -DevArgs @('test', $scope)
            Assert-Exit -Expected 0 -Actual $rc -What ("test {0}" -f $scope)

            $out = (& $devPs1 'verify' $scope *>&1 | Out-String)
            $rc = $LASTEXITCODE
            Assert-Exit -Expected 0 -Actual $rc -What ("verify {0}" -f $scope)

            # Exact scoped verification language per scope (never generic).
            $expectedLabel = if ($scope -eq 'rational-time') {
                'M0-001 RationalTime slice verification passed'
            } else {
                'M0-003 EntityId/Revision primitive verification passed'
            }
            $script:checks++
            if ($out -match [regex]::Escape($expectedLabel)) {
                Write-Host ("ok   dispatch: verify {0} emits exact scoped language ''{1}''" -f $scope, $expectedLabel)
            } else {
                $script:failures++
                Write-Host ("FAIL dispatch: verify {0} output missing exact scoped language ''{1}''" -f $scope, $expectedLabel)
            }
            $script:checks++
            if ($out -match 'milestone gate remains OPEN') {
                Write-Host ("ok   dispatch: verify {0} retains full M0 milestone gate open statement" -f $scope)
            } else {
                $script:failures++
                Write-Host ("FAIL dispatch: verify {0} output missing full M0 milestone-gate-open statement" -f $scope)
            }
        }
    }
}

Write-Host ("dispatch tests: {0} checks, {1} failures, {2} skipped" -f $checks, $failures, $skipped)
exit $(if ($failures -gt 0) { 1 } else { 0 })
