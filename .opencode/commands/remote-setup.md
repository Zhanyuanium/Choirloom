---
description: Configure the remote runner/executor path with explicit user opt-in and no silent uploads.
agent: orchestrator
---

Set up the remote runner/executor path for: $ARGUMENTS

Use the interactive wizard `.\dev.ps1 remote setup` (interactive-only; requires
`-Confirm` to enter, and every step still asks for confirmation). Current setup
is limited to **local configuration** — it does not install or configure the
server. The intended seven-step flow:

1. **OpenSSH check**: the wizard checks `ssh` and `ssh-keygen` availability and
   reports the result (setup can still write config if either is missing).
2. **Ask fields**: hostname/IP, SSH user, port (default 22), remote project
   base directory (absolute POSIX path), optional SSH alias — all validated
   with the module's strict rules; no secrets are requested.
3. **Key (optional, never overwritten)**: if the suggested key
   `%USERPROFILE%\.ssh\choral-score-opencode-ed25519` does not exist, ask
   whether to generate; default is NO and only an explicit `yes` runs
   `ssh-keygen`. Passphrase is preferred; an explicitly empty passphrase needs
   a separate confirmation. An existing key is never overwritten.
4. **Public key / authorized_keys (manual)**: show the public key path and
   content; the user must append it to `~/.ssh/authorized_keys` on the server
   using an existing, already-working login method. Never copy the private key,
   never run `ssh-copy-id`.
5. **SSH config stanza (never auto-written)**: show a suggested `Host` stanza;
   the user edits `%USERPROFILE%\.ssh\config` themselves. The wizard never
   writes SSH config.
6. **Save config**: show a summary, then require the user to type `yes` before
   writing `.dev/remote.local.json` (git-ignored, no secrets). Declining writes
   nothing.
7. **Post-setup**: tell the user to install the public key on the server, then
   run `.\dev.ps1 remote doctor` (read-only). No driver/CUDA installs and no
   remote probe run during setup.

Rules (strict — do not skip):

1. Remote execution is always an explicit user choice (decisions A12/A15, `spec/v0.1/02 §17`). Confirm with the user before enabling anything, and record the confirmation.
2. Never silently upload source scores, project DB, corrections, or lyrics. Default is fully offline/local.
3. The executor-side protocol is future (`spec/development-workflow.md §5`): `remote submit|status|logs|cancel|fetch-report|fetch-model` are stable `NotImplemented` contracts until a runner exists. Do not present them as working.
4. No arbitrary code execution from model packages or remote artifacts (decision A8).
5. Host settings live only in `.dev/remote.local.json` (git-ignored, no secrets). The private key never leaves the local machine and never appears in the repo.
6. All project operations go through `.\dev.ps1`.
7. After setup, verify the local path still works offline and report the explicit confirmation(s) you obtained.
