#!/usr/bin/env python3
"""Linux Browser *manual* atomic version swap. OFFLINE, no automatic update.

No download, credential, GUI, root escalation or mutable in-place upgrade.
A release update is NOT trusted merely because this script accepts a source dir:
the publisher trust/TUF gate belongs upstream and remains CLOSED.
"""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import sys
from datetime import datetime, timezone


class InstallError(RuntimeError):
    pass


def ensure(condition: bool, code: str):
    if not condition:
        raise InstallError(code)


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def contents(src: Path, helper: Path):
    entries = []
    for p in sorted(src.rglob("*")):
        rel = p.relative_to(src).as_posix()
        ensure(not p.is_symlink() or (rel == "chrome-sandbox" and
                                     p.resolve(strict=True) == helper),
               "PAYLOAD_SYMLINK_REFUSED:" + rel)
        if p.is_file():
            ensure(p.stat().st_size < 4_000_000_000, "PAYLOAD_FILE_TOO_LARGE")
            entries.append(rel + "|" + sha256(p))
        else:
            ensure(p.is_dir(), "PAYLOAD_UNEXPECTED_ENTRY")
    return entries


def install(args):
    source = Path(args.source).resolve(strict=True)
    root = Path(os.path.abspath(args.install_root))
    helper = Path(args.trusted_sandbox).resolve(strict=True)
    ensure(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-rc\.[0-9]+)?", args.version) is not None,
           "INVALID_VERSION")
    ensure(source.is_dir() and source != root and not root.is_relative_to(source),
           "INVALID_SOURCE_ROOT")
    ensure((source / "chatgpt-cef-v2").is_file() and
           os.access(source / "chatgpt-cef-v2", os.X_OK), "BROWSER_EXE_MISSING")
    ensure((source / "chrome-sandbox").is_file(), "BROWSER_SANDBOX_MISSING")
    ensure(sha256(source / "chrome-sandbox") == sha256(helper),
           "SANDBOX_PAYLOAD_HASH_MISMATCH")
    hs = helper.stat()
    ensure(hs.st_uid == 0 and stat.S_IMODE(hs.st_mode) == 0o4755 and
           stat.S_ISREG(hs.st_mode), "SANDBOX_TRUST_ROOT_MISSING")
    ensure(not root.is_symlink(), "SYMLINK_INSTALL_ROOT_REJECTED")
    if not root.exists():
        root.mkdir(mode=0o700, parents=True)
    ensure(root.is_dir() and root.stat().st_uid == os.getuid() and
           not(root.stat().st_mode & 0o022), "UNSAFE_INSTALL_ROOT")
    # Only QA fixtures may inject failures; no production use of this mechanism.
    if args.fault != "None":
        fixture = Path(__file__).resolve().parents[1] / "test" / "linux-swap-fixture-r1"
        ensure(root.is_relative_to(fixture) and root != fixture,
               "PUBLIC_FAULT_INJECTION_FORBIDDEN")
    lock_path = root / ".swap.lock"
    ensure(not lock_path.is_symlink(), "LOCK_SYMLINK_REFUSED")
    lock_fd = os.open(lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(lock_fd, "a+b") as lock:
        lock_stat = os.fstat(lock.fileno())
        ensure(stat.S_ISREG(lock_stat.st_mode) and lock_stat.st_uid == os.getuid() and
               (lock_stat.st_mode & 0o077) == 0, "UNSAFE_INSTALL_LOCK")
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise InstallError("CONCURRENT_INSTALL_LOCKED") from exc
        current = root / "current"
        # Recover only a precisely identifiable interrupted old-pointer rename.
        # A normal successful swap has no pending marker; ambiguous or forged
        # markers fail closed instead of guessing which version is trusted.
        if not os.path.lexists(current):
            incomplete = sorted(root.glob("current.pending-*"))
            if incomplete:
                ensure(len(incomplete) == 1, "INTERRUPTED_SWAP_AMBIGUOUS")
                pending_marker = incomplete[0]
                suffix = pending_marker.name.removeprefix("current.pending-")
                recovered = root / ("current.rollback-" + suffix)
                ensure(pending_marker.is_symlink() and recovered.is_symlink(),
                       "INTERRUPTED_SWAP_NO_RECOVERABLE_BASELINE")
                pending_target = os.readlink(pending_marker)
                old_target = os.readlink(recovered)
                for target in (pending_target, old_target):
                    ensure(target == Path(target).name and target.startswith("v") and
                           (root / target).is_dir() and not (root / target).is_symlink() and
                           (root / target).resolve().parent == root.resolve(),
                           "INTERRUPTED_SWAP_INVALID_TARGET")
                recovered.replace(current)
        previous = None
        if os.path.lexists(current):
            ensure(current.is_symlink(), "CURRENT_NOT_SYMLINK")
            target = os.readlink(current)
            ensure(target == Path(target).name and target.startswith("v") and
                   (root / target).is_dir() and
                   (root / target).resolve().parent == root.resolve(),
                   "CURRENT_TARGET_NOT_OWNED")
            previous = target
        entries = contents(source, helper)
        manifest_hash = hashlib.sha256("\n".join(entries).encode()).hexdigest()
        stem = "v" + args.version + "-" + manifest_hash[:12]
        dest = root / stem
        stage = root / (".stage-" + stem)
        swap_id = hashlib.sha256((args.version + "\0" + manifest_hash).encode()).hexdigest()[:12]
        pending = root / ("current.pending-" + swap_id)
        rollback = root / ("current.rollback-" + swap_id)
        failed = root / ("current.failed-" + swap_id)
        for p in (dest, stage, pending, rollback, failed):
            ensure(not os.path.lexists(p), "VERSION_OR_SWAP_PATH_ALREADY_EXISTS")
        stage.mkdir(mode=0o700)
        try:
            for part in source.iterdir():
                if part.name == "chrome-sandbox":
                    (stage / "chrome-sandbox").symlink_to(helper)
                elif part.is_file() and not part.is_symlink():
                    shutil.copy2(part, stage / part.name)
                elif part.is_dir() and not part.is_symlink():
                    shutil.copytree(part, stage / part.name, symlinks=False)
                else:
                    raise InstallError("PAYLOAD_BAD_TOP_LEVEL_TYPE")
            ensure(contents(stage, helper) == entries, "PAYLOAD_COPY_HASH_MISMATCH")
            (stage / "product-install.json").write_text(
                json.dumps({"schema": 1, "version": args.version,
                            "manifestSha256": manifest_hash,
                            "installedAt": datetime.now(timezone.utc).isoformat(),
                            "automaticUpdate": False}, sort_keys=True) + "\n")
            exe_hash = sha256(stage / "chatgpt-cef-v2")
            helper_hash = sha256(helper)
            launcher = stage / "launch.sh"
            launcher.write_text(
                "#!/bin/sh\nset -eu\n"
                "BIN=$(dirname \"$(realpath \"$0\")\")/chatgpt-cef-v2\n"
                "SANDBOX=$(dirname \"$(realpath \"$0\")\")/chrome-sandbox\n"
                f"test \"$(sha256sum \"$BIN\" | cut -d' ' -f1)\" = {shlex.quote(exe_hash)} || exit 23\n"
                f"test \"$(sha256sum \"$SANDBOX\" | cut -d' ' -f1)\" = {shlex.quote(helper_hash)} || exit 24\n"
                "test \"$(stat -Lc '%u:%a' \"$SANDBOX\")\" = 0:4755 || exit 25\n"
                "exec \"$BIN\" \"$@\"\n")
            launcher.chmod(0o700)
            stage.replace(dest)
        except Exception:
            # Leave diagnostics for inspection, never change current.
            raise
        pending.symlink_to(stem)
        moved = False
        try:
            if previous:
                # Preserve the previous pointer as a separate validated alias;
                # never remove the live current link before the atomic replacement.
                rollback.symlink_to(previous)
                moved = True
            if args.fault == "AfterBaselineMove":
                raise InstallError("QA_FAULT_AFTER_BASELINE_MOVE")
            pending.replace(current)
            if args.fault == "AfterCandidateMove":
                raise InstallError("QA_FAULT_AFTER_CANDIDATE_MOVE")
            ensure(current.is_symlink() and os.readlink(current) == stem,
                   "CURRENT_PROMOTION_VERIFY_FAILED")
        except Exception as exc:
            if os.path.lexists(current) and current.is_symlink() and os.readlink(current) == stem:
                current.replace(failed)
            if moved:
                rollback.replace(current)
            ensure(previous is None or
                   (current.is_symlink() and os.readlink(current) == previous),
                   "ROLLBACK_UNRESOLVED")
            raise InstallError("SWAP_ABORTED_AND_BASELINE_PRESERVED:" + str(exc)) from exc
        return {"status": "PASS", "version": args.version,
                "current": stem, "previous": previous, "manifestSha256": manifest_hash,
                "browserSha256": exe_hash, "automaticUpdate": False}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--source", required=True)
    p.add_argument("--install-root", required=True)
    p.add_argument("--trusted-sandbox", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--fault", choices=["None", "AfterBaselineMove", "AfterCandidateMove"],
                   default="None")
    args = p.parse_args()
    try:
        print(json.dumps(install(args), sort_keys=True))
    except (InstallError, OSError, ValueError) as e:
        print("BROWSER_LINUX_TXN_REFUSED:" + str(e), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
