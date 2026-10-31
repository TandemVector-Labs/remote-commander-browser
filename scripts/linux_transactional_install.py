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
import signal
import stat
import subprocess
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


def sync_dir(path: Path):
    """Persist directory entries, including symlink renames on POSIX filesystems."""
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def sync_payload(root: Path):
    """Flush every staged regular file, then directories bottom-up before activation."""
    dirs = []
    for location, _, filenames in os.walk(root, followlinks=False):
        directory = Path(location)
        dirs.append(directory)
        for name in filenames:
            path = directory / name
            if path.is_symlink():
                continue
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
    for directory in reversed(dirs):
        sync_dir(directory)


def qa_canary_check(root: Path, dest: Path, swap_id: str, seconds: int):
    """Owner-invoked offline synthetic canary; never uses a primary browser profile."""
    ensure(1 <= seconds <= 15, "QA_CANARY_TIMEOUT_OUT_OF_RANGE")
    profile = root / (".qa-canary-" + swap_id)
    ensure(not os.path.lexists(profile), "QA_CANARY_PROFILE_ALREADY_EXISTS")
    profile.mkdir(mode=0o700)
    env = {"PATH": "/usr/bin:/bin", "HOME": str(profile), "LANG": "C",
           "XDG_CONFIG_HOME": str(profile / "config"),
           "XDG_CACHE_HOME": str(profile / "cache"),
           "XDG_DATA_HOME": str(profile / "data")}
    cmd = [str(dest / "launch.sh"), "--background-test", "--self-test",
           "--shutdown-self-test", "--host-resolver-rules=MAP * ~NOTFOUND",
           "--cgwa-profile-dir=" + str(profile)]
    child = None
    try:
        child = subprocess.Popen(cmd, cwd=profile, env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            start_new_session=True)
        try:
            _, errors = child.communicate(timeout=seconds)
        except subprocess.TimeoutExpired as exc:
            os.killpg(child.pid, signal.SIGKILL)
            child.communicate(timeout=5)
            raise InstallError("QA_CANARY_TIMEOUT") from exc
        ensure(child.returncode == 0, "QA_CANARY_FAILED_EXIT")
    finally:
        if child is not None and child.poll() is None:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait(timeout=5)
        shutil.rmtree(profile)
        sync_dir(root)


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



def owned_version_directory(root: Path, target: str) -> bool:
    """Recognize an installer-created local version; not publisher authentication."""
    match = re.fullmatch(
        r"v([0-9]+\.[0-9]+\.[0-9]+(?:-rc\.[0-9]+)?)-([0-9a-f]{12})", target)
    if not match:
        return False
    directory = root / target
    if (directory.is_symlink() or not directory.is_dir() or
            directory.resolve().parent != root.resolve()):
        return False
    for name in ("chatgpt-cef-v2", "launch.sh", "product-install.json"):
        entry = directory / name
        if entry.is_symlink() or not entry.is_file():
            return False
    metadata = directory / "product-install.json"
    if metadata.stat().st_size > 4096:
        return False
    try:
        info = json.loads(metadata.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, ValueError):
        return False
    if not isinstance(info, dict):
        return False
    digest = info.get("manifestSha256")
    return (type(info.get("schema")) is int and info["schema"] == 1 and
            info.get("version") == match.group(1) and
            info.get("automaticUpdate") is False and isinstance(digest, str) and
            re.fullmatch(r"[0-9a-f]{64}", digest) is not None and
            digest.startswith(match.group(2)))


def install(args):
    source = Path(args.source).resolve(strict=True)
    root = Path(os.path.abspath(args.install_root))
    helper = Path(args.trusted_sandbox).resolve(strict=True)
    ensure(re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-rc\.[0-9]+)?", args.version) is not None,
           "INVALID_VERSION")
    real_root = root.resolve(strict=False)
    ensure(source.is_dir() and source != real_root and
           not real_root.is_relative_to(source), "INVALID_SOURCE_ROOT")
    ensure((source / "chatgpt-cef-v2").is_file() and
           os.access(source / "chatgpt-cef-v2", os.X_OK), "BROWSER_EXE_MISSING")
    ensure((source / "chrome-sandbox").is_file(), "BROWSER_SANDBOX_MISSING")
    ensure(sha256(source / "chrome-sandbox") == sha256(helper),
           "SANDBOX_PAYLOAD_HASH_MISMATCH")
    hs = helper.stat()
    ensure(hs.st_uid == 0 and stat.S_IMODE(hs.st_mode) == 0o4755 and
           stat.S_ISREG(hs.st_mode), "SANDBOX_TRUST_ROOT_MISSING")
    ensure(not root.is_symlink(), "SYMLINK_INSTALL_ROOT_REJECTED")
    # Resolve every parent before any mkdir: a lexical path under the QA root
    # could otherwise follow an ancestor symlink into a production directory.
    fixture = (Path(__file__).resolve().parents[1] /
               "test" / "linux-swap-fixture-r1").resolve(strict=False)
    if args.fault != "None":
        ensure(real_root.is_relative_to(fixture) and real_root != fixture,
               "PUBLIC_FAULT_INJECTION_FORBIDDEN")
    if getattr(args, "qa_canary", False):
        ensure(real_root.is_relative_to(fixture) and real_root != fixture,
               "PUBLIC_QA_CANARY_FORBIDDEN")
    if not root.exists():
        root.mkdir(mode=0o700, parents=True)
    ensure(root.is_dir() and root.stat().st_uid == os.getuid() and
           not(root.stat().st_mode & 0o022), "UNSAFE_INSTALL_ROOT")
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
                    ensure(owned_version_directory(root, target),
                           "INTERRUPTED_SWAP_INVALID_TARGET")
                recovered.replace(current)
                sync_dir(root)
        previous = None
        if os.path.lexists(current):
            ensure(current.is_symlink(), "CURRENT_NOT_SYMLINK")
            target = os.readlink(current)
            ensure(owned_version_directory(root, target),
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
            sync_payload(stage)
            stage.replace(dest)
            sync_dir(root)
        except Exception:
            # Leave diagnostics for inspection, never change current.
            raise
        pending.symlink_to(stem)
        sync_dir(root)
        moved = False
        try:
            if previous:
                # Preserve the previous pointer as a separate validated alias;
                # never remove the live current link before the atomic replacement.
                rollback.symlink_to(previous)
                sync_dir(root)
                moved = True
            if args.fault == "AfterBaselineMove":
                raise InstallError("QA_FAULT_AFTER_BASELINE_MOVE")
            pending.replace(current)
            sync_dir(root)
            if args.fault == "AfterCandidateMove":
                raise InstallError("QA_FAULT_AFTER_CANDIDATE_MOVE")
            if getattr(args, "qa_canary", False):
                qa_canary_check(root, dest, swap_id,
                    int(getattr(args, "qa_canary_timeout", 5)))
            ensure(current.is_symlink() and os.readlink(current) == stem,
                   "CURRENT_PROMOTION_VERIFY_FAILED")
        except Exception as exc:
            if moved:
                # Rollback must be one atomic replacement as well. Never move
                # current away first: that would expose a missing-current gap.
                rollback.replace(current)
                sync_dir(root)
            elif os.path.lexists(current) and current.is_symlink() and os.readlink(current) == stem:
                # Initial installation has no prior version to restore.
                current.replace(failed)
                sync_dir(root)
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
    p.add_argument("--qa-canary", action="store_true", help="QA fixture only; never enables updates")
    p.add_argument("--qa-canary-timeout", type=int, default=5)
    args = p.parse_args()
    try:
        print(json.dumps(install(args), sort_keys=True))
    except (InstallError, OSError, ValueError) as e:
        print("BROWSER_LINUX_TXN_REFUSED:" + str(e), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
