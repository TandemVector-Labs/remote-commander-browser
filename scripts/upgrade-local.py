#!/usr/bin/env python3
"""Fail-closed activation of an ALREADY STAGED, distinct-version Linux CEF runtime.

This does NOT create a privileged sandbox, sign a package, delete a runtime,
inspect a profile, or authorize a release. The owner-approved package must be
pre-staged at RUNTIME/.approved-release.zip with an independently verified SHA. Release owner must verify the source
of the expected digests and stage a correct root-owned SUID helper separately.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import shutil
import stat
import subprocess
import sys

VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z][0-9A-Za-z.-]*)?$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
HEX40 = re.compile(r"^[0-9a-f]{40}$")
STATE = ".upgrade-rollback"
LOCK = ".upgrade.lock"


class UpgradeError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise UpgradeError(message)


def digest_bytes(value):
    return hashlib.sha256(value).hexdigest()


def digest_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as stream:
        while chunk := stream.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def sync_dir(path):
    fd = os.open(path, os.O_DIRECTORY | os.O_RDONLY | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def validate_dirs(home):
    require(home.is_absolute() and home == home.resolve(), "HOME must be a non-symlink absolute path")
    roots = [home, home / ".local", home / ".local/share",
             home / ".local/share/chatgpt-cef-v2",
             home / ".local/bin", home / ".local/share/applications"]
    for path in roots:
        if path.is_symlink():
            raise UpgradeError("symlink ancestor refused: " + str(path))
        if not path.exists():
            path.mkdir(mode=0o700)
        meta = path.lstat()
        require(stat.S_ISDIR(meta.st_mode) and meta.st_uid == os.getuid()
                and not (meta.st_mode & 0o022),
                "unsafe directory ownership/permissions: " + str(path))
    return roots[3]


def regular_or_absent(path, require_safe_mode=False):
    if path.is_symlink():
        raise UpgradeError("linked launcher/desktop refused")
    if not path.exists():
        return None
    meta = path.lstat()
    require(stat.S_ISREG(meta.st_mode) and meta.st_uid == os.getuid(),
            "unexpected existing launcher/desktop")
    require(not require_safe_mode or not (meta.st_mode & 0o022),
            "unsafe launcher/desktop permissions")
    return {"hash": digest_file(path), "mode": stat.S_IMODE(meta.st_mode)}


def validate_staged(app, version, binary_hash, sandbox_hash, package, package_hash, source_commit):
    require(bool(VERSION.fullmatch(version)), "invalid distinct version")
    require(bool(HEX64.fullmatch(binary_hash)) and bool(HEX64.fullmatch(sandbox_hash))
            and bool(HEX64.fullmatch(package_hash)) and bool(HEX40.fullmatch(source_commit)),
            "expected digests/source identity required")
    runtime = app / ("runtime-v" + version)
    require(runtime.is_dir() and not runtime.is_symlink(), "distinct staged runtime missing or linked")
    meta = runtime.lstat()
    require(meta.st_uid == os.getuid() and not (meta.st_mode & 0o022),
            "staged runtime must be user-owned and non-writable by other principals")
    require(runtime.resolve() == runtime, "runtime ancestor resolves outside trusted runtime")
    binary, sandbox = runtime / "chatgpt-cef-v2", runtime / "chrome-sandbox"
    for path in (binary, sandbox, package):
        require(not path.is_symlink() and path.is_file(), "linked/missing artifact: " + str(path))
    bstat, sstat = binary.lstat(), sandbox.lstat()
    require(stat.S_ISREG(bstat.st_mode) and bstat.st_uid == os.getuid()
            and bool(bstat.st_mode & 0o111)
            and not (bstat.st_mode & 0o022),
            "unsafe binary ownership or executability/permissions")
    require(stat.S_ISREG(sstat.st_mode) and sstat.st_uid == 0 and sstat.st_gid == 0
            and stat.S_IMODE(sstat.st_mode) == 0o4755, "SUID sandbox must be root:root 4755")
    pstat = package.lstat()
    require(stat.S_ISREG(pstat.st_mode)
            and pstat.st_uid in (0, os.getuid())
            and not (pstat.st_mode & 0o022),
            "unsafe staged package ownership/permissions")
    require(digest_file(binary) == binary_hash, "binary digest mismatch")
    require(digest_file(sandbox) == sandbox_hash, "sandbox digest mismatch")
    require(digest_file(package) == package_hash, "candidate package digest mismatch")
    return runtime


def rendered_files(home, runtime, binary_hash, sandbox_hash):
    quote = shlex.quote
    launcher = f"""#!/usr/bin/env bash
set -euo pipefail
umask 077
RUNTIME={quote(str(runtime))}
BIN="$RUNTIME/chatgpt-cef-v2"
SANDBOX="$RUNTIME/chrome-sandbox"
EXPECTED_BIN_SHA={quote(binary_hash)}
EXPECTED_SANDBOX_SHA={quote(sandbox_hash)}
fail(){{ printf 'Remote Commander Browser: %s\\n' "$1" >&2; exit "$2"; }}
[[ -x "$BIN" && -f "$BIN" && ! -L "$BIN" ]] || fail 'binary missing or linked' 20
[[ -f "$SANDBOX" && ! -L "$SANDBOX" ]] || fail 'sandbox missing or linked' 21
read -r mode owner group < <(stat -Lc '%a %U %G' "$SANDBOX")
[[ "$mode" == 4755 && "$owner" == root && "$group" == root ]] || fail 'unsafe sandbox' 22
[[ "$(sha256sum "$BIN" | awk '{{print $1}}')" == "$EXPECTED_BIN_SHA" ]] || fail 'binary SHA mismatch' 23
[[ "$(sha256sum "$SANDBOX" | awk '{{print $1}}')" == "$EXPECTED_SANDBOX_SHA" ]] || fail 'sandbox SHA mismatch' 24
cd "$RUNTIME"
exec "$BIN" "$@"
""".encode()
    launch_path = home / ".local/bin/chatgpt-cef-v2"
    # Desktop Exec uses freedesktop quoting, not shell evaluation.
    require(not any(c in str(launch_path) for c in '\n\r"\\\t '), "unsafe desktop Exec path")
    desktop = f"""[Desktop Entry]
Version=1.0
Type=Application
Name=Remote Commander Browser
Comment=Versioned native CEF runtime
Exec={launch_path}
TryExec={launch_path}
Icon=applications-internet
Terminal=false
Categories=Network;Utility;
StartupWMClass=ChatGPT-CEF-V2
StartupNotify=false
""".encode()
    subprocess.run(["bash", "-n"], input=launcher, check=True, capture_output=True)
    return {"launcher": (launch_path, launcher, 0o755),
            "desktop": (home / ".local/share/applications/chatgpt-cef-v2.desktop",
                        desktop, 0o644)}


def atomic_write(path, data, mode):
    temp = path.parent / ("." + path.name + ".staged-" + secrets.token_hex(8))
    fd = os.open(temp, os.O_CREAT | os.O_EXCL | os.O_WRONLY | os.O_NOFOLLOW, mode)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temp, mode)
        os.replace(temp, path)
        sync_dir(path.parent)
    finally:
        if temp.exists():
            temp.unlink()


def read_journal(app):
    state = app / STATE
    require(state.is_dir() and not state.is_symlink()
            and state.lstat().st_uid == os.getuid()
            and not (state.lstat().st_mode & 0o077), "no trusted rollback transaction")
    meta = state / "journal.json"
    require(meta.is_file() and not meta.is_symlink()
            and meta.lstat().st_uid == os.getuid(), "rollback metadata invalid")
    obj = json.loads(meta.read_text())
    require(obj.get("format") == 1 and set(obj["files"]) == {"launcher", "desktop"},
            "unrecognized rollback metadata")
    return state, obj


def rollback_locked(home, app, remove_state=True):
    state, record = read_journal(app)
    targets = {"launcher": home / ".local/bin/chatgpt-cef-v2",
               "desktop": home / ".local/share/applications/chatgpt-cef-v2.desktop"}
    # Compare-and-swap preflight both outputs BEFORE modifying either.
    for name, target in targets.items():
        existing = regular_or_absent(target)
        current = existing["hash"] if existing else None
        item = record["files"][name]
        require(current in (item["old_hash"], item["new_hash"]),
                "rollback conflict: " + name)
        backup = state / (name + ".old")
        require(item["old_hash"] is None or (
            backup.is_file() and not backup.is_symlink()
            and digest_file(backup) == item["old_hash"]), "corrupt backup: " + name)
    for name, target in targets.items():
        item = record["files"][name]
        if item["old_hash"] is None:
            if target.exists():
                target.unlink()
                sync_dir(target.parent)
        else:
            atomic_write(target, (state / (name + ".old")).read_bytes(), item["old_mode"])
    if remove_state:
        shutil.rmtree(state)
        sync_dir(app)


def with_lock(app):
    path = app / LOCK
    fd = os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    meta = os.fstat(fd)
    if not (stat.S_ISREG(meta.st_mode) and meta.st_uid == os.getuid()
            and not (meta.st_mode & 0o022)):
        os.close(fd)
        raise UpgradeError("unsafe upgrade lock ownership or permissions")
    fcntl.flock(fd, fcntl.LOCK_EX)
    return fd


def activate(home, version, binary_hash, sandbox_hash, package, package_hash,
             source_commit, fail_at=None):
    app = validate_dirs(home)
    fd = with_lock(app)
    try:
        state = app / STATE
        require(not state.exists() and not state.is_symlink(),
                "pending transaction: rollback/finalize first")
        runtime = validate_staged(app, version, binary_hash, sandbox_hash,
                                  package, package_hash, source_commit)
        outputs = rendered_files(home, runtime, binary_hash, sandbox_hash)
        old = {name: regular_or_absent(path, require_safe_mode=True)
               for name, (path, _, _) in outputs.items()}
        if old["launcher"] is not None:
            launcher_old = outputs["launcher"][0].read_text()
            require(("runtime-v" + version) not in launcher_old,
                    "refusing same-version activation")
        temp_state = app / (STATE + ".prepare-" + secrets.token_hex(8))
        temp_state.mkdir(mode=0o700)
        try:
            journal = {"format": 1, "version": version, "source_commit": source_commit,
                       "binary_sha256": binary_hash, "sandbox_sha256": sandbox_hash,
                       "package_sha256": package_hash, "files": {}}
            for name, (path, content, mode) in outputs.items():
                previous = old[name]
                if previous is not None:
                    backup = temp_state / (name + ".old")
                    with open(backup, "xb") as stream:
                        stream.write(path.read_bytes())
                        stream.flush()
                        os.fsync(stream.fileno())
                    os.chmod(backup, 0o600)
                journal["files"][name] = {
                    "old_hash": previous["hash"] if previous else None,
                    "old_mode": previous["mode"] if previous else None,
                    "new_hash": digest_bytes(content),
                    "new_mode": mode,
                }
            meta = temp_state / "journal.json"
            with open(meta, "x") as stream:
                json.dump(journal, stream, sort_keys=True)
                stream.flush()
                os.fsync(stream.fileno())
            sync_dir(temp_state)
            os.replace(temp_state, state)
            sync_dir(app)
        finally:
            if temp_state.exists():
                shutil.rmtree(temp_state)
        try:
            for name, (path, data, mode) in outputs.items():
                atomic_write(path, data, mode)
                if fail_at == name:
                    raise UpgradeError("simulated interrupted switch after " + name)
        except BaseException:
            rollback_locked(home, app)
            raise
        return runtime
    finally:
        os.close(fd)


def action(home, mode):
    app = validate_dirs(home)
    fd = with_lock(app)
    try:
        if mode == "rollback":
            rollback_locked(home, app)
            return
        state, record = read_journal(app)
        # Do not discard the only rollback receipt while executable, privileged
        # helper, or release package bytes/permissions have drifted since activation.
        required = ("version", "source_commit", "binary_sha256",
                    "sandbox_sha256", "package_sha256")
        require(all(isinstance(record.get(key), str) for key in required),
                "finalize missing staged artifact attestations")
        package = (app / ("runtime-v" + record["version"])
                   / ".approved-release.zip")
        validate_staged(app, record["version"], record["binary_sha256"],
                        record["sandbox_sha256"], package,
                        record["package_sha256"], record["source_commit"])
        for name, path in (("launcher", home / ".local/bin/chatgpt-cef-v2"),
                           ("desktop", home / ".local/share/applications/chatgpt-cef-v2.desktop")):
            cur = regular_or_absent(path)
            expected = record["files"][name]
            require(cur is not None and cur["hash"] == expected["new_hash"]
                    and cur["mode"] == expected.get("new_mode"),
                    "finalize conflict: " + name)
        # Finalization only removes rollback launcher/desktop backups; old runtime
        # directories are retained until independently approved cleanup.
        shutil.rmtree(state)
        sync_dir(app)
    finally:
        os.close(fd)


def main():
    p = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    modes = p.add_mutually_exclusive_group(required=True)
    modes.add_argument("--activate", action="store_true")
    modes.add_argument("--rollback", action="store_true")
    modes.add_argument("--finalize", action="store_true")
    p.add_argument("--version")
    p.add_argument("--binary-sha256")
    p.add_argument("--sandbox-sha256")
    p.add_argument("--package-sha256")
    p.add_argument("--source-commit")
    a = p.parse_args()
    try:
        require(os.geteuid() != 0, "activation must run unprivileged; provision SUID separately")
        home = Path.home()
        if a.activate:
            require(all((a.version, a.binary_sha256, a.sandbox_sha256,
                         a.package_sha256, a.source_commit)),
                    "activate requires distinct version/source and trusted exact digests")
            require(bool(VERSION.fullmatch(a.version)), "invalid version")
            # Deliberately NO CLI-controlled path: only a pre-staged package at
            # this fixed filename inside the checked versioned runtime.
            package = (home / ".local/share/chatgpt-cef-v2"
                       / ("runtime-v" + a.version) / ".approved-release.zip")
            print("UPGRADE_STAGED=" + str(activate(
                home, a.version, a.binary_sha256, a.sandbox_sha256,
                package, a.package_sha256, a.source_commit)))
            print("UPGRADE_PENDING_OWNER_ACCEPTANCE=YES")
        else:
            action(home, "rollback" if a.rollback else "finalize")
            print("UPGRADE_" + ("ROLLBACK" if a.rollback else "FINALIZED") + "=PASS")
    except (UpgradeError, OSError, ValueError, subprocess.CalledProcessError, KeyError) as exc:
        print("UPGRADE_FAIL_CLOSED=" + str(exc), file=sys.stderr)
        return 24
    return 0


if __name__ == "__main__":
    sys.exit(main())
