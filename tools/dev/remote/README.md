# Choirloom — Remote Development

This document describes the remote workflow and the deliberately limited
automation currently exposed by the dev control plane (`dev.ps1 remote ...`).
The repository does **not** have a server-side runner yet, so the job verbs
fail with an explicit `NotImplemented:` message and a prerequisite. Nothing
here assumes a live runner protocol, and the control plane **never fakes
success**.

## Current capability matrix

| Command                     | State     | Notes |
|-----------------------------|-----------|-------|
| `dev.ps1 remote setup`      | Implemented (interactive) | Interactive wizard, interactive-only, entry gated by `-Confirm`. Collects non-secret fields into `.dev/remote.local.json`; optional, explicitly-confirmed local `ssh-keygen`; shows public key + suggested ssh config stanza but **never writes SSH config**, never copies/transfers a private key, never runs a remote probe. |
| `dev.ps1 remote doctor`     | Implemented | Read-only. Requires `.dev/remote.local.json` + OpenSSH client. |
| `dev.ps1 remote status`     | `NotImplemented` | Needs a server-side runner protocol (job state query). |
| `dev.ps1 remote logs`       | `NotImplemented` | Needs a server-side runner protocol (log retrieval). |
| `dev.ps1 remote fetch-report` | `NotImplemented` | Needs a runner protocol (result/report retrieval). |
| `dev.ps1 remote submit`     | `NotImplemented` | Confirmation-gated; needs manifest schema + runner. |
| `dev.ps1 remote cancel`     | `NotImplemented` | Confirmation-gated; needs runner job control. |
| `dev.ps1 remote fetch-model`| `NotImplemented` | Confirmation-gated; needs ModelPackage distribution. |

Every unbacked operation exits non-zero with `NotImplemented:` and the
prerequisite to satisfy first.

## Configuration

- `.dev/remote.example.json` — committed template, **no secrets**.
- `.dev/remote.local.json` — your local copy, git-ignored, **never committed**.

Fields (all validated by the control plane; unknown fields and anything
secret-shaped are rejected):

| Field                 | Required | Description |
|-----------------------|----------|-------------|
| `host`                | yes      | Remote hostname or IP. |
| `user`                | yes      | SSH login user. |
| `port`                | yes      | SSH port (1–65535). |
| `remoteBaseDirectory` | yes      | Absolute POSIX path on the host where the runner workspace lives. |
| `sshAlias`            | optional | Use an existing `~/.ssh/config` alias instead of `user@host`. |
| `identityFile`        | optional | Path to a local private key file. **Reference only** — a path string, never inline key material. May be empty (default SSH agent/key). |

Security rules: no passwords/tokens/secrets anywhere in the config; private key
material is never committed and never appears inline in any file in this repo;
`*.key` / `*.pem` are git-ignored while `*.pub` is not; **the private key never
leaves the local machine**.

## SSH setup (manual, interactive)

The control plane never copies or transfers your private key and never edits
SSH config. Setup is limited to local configuration; the server side is yours.

1. **Verify the OpenSSH client**: the wizard checks `ssh` and `ssh-keygen`
   (`ssh -V`, `ssh-keygen`).
2. **Trust the host fingerprint safely**: do **not** rely on unverified
   `ssh-keyscan` output. Obtain the host fingerprint over a trusted channel
   (server console, your administrator, or an existing verified connection),
   and only then accept it on first connect — or pin it by comparing the shown
   fingerprint against the one printed by `ssh-keygen -lf` on the server.
3. **Create or reuse a key pair**: the wizard can generate one at
   `%USERPROFILE%\.ssh\choral-score-opencode-ed25519`. It is **never
   overwritten** if it already exists. A passphrase is preferred; choosing an
   explicitly empty passphrase requires a separate confirmation.
4. **Install the public key manually**: connect to the server with your
   existing, already-working login method (for example your current password or
   admin session) and append the **public** key (`.pub`) to
   `~/.ssh/authorized_keys` yourself. Do not use unverified
   `ssh-copy-id`-style automation and never send the private key anywhere.
5. **Optionally add an ssh config alias**: edit `%USERPROFILE%\.ssh\config`
   yourself; the wizard only prints a suggested stanza and never writes it.
6. **Run the wizard**: `.\dev.ps1 remote setup` (requires `-Confirm`, runs only
   in an interactive terminal) → fields → optional key generation → summary →
   type `yes` to write `.dev/remote.local.json` (git-ignored, no secrets).
7. **Verify read-only**: after the public key is installed,
   `.\dev.ps1 remote doctor` runs fixed read-only probes only (`hostname`,
   `uname`, `python --version`, `nvidia-smi`, remote base directory access).

Interactive confirmation: `setup` / `submit` / `cancel` / `fetch-model` are
confirmation-gated operations. In an interactive terminal you will be asked to
confirm; in a non-interactive shell you must pass `-Confirm` explicitly, or the
command is refused. `setup` is interactive-only even with `-Confirm`.

## Remote job execution contract (once the runner exists)

- **Exact clean Git SHA**: every remote job is pinned to the exact 40-character
  commit SHA, e.g. `git rev-parse HEAD`. Remote runs happen from a **clean
  checkout** of that exact SHA — never a branch tip, never a dirty worktree.
  `submit` will reject anything else.
- **Manifest fields** (`submit` will require a manifest; schema not yet fixed,
  intended fields):
  - `schemaVersion`
  - `task` — `train` | `benchmark` | `evaluate`
  - `repoSha` — exact 40-character git SHA (see above)
  - `modelPath` / `datasetPath` — references, resolved only against
    `remoteBaseDirectory`
  - `entrypoint` — fixed, allow-listed runner command only
  - `timeoutSeconds`
  - `createdBy` — short identifier, no secrets
- **No raw server code edits**: never use ad-hoc `ssh` to mutate the runner or
  hand-edit files on the host. All state changes go through reviewed
  automation; the control plane only ever issues **fixed, read-only** probes
  (`remote doctor`). Anything else is `NotImplemented` until the runner
  protocol is defined.
- **ModelPackage**: fetched models must be declarative packages
  (manifest + checksum; no arbitrary code execution from repositories), per
  `spec/v0.1` ModelPackage rules.

## Permission tiers & promotion policy

- **Tier 0 — read-only**: `doctor`, `status`, `logs`, `fetch-report` queries.
  Fixed safe commands only; nothing may write to the host.
- **Tier 1 — job control (confirmation-gated)**: `setup`, `submit`, `cancel`,
  `fetch-model`. Refused in non-interactive shells unless `-Confirm` is passed.
  `setup` also remains interactive-only even with `-Confirm`.
- **Tier 2 — mutating runner state**: not exposed at all.

Promotion workflow: an operation may move up a tier **only** when the runner
protocol, manifest schema, ModelPackage rules, and a review gate exist
(development plan M0/M4, `spec/v0.1/03-development-plan.md`). Promotion is
reviewed via ADR where it changes a frozen decision; it is never a silent
change in code.
