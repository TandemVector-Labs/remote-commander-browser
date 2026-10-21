# Browser Linux version-swap/rollback candidate — 21 October 2026

## Scope / non-goals
This branch adds `scripts/linux_transactional_install.py` as an **explicit manual,
offline** Linux version-swap installer, not an automatic or production updater.
The source directory is not authenticated as a publisher release by this
script; the caller must independently verify the release through a trusted
publisher metadata channel. **No new trusted root or TUF role keys have
been provisioned. Keep any automatic updater DISABLED.**

The command requires `--source`, `--install-root`, `--version` and
`--trusted-sandbox`, and does not request sudo. The preapproved root-owned
SUID sandbox must be exactly mode 4755 and SHA-256-equal to the source payload.
No `--no-sandbox` or browser extension fallback is provided.

## Properties under test
- Always deploys into a new immutable version directory, verifying the copied
  file-by-file payload hashes before promotion.
- Exclusive `flock`; restricted-owner install directory; refuses unknown
  current pointers, arbitrary payload symlinks, traversal/malformed versions
  and same-version overwrites.
- Current remains a valid local relative symlink throughout promotion:
  prior target is first copied as `current.rollback-...`, then the staged
  pending symlink atomically replaces `current` (no empty-current window).
  On failure, the previous alias atomically replaces `current` again.
- Fault injection is restricted to the explicitly named test fixture root.
  Both interruption positions restore the original current symlink.
- Restart-time recovery now recognizes a uniquely paired `current.pending-*`/
  `current.rollback-*` marker from the legacy two-rename sequence, validates both target versions as local directories, and
  restores the previous `current`. Ambiguous markers, forged external
  rollback targets and incomplete first installs fail closed. This
  covers a process interruption at the simulated swap boundary, **not**
  verified power-loss durability; parent-directory `fsync` and live
  interruption testing remain separate acceptance requirements.
- Separate `launch.sh` verifies executable and root-owned SUID sandbox SHA
  and mode on every invocation; no profile/credentials are copied.
- Key material, accounts, production routing, ChatGPT conversations and
  installed owner browser are deliberately not touched.

## Verified source QA
Base browser Git commit: `10e5bc57ae4301b009df9736483c62c1ebad3695`.
- Unit/negative tests: **15 PASS / 0 FAIL** on MMZ Linux, 21 October 2026.
- Real offline CEF build payload (1.5 GiB): isolated staged install PASS;
  browser SHA256
  `4d3f2b6a5389a9a2f2ae82f1f09e940c47eb29cd2e7a88504473fdcb80e3e08d`;
  receipt SHA256
  `b2a3798edc279f61d0c951001f5283a2b5ed9b0010f8acd50bf8afdd7eb24460`;
  operation `65b63e0c-c6c3-44c6-bb72-fd01b4d31c4d`.
- GUI run **via the installed `current/launch.sh`**, isolated X11/CDP:
  initial 1 tab, create 16, reject 17th, close to 15, reopen to 16;
  operation `2ad44f93-6d38-4c74-97bc-50e458b5615e`, exit 0.
  No real login. Temporary X server cleaned.

## Explicit blockers for automatic deployment
Before *any* auto-update needs an out-of-band pretrusted publisher root,
valid signatures with separately delegated targets/snapshot/timestamp metadata
and threshold rules, expiry/freshness and anti-rollback checks. TUF provides
a maintained specification; implementing only package SHA validation is **not**
a TUF update client. Also require live-user opt-in/canary, auth/project binding,
action authorization, full rollback on installed target, and independent review.
Do not enable automatic install, silently change production symlinks or promote
release while these gates are open.

References: https://theupdateframework.io/docs/metadata/ and
https://theupdateframework.github.io/specification/v1.0.36/ .
