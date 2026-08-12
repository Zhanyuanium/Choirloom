#Requires -Version 5.1
# ============================================================================
# tests/dev/dispatch.tests.ps1 - M0-002 dev.ps1 scope-dispatch contract tests.
#
# Verifies, through the control plane itself (no bypass, no direct project
# build/test tooling):
#   * help exits 0;
#   * build/test/verify with a MISSING scope exit non-zero (Usage);
#   * build/test/verify with an UNKNOWN scope exit non-zero (Unknown scope);
#   * build/test/verify with EXTRA scope arguments exit non-zero;
#   * (default mode, toolchain present) build/test/verify rational-time
#     succeed and verify emits the scoped "verification passed" language.
#
# Two invocation modes:
#   * Standalone (no switch): runs all of the above; the rational-time success
#     cases run only when cmake + ctest are discoverable, otherwise they are
#     reported as skipped.
#   * -SkipRationalTimeSuccess (also honored via env
#     CHOIRLOOM_DISPATCH_SKIP_RT_SUCCESS=1): runs only the negative-path cases
#     (help/missing/unknown/extra arguments). This is the mode invoked by
#     `dev.ps1 verify rational-time`; it never re-enters the success paths, so
#     there is no recursion. Any failure there fails the verify gate.
#
# Run from anywhere:
#   pwsh -NoProfile -ExecutionPolicy Bypass -File tests/dev/dispatch.tests.ps1
#
# Note: this is a control-plane dispatch test, not a product test; it is not
# part of the M0-002 rational-time CTest scope.
# ============================================================================

param(
    [switch]$SkipRationalTimeSuccess
)
if ($env:CHOIRLOOM_DISPATCH_SKIP_RT_SUCCESS -eq '1') {
    $SkipRationalTimeSuccess = $true
}

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$devPs1   = Join-Path $repoRoot 'dev.ps1'

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

# --- Extra scope arguments must fail (exactly one scope) ---------------------
foreach ($verb in @('build', 'test', 'verify')) {
    $rc = Invoke-DevPs1Silently -DevArgs @($verb, 'rational-time', 'unexpected')
    Assert-Exit -Expected 1 -Actual $rc -What ("{0} rational-time unexpected (extra args)" -f $verb)
}

# --- Rational-time success scope (skipped in verify-embedded mode) ------------
if ($SkipRationalTimeSuccess) {
    Write-Host 'note dispatch: rational-time success cases skipped (negative-path mode, invoked by dev.ps1 verify rational-time).'
    $script:skipped += 3
} else {
    $cmake = Get-Command -Name 'cmake' -CommandType Application -ErrorAction SilentlyContinue
    $ctest = Get-Command -Name 'ctest' -CommandType Application -ErrorAction SilentlyContinue
    if ($null -eq $cmake -or $null -eq $ctest) {
        Write-Host 'warn dispatch: cmake/ctest not on PATH - skipping rational-time success cases (toolchain prerequisite not met).'
        $script:skipped += 3
    } else {
        $rc = Invoke-DevPs1Silently -DevArgs @('build', 'rational-time')
        Assert-Exit -Expected 0 -Actual $rc -What 'build rational-time'

        $rc = Invoke-DevPs1Silently -DevArgs @('test', 'rational-time')
        Assert-Exit -Expected 0 -Actual $rc -What 'test rational-time'

        $out = (& $devPs1 'verify' 'rational-time' *>&1 | Out-String)
        $rc = $LASTEXITCODE
        Assert-Exit -Expected 0 -Actual $rc -What 'verify rational-time'
        $script:checks++
        if ($out -match 'M0-001 RationalTime slice verification passed') {
            Write-Host 'ok   dispatch: verify emits scoped verification-passed language'
        } else {
            $script:failures++
            Write-Host 'FAIL dispatch: verify output missing "M0-001 RationalTime slice verification passed"'
        }
        $script:checks++
        if ($out -match 'milestone gate remains OPEN') {
            Write-Host 'ok   dispatch: verify retains full M0 milestone gate open statement'
        } else {
            $script:failures++
            Write-Host 'FAIL dispatch: verify output missing full M0 milestone-gate-open statement'
        }
    }
}

Write-Host ("dispatch tests: {0} checks, {1} failures, {2} skipped" -f $checks, $failures, $skipped)
exit $(if ($failures -gt 0) { 1 } else { 0 })
