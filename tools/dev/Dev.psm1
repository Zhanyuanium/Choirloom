#Requires -Version 5.1
# ============================================================================
#  Dev.psm1 — Choirloom development control plane (implementation).
#
#  All complex logic lives here; dev.ps1 is a thin entry point.
#  Written for Windows PowerShell 5.1 compatibility (also runs on pwsh 7+).
#
#  The repository currently has NO build system, NO test/verify harness and NO
#  training/runner pipeline (baseline is spec-only, pre-M0 implementation).
#  Every operation that is not yet backed by a real implementation MUST fail
#  with an explicit "NotImplemented:" message and its prerequisite.  It must
#  NEVER fake success.
#
#  This module never edits SSH config and never copies or transfers private
#  keys. The only mutating action anywhere is the OPTIONAL, explicitly
#  user-confirmed `ssh-keygen` step inside `remote setup`; everything else is
#  read-only. Remote diagnostics are strictly read-only.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Public API (exported)
# ---------------------------------------------------------------------------
function Invoke-DevDoctor {
    Write-DevHeader 'Choirloom dev doctor'
    $repoRoot = Get-DevRepoRoot
    $failures = 0

    # --- Runtime ------------------------------------------------------------
    Write-DevHeader 'Runtime'
    Write-DevInfo ('Host: {0}' -f $Host.Name)
    $edition = [string]$PSVersionTable.PSEdition
    $version = [string]$PSVersionTable.PSVersion
    Write-DevInfo ('PowerShell: {0} ({1})' -f $version, $edition)
    if ($edition -eq 'Desktop') {
        $v = $PSVersionTable.PSVersion
        if ($v.Major -lt 5 -or ($v.Major -eq 5 -and $v.Minor -lt 1)) {
            Write-DevFail ('Windows PowerShell 5.1+ required (found {0}).' -f $version)
            $failures++
        } else {
            Write-DevOk ('Windows PowerShell {0} — 5.1+ compatible.' -f $version)
        }
    } else {
        Write-DevOk 'PowerShell Core.'
    }
    $pwsh = Get-DevCommand 'pwsh'
    if ($null -ne $pwsh) {
        Write-DevOk ('pwsh on PATH: {0}' -f $pwsh.Version)
    } else {
        Write-DevWarn 'pwsh not on PATH (optional; Windows PowerShell 5.1 is also supported).'
    }

    # --- Repository / git ----------------------------------------------------
    Write-DevHeader 'Repository'
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot '.git'))) {
        Write-DevFail ('Not a git repository (missing .git at {0}).' -f $repoRoot)
        $failures++
    } else {
        Write-DevOk ('Git repository present: {0}' -f $repoRoot)
    }
    $git = Get-DevCommand 'git'
    if ($null -eq $git) {
        Write-DevFail 'git not found on PATH.'
        $failures++
    } else {
        try {
            $gitVersion = (& git --version 2>&1 | Out-String).Trim()
            Write-DevOk ('git: {0}' -f $gitVersion)
        } catch {
            Write-DevFail ('git found but failed to run: {0}' -f $_.Exception.Message)
            $failures++
        }
        # Read-only plumbing queries only — doctor must not write files.
        try {
            $head = (& git rev-parse HEAD 2>&1 | Out-String).Trim()
            $short = (& git rev-parse --short HEAD 2>&1 | Out-String).Trim()
            Write-DevInfo ('HEAD: {0} (short: {1})' -f $head, $short)
        } catch {
            Write-DevWarn ('Could not read git HEAD: {0}' -f $_.Exception.Message)
        }
        try {
            $n = (& git ls-files 2>&1 | Measure-Object -Line).Lines
            Write-DevInfo ('Tracked files: {0}' -f $n)
        } catch {
            Write-DevWarn ('Could not list tracked files: {0}' -f $_.Exception.Message)
        }
    }
    foreach ($requiredFile in @('dev.ps1', 'spec', 'LICENSE', '.dev\remote.example.json', 'tools\dev\Dev.psm1')) {
        $p = Join-Path $repoRoot $requiredFile
        if (Test-Path -LiteralPath $p) { Write-DevOk ('Present: {0}' -f $requiredFile) }
        else { Write-DevWarn ('Expected in baseline but missing: {0}' -f $requiredFile) }
    }

    # --- Optional tooling -----------------------------------------------------
    Write-DevHeader 'Optional tooling'
    $ohmy = Get-DevCommand 'ohmy'
    if ($null -ne $ohmy) {
        try {
            $ohmyOut = (& ohmy --version 2>&1 | Out-String).Trim()
            if ($ohmyOut) { Write-DevOk ('OhMy CLI: {0}' -f $ohmyOut) }
            else { Write-DevOk 'OhMy CLI found (no version output).' }
        } catch {
            # doctor must NOT fail because an optional CLI cannot execute.
            Write-DevWarn 'OhMy CLI found but could not execute its version probe (optional tooling; skipped).'
        }
    } else {
        Write-DevWarn 'OhMy CLI (ohmy) not found — optional.'
    }
    $ssh = Get-DevCommand 'ssh'
    if ($null -ne $ssh) {
        try {
            $sshVer = (& ssh -V 2>&1 | Out-String).Trim()
            Write-DevOk ('OpenSSH client: {0}' -f $sshVer)
        } catch {
            Write-DevWarn ('ssh found but could not run: {0}' -f $_.Exception.Message)
            $ssh = $null
        }
    } else {
        Write-DevWarn 'OpenSSH client (ssh) not found — remote doctor unavailable (optional for local work).'
    }

    # --- Remote configuration -------------------------------------------------
    Write-DevHeader 'Remote configuration'
    $cfgPath = Join-Path $repoRoot '.dev\remote.local.json'
    $cfg = $null
    if (Test-Path -LiteralPath $cfgPath) {
        try {
            $cfg = Read-DevRemoteConfigFile -Path $cfgPath
            Write-DevOk ('Valid remote config: {0}' -f $cfgPath)
        } catch {
            Write-DevFail $_.Exception.Message
            $failures++
        }
    } else {
        Write-DevInfo 'No .dev/remote.local.json yet — remote operations unavailable until configured (see tools/dev/remote/README.md).'
    }

    # --- Read-only remote probe (only when config + ssh are both present) ----
    if ($null -ne $cfg) {
        if ($null -ne $ssh) {
            try {
                Invoke-DevRemoteProbe -Config $cfg
            } catch {
                Write-DevFail ('Remote probe failed: {0}' -f $_.Exception.Message)
                $failures++
            }
        } else {
            Write-DevWarn 'Remote config present but ssh client missing — remote probe skipped.'
        }
    }

    Write-DevHeader 'Summary'
    if ($failures -gt 0) {
        throw ('doctor found {0} problem(s). Fix them and re-run.' -f $failures)
    }
    Write-DevOk 'All local checks passed.'
}

function Invoke-DevBuild {
    param([string]$Scope)
    Write-DevHeader 'build'
    $scopeText = if ([string]::IsNullOrWhiteSpace($Scope)) { '(default)' } else { $Scope }
    Write-DevInfo ('Scope: {0}' -f $scopeText)
    throw 'NotImplemented: there is no build system in this repository yet (baseline is spec-only, pre-M0). Prerequisite: add a build system + harness (see spec/v0.1/03-development-plan.md, M0 deliverables) before build can pass.'
}

function Invoke-DevTest {
    param([string]$Scope)
    Write-DevHeader 'test'
    $scopeText = if ([string]::IsNullOrWhiteSpace($Scope)) { '(default)' } else { $Scope }
    Write-DevInfo ('Scope: {0}' -f $scopeText)
    throw 'NotImplemented: there is no test harness in this repository yet (baseline is spec-only, pre-M0). Prerequisite: harness + fixture infrastructure (spec/v0.1/03-development-plan.md §12) before test can pass.'
}

function Invoke-DevVerify {
    param([string]$Scope)
    Write-DevHeader 'verify'
    $scopeText = if ([string]::IsNullOrWhiteSpace($Scope)) { '(default)' } else { $Scope }
    Write-DevInfo ('Scope: {0}' -f $scopeText)
    throw 'NotImplemented: there is no verification harness in this repository yet (baseline is spec-only, pre-M0). Prerequisite: verification/CI gate per Definition of Done (spec/v0.1/03-development-plan.md §13) before verify can pass.'
}

function Invoke-DevRemote {
    param(
        [Parameter(Mandatory = $true)][string]$SubCommand,
        [switch]$Confirm,
        [string]$Argument
    )
    switch ($SubCommand.ToLowerInvariant()) {
        'setup'        { Invoke-DevRemoteSetup -Confirm:$Confirm }
        'doctor'       { Invoke-DevRemoteDoctor }
        'submit'       { Invoke-DevRemoteSubmit -Confirm:$Confirm -ManifestPath $Argument }
        'status'       { Invoke-DevRemoteStatus }
        'logs'         { Invoke-DevRemoteLogs }
        'cancel'       { Invoke-DevRemoteCancel -Confirm:$Confirm }
        'fetch-report' { Invoke-DevRemoteFetchReport }
        'fetch-model'  { Invoke-DevRemoteFetchModel -Confirm:$Confirm -OutputPath $Argument }
        default {
            throw ('Unknown remote subcommand ''{0}''. Supported: setup, doctor, submit, status, logs, cancel, fetch-report, fetch-model.' -f $SubCommand)
        }
    }
}

function Invoke-DevModel {
    param(
        [Parameter(Mandatory = $true)][string]$SubCommand,
        [string]$Path
    )
    switch ($SubCommand.ToLowerInvariant()) {
        'validate'  { Invoke-DevModelValidate -Path $Path }
        'benchmark' { Invoke-DevModelBenchmark -Path $Path }
        default { throw ('Unknown model subcommand ''{0}''. Supported: validate, benchmark.' -f $SubCommand) }
    }
}

# ---------------------------------------------------------------------------
# Remote sub-commands
# ---------------------------------------------------------------------------
function Invoke-DevRemoteSetup {
    param([switch]$Confirm)

    Write-DevHeader 'remote setup'

    # --- Entry gate: without -Confirm never prompt, never block, exit non-zero.
    if (-not $Confirm) {
        Write-DevInfo 'This wizard collects the following NON-SECRET fields into .dev/remote.local.json:'
        Write-DevInfo '  host, user, port, remoteBaseDirectory  (optional: sshAlias, identityFile path)'
        Write-DevInfo 'It never requests or stores a password, token, or private key.'
        Write-DevInfo 'It never copies or transfers a private key, and it never writes an SSH config file.'
        Write-DevInfo 'It never runs a remote SSH probe.'
        throw "Refusing 'remote setup' without confirmation: re-run with -Confirm to enter the interactive wizard. (-Confirm only enters the wizard; every action inside is still confirmed interactively.)"
    }

    if (-not (Test-DevInteractive)) {
        throw 'remote setup is interactive-only: -Confirm grants entry, but the wizard prompts for each field and requires typing yes at key steps. Run it in an interactive terminal.'
    }

    Write-DevInfo 'Entering interactive remote setup. Fields are validated as you go.'
    Write-DevInfo 'No password/token/private key will be requested or stored.'
    Write-DevInfo 'No SSH config file will be written and no remote SSH probe will run during setup.'

    # --- OpenSSH tooling check (report only; setup can still write config) ---
    $ssh = Get-DevCommand 'ssh'
    $keygen = Get-DevCommand 'ssh-keygen'
    if ($null -ne $ssh) { Write-DevOk 'OpenSSH client (ssh) found.' }
    else { Write-DevWarn 'ssh not found on PATH — remote doctor will not work until it is installed.' }
    if ($null -ne $keygen) { Write-DevOk 'ssh-keygen found.' }
    else { Write-DevWarn 'ssh-keygen not found on PATH — key generation will be skipped.' }

    # --- Step 2: ask the non-secret fields (strict validation, re-prompt) -----
    Write-DevHeader 'Connection fields'
    $hostName = Read-DevSetupValue -Prompt 'Remote hostname or IP' -Validate {
        param($v) $v -match '^[A-Za-z0-9._\-:\[\]]+$'
    } -InvalidMessage 'Invalid host (letters, digits, . _ - : [ ] only).'
    $user = Read-DevSetupValue -Prompt 'SSH user' -Validate {
        param($v) $v -match '^[A-Za-z0-9._\-]+$'
    } -InvalidMessage 'Invalid user (letters, digits, . _ - only).'
    $port = Read-DevSetupValue -Prompt 'SSH port' -Default '22' -Validate {
        param($v)
        $n = 0
        return ([int]::TryParse($v, [ref]$n) -and $n -ge 1 -and $n -le 65535)
    } -InvalidMessage 'Port must be an integer between 1 and 65535.'
    $portInt = [int]$port
    $base = Read-DevSetupValue -Prompt 'Remote project base directory (absolute POSIX path)' -Validate {
        param($v) ($v -match '^/') -and ($v -notmatch "['`r`n]")
    } -InvalidMessage 'Must be an absolute POSIX path starting with / (no quotes/newlines).'
    $sshAlias = Read-DevSetupValue -Prompt 'Optional ssh alias from your ssh config (Enter to skip)' -AllowEmpty -Validate {
        param($v) $v -eq '' -or $v -match '^[A-Za-z0-9._\-]+$'
    } -InvalidMessage 'Invalid alias (letters, digits, . _ - only).'

    # --- Step 3: optional key generation (never overwrite; layer-by-layer) ----
    Write-DevHeader 'SSH key'
    $keyPath = Join-Path $env:USERPROFILE '.ssh\choirloom-opencode-ed25519'
    $keygenRan = $false
    if (Test-Path -LiteralPath $keyPath) {
        Write-DevOk ('Key already exists — will NOT be overwritten: {0}' -f $keyPath)
    } elseif ($null -eq $keygen) {
        Write-DevWarn 'ssh-keygen not available; skipping key generation.'
    } else {
        Write-DevInfo ('Suggested key path: {0}' -f $keyPath)
        $answer = Read-Host "Generate a new ed25519 key pair at this path? Type 'yes' to generate (default: no)"
        if ($answer -eq 'yes') {
            $passAnswer = Read-Host "Use a passphrase on this key? Type 'yes' for an interactive passphrase prompt; anything else means you EXPLICITLY choose an empty passphrase (confirmed next)"
            if ($passAnswer -eq 'yes') {
                Write-DevInfo 'ssh-keygen will now prompt for a passphrase (twice).'
                & ssh-keygen -t ed25519 -f $keyPath
            } else {
                Write-DevWarn 'You are choosing an EMPTY passphrase for this key.'
                $emptyOk = Read-Host "Type 'yes' to confirm an empty passphrase, anything else to abort key generation"
                if ($emptyOk -ne 'yes') { throw 'Aborted: key generation declined. No key was created.' }
                & ssh-keygen -t ed25519 -f $keyPath -N ''
            }
            if ($LASTEXITCODE -ne 0) {
                throw ('ssh-keygen failed (exit code {0}). No key was created.' -f $LASTEXITCODE)
            }
            $keygenRan = $true
            Write-DevOk ('Key generated: {0}' -f $keyPath)
        } else {
            Write-DevInfo 'No key generated. You may reference an existing key via identityFile or rely on your SSH agent.'
        }
    }

    # --- Step 3b: identityFile (optional, path reference only) -----------------
    $identityFileDefault = ''
    if (Test-Path -LiteralPath $keyPath) { $identityFileDefault = $keyPath }
    $identityFile = Read-DevSetupValue -Prompt 'identityFile (local path to your private key, or Enter for none)' -Default $identityFileDefault -AllowEmpty -Validate {
        param($v)
        if ($v -eq '') { return $true }
        if ($v -match "['`r`n]" -or $v -match 'PRIVATE KEY|BEGIN ') { return $false }
        return $true
    } -InvalidMessage 'identityFile must be a plain local path — never inline private key material.'
    if (-not [string]::IsNullOrWhiteSpace($identityFile)) {
        $repoRoot = Get-DevRepoRoot
        if (-not $identityFile.StartsWith('~', [System.StringComparison]::Ordinal)) {
            try {
                $full = [System.IO.Path]::GetFullPath($identityFile)
                if ($full.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                    throw ('identityFile must not point inside the repository ({0}). Private keys never belong in the repo.' -f $repoRoot)
                }
            } catch {
                # un-resolvable relative path: leave as-is
            }
        }
    }

    # --- Step 4: public key / authorized_keys (manual only) -------------------
    $pubPath = $keyPath + '.pub'
    if (Test-Path -LiteralPath $pubPath) {
        Write-DevHeader 'Public key (manual install)'
        Write-DevInfo ('Public key path: {0}' -f $pubPath)
        Write-DevInfo 'Append this key to ~/.ssh/authorized_keys on the server using an EXISTING login method.'
        Write-DevInfo 'The private key never leaves this machine. This wizard never copies it and never runs ssh-copy-id.'
        Write-DevInfo '--- public key content (safe to share) ---'
        Write-Host ((Get-Content -LiteralPath $pubPath -Raw).Trim())
        Write-DevInfo '--- end public key ---'
    } else {
        Write-DevInfo 'No public key at the suggested path. Skip the manual install, or reference your own key via identityFile.'
    }

    # --- Step 5: suggested ssh config stanza (never written here) -------------
    Write-DevHeader 'SSH config (never written by this wizard)'
    Write-DevInfo ('If you want an alias, edit {0} yourself. This wizard never writes it.' -f (Join-Path $env:USERPROFILE '.ssh\config'))
    $stanza = @()
    $stanza += 'Host {0}' -f $(if ($sshAlias) { $sshAlias } else { $hostName })
    $stanza += '    HostName {0}' -f $hostName
    $stanza += '    User {0}' -f $user
    $stanza += '    Port {0}' -f $portInt
    if (-not [string]::IsNullOrWhiteSpace($identityFile)) { $stanza += '    IdentityFile {0}' -f $identityFile }
    $stanza | ForEach-Object { Write-Host ('    {0}' -f $_) }
    $null = Read-Host "Review the stanza above. Add it manually to your ssh config later if desired. Press Enter to continue"

    # --- Step 6: summary + explicit 'yes' before writing the config -----------
    Write-DevHeader 'Summary'
    Write-DevInfo ('  host:                {0}' -f $hostName)
    Write-DevInfo ('  user:                {0}' -f $user)
    Write-DevInfo ('  port:                {0}' -f $portInt)
    Write-DevInfo ('  remoteBaseDirectory: {0}' -f $base)
    Write-DevInfo ('  sshAlias:            {0}' -f $(if ($sshAlias) { $sshAlias } else { '(none)' }))
    Write-DevInfo ('  identityFile:        {0}' -f $(if ($identityFile) { $identityFile } else { '(default SSH agent/key)' }))
    $cfgPath = Join-Path (Get-DevRepoRoot) '.dev\remote.local.json'
    if (Test-Path -LiteralPath $cfgPath) {
        Write-DevWarn ('This OVERWRITES the existing config at {0}.' -f $cfgPath)
    } else {
        Write-DevInfo ('This writes {0} (git-ignored, no secrets).' -f $cfgPath)
    }
    $answer = Read-Host "Type 'yes' to write the config, anything else to abort"
    if ($answer -ne 'yes') { throw 'Aborted remote setup: no config was written.' }

    $configObj = [PSCustomObject][ordered]@{
        host                = $hostName
        user                = $user
        port                = $portInt
        remoteBaseDirectory = $base
        sshAlias            = $sshAlias
        identityFile        = $identityFile
    }
    $json = $configObj | ConvertTo-Json
    try {
        [System.IO.File]::WriteAllText($cfgPath, $json)
    } catch {
        throw ('Could not write {0}: {1}' -f $cfgPath, $_.Exception.Message)
    }
    Write-DevOk ('Saved: {0}' -f $cfgPath)
    Write-DevOk '.dev/remote.local.json is git-ignored and contains no secrets.'

    # --- Step 7: post-setup guidance (never probe now) ------------------------
    Write-DevHeader 'Next steps'
    Write-DevInfo '1. Install the public key into ~/.ssh/authorized_keys on the server (manual, using an existing login).'
    if ($keygenRan) { Write-DevInfo '2. If you set a passphrase, load the key into ssh-agent once (ssh-add <path>).' }
    Write-DevInfo '3. Then run:  .\dev.ps1 remote doctor   (read-only verification).'
    Write-DevInfo 'No remote probe ran during setup and none will run until you invoke remote doctor.'
}

function Invoke-DevRemoteDoctor {
    $cfg = Read-DevRemoteConfig
    Write-DevHeader 'remote doctor'
    Invoke-DevRemoteProbe -Config $cfg
    Write-DevOk 'Remote doctor completed (read-only).'
}

function Invoke-DevRemoteSubmit {
    param([switch]$Confirm, [string]$ManifestPath)
    Confirm-DevWriteOperation -Operation 'remote submit' -Confirm:$Confirm
    $null = Read-DevRemoteConfig
    if (-not [string]::IsNullOrWhiteSpace($ManifestPath) -and -not (Test-Path -LiteralPath $ManifestPath)) {
        throw ('Manifest not found: {0}' -f $ManifestPath)
    }
    throw 'NotImplemented: remote submit requires a runner protocol (job endpoints, manifest schema, pinned git SHA) that does not exist yet. Prerequisite: implement the runner protocol (spec M0+/M4). Contract is stable: dev.ps1 remote submit [manifestPath] -Confirm.'
}

function Invoke-DevRemoteStatus {
    $null = Read-DevRemoteConfig
    throw 'NotImplemented: remote status requires a server-side runner protocol (job state query) that does not exist yet. Prerequisite: implement the runner protocol first; the control plane intentionally does not guess it. Contract is stable: dev.ps1 remote status.'
}

function Invoke-DevRemoteLogs {
    $null = Read-DevRemoteConfig
    throw 'NotImplemented: remote logs requires a server-side runner protocol (log retrieval) that does not exist yet. Prerequisite: implement the runner protocol first. Contract is stable: dev.ps1 remote logs.'
}

function Invoke-DevRemoteCancel {
    param([switch]$Confirm)
    Confirm-DevWriteOperation -Operation 'remote cancel' -Confirm:$Confirm
    $null = Read-DevRemoteConfig
    throw 'NotImplemented: remote cancel requires a runner protocol (job control) that does not exist yet. Prerequisite: implement the runner protocol first. Contract is stable: dev.ps1 remote cancel -Confirm.'
}

function Invoke-DevRemoteFetchReport {
    $null = Read-DevRemoteConfig
    throw 'NotImplemented: remote fetch-report requires a runner protocol (result/report retrieval) that does not exist yet. Prerequisite: implement the runner protocol first. Contract is stable: dev.ps1 remote fetch-report.'
}

function Invoke-DevRemoteFetchModel {
    param([switch]$Confirm, [string]$OutputPath)
    Confirm-DevWriteOperation -Operation 'remote fetch-model' -Confirm:$Confirm
    $null = Read-DevRemoteConfig
    throw 'NotImplemented: remote fetch-model requires a runner protocol + ModelPackage distribution (manifest/checksum, spec M4 / ModelPackage v0.1) that does not exist yet. Contract is stable: dev.ps1 remote fetch-model [outputPath] -Confirm.'
}

# ---------------------------------------------------------------------------
# Model sub-commands
# ---------------------------------------------------------------------------
function Invoke-DevModelValidate {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'Usage: dev.ps1 model validate <path>' }
    if (-not (Test-Path -LiteralPath $Path)) { throw ('Model path not found: {0}' -f $Path) }
    throw 'NotImplemented: ModelPackage validator does not exist yet. Prerequisite: ModelPackage v0.1 schema + validator implementation (spec M0 deliverable). A path was checked but nothing was validated — this is not success.'
}

function Invoke-DevModelBenchmark {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'Usage: dev.ps1 model benchmark <path>' }
    if (-not (Test-Path -LiteralPath $Path)) { throw ('Model path not found: {0}' -f $Path) }
    throw 'NotImplemented: model benchmark harness does not exist yet. Prerequisite: benchmark harness + corpus (spec/v0.1/03-development-plan.md §12.4) before benchmark can run.'
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Get-DevRepoRoot {
    # tools/dev/Dev.psm1 -> tools/dev -> tools -> repository root
    return Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}

function Get-DevCommand {
    param([Parameter(Mandatory = $true)][string]$Name)
    return Get-Command -Name $Name -ErrorAction SilentlyContinue
}

function Get-DevConfigValue {
    param($Config, [string]$Name)
    $prop = $Config.PSObject.Properties[$Name]
    if ($null -eq $prop) { return $null }
    return $prop.Value
}

function Test-DevInteractive {
    try {
        return (-not [System.Console]::IsInputRedirected) -and (-not [System.Console]::IsOutputRedirected)
    } catch {
        return $false
    }
}

function Read-DevSetupValue {
    param(
        [Parameter(Mandatory = $true)][string]$Prompt,
        [string]$Default = '',
        [scriptblock]$Validate = $null,
        [string]$InvalidMessage = 'Invalid value.',
        [switch]$AllowEmpty
    )
    while ($true) {
        $label = $Prompt
        if (-not [string]::IsNullOrWhiteSpace($Default)) { $label = '{0} [default: {1}]' -f $Prompt, $Default }
        $value = Read-Host $label
        if ([string]::IsNullOrWhiteSpace($value) -and -not [string]::IsNullOrWhiteSpace($Default)) { $value = $Default }
        if ([string]::IsNullOrWhiteSpace($value)) {
            if ($AllowEmpty) { return '' }
            Write-DevWarn 'A value is required.'
            continue
        }
        $value = $value.Trim()
        if ($null -ne $Validate -and -not (& $Validate $value)) {
            Write-DevWarn $InvalidMessage
            continue
        }
        return $value
    }
}

function Confirm-DevWriteOperation {
    param([Parameter(Mandatory = $true)][string]$Operation, [switch]$Confirm)
    if ($Confirm) {
        Write-DevInfo ('Confirmation granted via -Confirm for: {0}' -f $Operation)
        return
    }
    if (-not (Test-DevInteractive)) {
        throw ('Operation ''{0}'' can modify remote/server state. Non-interactive mode refuses it. Re-run in an interactive terminal or pass -Confirm.' -f $Operation)
    }
    Write-DevWarn ('Operation ''{0}'' can modify remote/server state. Confirm to continue.' -f $Operation)
    $answer = Read-Host "Type 'yes' to continue, anything else to abort"
    if ($answer -ne 'yes') { throw ('Aborted ''{0}'': confirmation declined.' -f $Operation) }
    Write-DevInfo ('Confirmed: {0}' -f $Operation)
}

function Read-DevRemoteConfig {
    $path = Join-Path (Get-DevRepoRoot) '.dev\remote.local.json'
    if (-not (Test-Path -LiteralPath $path)) {
        throw 'NotImplemented: no remote configuration found at .dev/remote.local.json. Prerequisite: configure the host (see .dev/remote.example.json and tools/dev/remote/README.md). Local config is git-ignored and never contains secrets.'
    }
    return Read-DevRemoteConfigFile -Path $path
}

function Read-DevRemoteConfigFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    $raw = $null
    try { $raw = Get-Content -LiteralPath $Path -Raw } catch {
        throw ('Cannot read remote config {0}: {1}' -f $Path, $_.Exception.Message)
    }
    if ([string]::IsNullOrWhiteSpace($raw)) { throw ('Remote config {0} is empty.' -f $Path) }
    $cfg = $null
    try { $cfg = $raw | ConvertFrom-Json } catch {
        throw ('Remote config {0} is not valid JSON: {1}' -f $Path, $_.Exception.Message)
    }
    if ($null -eq $cfg -or ($cfg -isnot [System.Management.Automation.PSCustomObject])) {
        throw ('Remote config {0} must be a single JSON object.' -f $Path)
    }

    $allowed = @('host', 'user', 'port', 'remoteBaseDirectory', 'sshAlias', 'identityFile')
    $secretPattern = 'password|passwd|token|secret|api[_ -]?key|private[_ -]?key'
    foreach ($prop in $cfg.PSObject.Properties) {
        if ($allowed -notcontains $prop.Name) {
            throw ('Remote config field ''{0}'' is not allowed. Allowed fields: {1}. No secrets or extra fields.' -f $prop.Name, ($allowed -join ', '))
        }
        if ($prop.Name -match $secretPattern) {
            throw ('Remote config field ''{0}'' is a secret and is not allowed.' -f $prop.Name)
        }
    }

    $hostName = [string](Get-DevConfigValue $cfg 'host')
    $user     = [string](Get-DevConfigValue $cfg 'user')
    $port     = Get-DevConfigValue $cfg 'port'
    $base     = [string](Get-DevConfigValue $cfg 'remoteBaseDirectory')
    $sshAlias = [string](Get-DevConfigValue $cfg 'sshAlias')
    $identityFile = [string](Get-DevConfigValue $cfg 'identityFile')

    if ([string]::IsNullOrWhiteSpace($hostName)) { throw "Missing required field 'host'." }
    if ([string]::IsNullOrWhiteSpace($user)) { throw "Missing required field 'user'." }
    if ($null -eq $port -or "$port" -eq '') { throw "Missing required field 'port'." }
    if ([string]::IsNullOrWhiteSpace($base)) { throw "Missing required field 'remoteBaseDirectory'." }

    if ($hostName -notmatch '^[A-Za-z0-9._\-:\[\]]+$') { throw ('Invalid ''host'' value: {0}' -f $hostName) }
    if ($user -notmatch '^[A-Za-z0-9._\-]+$') { throw ('Invalid ''user'' value: {0}' -f $user) }
    $portInt = 0
    if (-not [int]::TryParse([string]$port, [ref]$portInt) -or $portInt -lt 1 -or $portInt -gt 65535) {
        throw ('Invalid ''port'' value ''{0}'': must be an integer 1..65535.' -f $port)
    }
    if ($base -notmatch '^/' -or $base -match "['`r`n]") {
        throw "Invalid 'remoteBaseDirectory': must be an absolute POSIX path starting with '/', no quotes or newlines."
    }
    if (-not [string]::IsNullOrWhiteSpace($sshAlias)) {
        if ($sshAlias -notmatch '^[A-Za-z0-9._\-]+$') { throw ('Invalid ''sshAlias'' value: {0}' -f $sshAlias) }
    } else { $sshAlias = '' }
    if (-not [string]::IsNullOrWhiteSpace($identityFile)) {
        if ($identityFile -match "['`r`n]" -or $identityFile -match 'PRIVATE KEY|BEGIN ') {
            throw "Invalid 'identityFile': must be a local path reference (string), never inline private key material."
        }
        $repoRoot = Get-DevRepoRoot
        if (-not $identityFile.StartsWith('~', [System.StringComparison]::Ordinal)) {
            try {
                $full = [System.IO.Path]::GetFullPath($identityFile)
                if ($full.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                    throw ('identityFile must not point inside the repository ({0}). Private keys never belong in the repo.' -f $repoRoot)
                }
            } catch {
                # un-resolvable relative path: leave as-is
            }
        }
    }

    return [PSCustomObject]@{
        host                = $hostName
        user                = $user
        port                = $portInt
        remoteBaseDirectory = $base
        sshAlias            = $sshAlias
        identityFile        = $identityFile
    }
}

function Invoke-DevRemoteProbe {
    param([Parameter(Mandatory = $true)]$Config)

    if (-not [string]::IsNullOrWhiteSpace([string]$Config.identityFile) -and -not ([string]$Config.identityFile).StartsWith('~', [System.StringComparison]::Ordinal)) {
        if (-not (Test-Path -LiteralPath ([string]$Config.identityFile))) {
            Write-DevWarn ('identityFile ''{0}'' not found locally — ssh may fail until you fix it.' -f $Config.identityFile)
        }
    }

    $sshArgs = @('-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15')
    $target = ''
    if (-not [string]::IsNullOrWhiteSpace([string]$Config.sshAlias)) {
        $target = [string]$Config.sshAlias
    } else {
        $sshArgs += @('-p', [string]$Config.port)
        $target = '{0}@{1}' -f $Config.user, $Config.host
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$Config.identityFile)) {
        $sshArgs += @('-i', [string]$Config.identityFile)
    }

    $base = [string]$Config.remoteBaseDirectory
    $probe = "hostname; echo '---'; uname -a 2>/dev/null || echo 'uname: UNAVAILABLE'; echo '---'; (python3 --version 2>&1 || python --version 2>&1 || echo 'python: NOT_FOUND'); echo '---'; (nvidia-smi --query-gpu=name,memory.total --format=csv,noheader 2>&1 || echo 'nvidia-smi: NOT_FOUND'); echo '---'; if [ -d '{0}' ]; then echo 'BASE_OK'; else echo 'BASE_MISSING'; fi" -f $base

    Write-DevInfo ('Running read-only remote diagnostics via ssh ({0}) ...' -f $target)
    $output = @(& ssh $sshArgs $target $probe 2>&1 | ForEach-Object { "$_" })
    $rc = $LASTEXITCODE
    if ($output.Count -gt 0) {
        $output | ForEach-Object { Write-Host ('    {0}' -f $_) }
    }
    if ($rc -ne 0) {
        throw ('ssh remote probe failed (exit code {0}). Check reachability, keys, sshAlias/user/host.' -f $rc)
    }
    $joined = $output -join "`n"
    if ($joined -match 'BASE_MISSING') {
        throw ('remoteBaseDirectory ''{0}'' does not exist on the remote host.' -f $base)
    }
    Write-DevOk ('Remote base directory present: {0}' -f $base)
}

# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------
function Write-DevHeader { param([string]$Text) Write-Host ("`n== {0} ==" -f $Text) -ForegroundColor Cyan }
function Write-DevInfo  { param([string]$Text) Write-Host ('[dev] {0}' -f $Text) }
function Write-DevOk    { param([string]$Text) Write-Host ('[ok  ] {0}' -f $Text) -ForegroundColor Green }
function Write-DevWarn  { param([string]$Text) Write-Host ('[warn] {0}' -f $Text) -ForegroundColor Yellow }
function Write-DevFail  { param([string]$Text) Write-Host ('[fail] {0}' -f $Text) -ForegroundColor Red }

Export-ModuleMember -Function Invoke-DevDoctor, Invoke-DevBuild, Invoke-DevTest, Invoke-DevVerify, Invoke-DevRemote, Invoke-DevModel
