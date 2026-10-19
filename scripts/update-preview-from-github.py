#!/usr/bin/env python3
"""Single Glass Preview updater: only newer immutable, signed official GitHub releases.

Never touches the primary Browser, real profile, system sandbox ownership, or PR builds.
The published Linux archive's helper must match the existing trusted root-owned
helper; incompatible CEF upgrades fail closed until provisioned through approval.
"""
from __future__ import annotations
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time

REPO = "Usefull-Skills/remote-commander-browser"
WORKFLOW = f"{REPO}/.github/workflows/release.yml"
INITIAL_COMMIT = "5a0a9854649de3e6e3f919e7dc3431765ad42d38"
INITIAL_VERSION = "0.8.3-rc.2"
INITIAL_LAUNCH_SHA = "5cc4bc1e81d7526f9970d72027e2b28e972fce9255809cccdffeb87be92d0853"
INITIAL_ELF_SHA = "6e7aa2205e70547282442c8d9d547d4dbbeb32ed2262b0f15d33ae09208947e6"
INITIAL_HELPER_SHA = "02deb82893f0fd892d9041f7f2dc109dc132ad0bf303cef946f9660bf98eb2e0"
VERSION = re.compile(r"^([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})(?:-rc\.([0-9]{1,3}))?$")
HASH = re.compile(r"^[0-9a-f]{64}$")
GH = "/home/mmz/.local/bin/gh"

class UpdateRefused(RuntimeError):
    pass

def demand(condition, message):
    if not condition:
        raise UpdateRefused(message)

def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for b in iter(lambda: f.read(2 * 1024 * 1024), b""):
            h.update(b)
    return h.hexdigest()

def parse_version(s):
    m = VERSION.fullmatch(s)
    demand(m is not None, "invalid/unsupported version")
    x = list(map(int, m.group(1, 2, 3)))
    return (*x, 0 if m.group(4) is not None else 1,
            int(m.group(4)) if m.group(4) is not None else 0)

def call(*args, timeout=40):
    done = subprocess.run([GH, *args], text=True, capture_output=True,
                          timeout=timeout, check=False)
    demand(done.returncode == 0, "GitHub CLI request/verification failed")
    return done.stdout

def roots(home):
    demand(home == home.resolve() and home.is_absolute(), "unsafe home")
    app = home / ".local/state/rc-browser-glass-preview"
    root = home / ".local/share/chatgpt-cef-v2"
    launcher = home / ".local/bin/rc-browser-glass-preview"
    fallback = root / "runtime-v0.8.3-rc.2-qa114.3b818244/chrome-sandbox"
    for p in (app, root, launcher.parent):
        demand(p.is_dir() and not p.is_symlink() and p.stat().st_uid == os.getuid()
               and not(p.stat().st_mode & 0o022), "unsafe local directory")
    demand(not launcher.is_symlink() and launcher.is_file(), "linked/missing launcher")
    demand(fallback.is_file() and not fallback.is_symlink(), "fallback helper missing")
    s = fallback.stat()
    demand(s.st_uid == 0 and s.st_gid == 0 and stat.S_IMODE(s.st_mode) == 0o4755,
           "trusted root SUID sandbox unavailable")
    demand(sha(fallback) == INITIAL_HELPER_SHA, "trusted sandbox digest changed")
    profile = app / "profile"
    demand(profile.is_dir() and not profile.is_symlink()
           and stat.S_IMODE(profile.stat().st_mode) == 0o700,
           "preview profile missing/unsafe")
    return app, root, launcher, fallback

def baseline(launcher, root):
    runtime = root / "runtime-local-preview-5a0a985-20261019"
    elf = runtime / "chatgpt-cef-v2"
    demand(runtime.is_dir() and not runtime.is_symlink()
           and sha(elf) == INITIAL_ELF_SHA, "initial active runtime mismatch")
    demand(sha(launcher) == INITIAL_LAUNCH_SHA, "initial launcher mismatch")
    return {"version": INITIAL_VERSION, "source_commit": INITIAL_COMMIT,
            "launcher_sha256": INITIAL_LAUNCH_SHA,
            "elf_sha256": INITIAL_ELF_SHA, "runtime": str(runtime)}

def read_state(app, root, launcher):
    state = app / "github-update-state.json"
    if state.exists():
        demand(not state.is_symlink() and state.stat().st_uid == os.getuid()
               and stat.S_IMODE(state.stat().st_mode) == 0o600,
               "unsafe update state")
        data = json.loads(state.read_text())
        for field in ("version", "source_commit", "launcher_sha256", "elf_sha256", "runtime"):
            demand(isinstance(data.get(field), str), "corrupt update state")
        demand(re.fullmatch(r"[a-f0-9]{40}", data["source_commit"]), "unsafe source commit")
        demand(HASH.fullmatch(data["launcher_sha256"]) and HASH.fullmatch(data["elf_sha256"]), "unsafe update hashes")
        demand(Path(data["runtime"]).parent == root, "out-of-scope update runtime")
        demand(sha(launcher) == data["launcher_sha256"]
               and sha(Path(data["runtime"]) / "chatgpt-cef-v2") == data["elf_sha256"],
               "running candidate drift, manual recovery required")
        return data
    return baseline(launcher, root)

def release_candidate(current):
    rel = json.loads(call("api", f"repos/{REPO}/releases/latest"))
    demand(isinstance(rel, dict), "bad GitHub release")
    tag = rel.get("tag_name", "")
    demand(isinstance(tag, str) and tag.startswith("v"), "unexpected release tag")
    version = tag[1:]
    eligible = parse_version(version) > parse_version(current["version"])
    if not eligible:
        return None, "NO_NEWER_STABLE_RELEASE"
    demand("-rc." not in version, "pre-release version cannot enter stable channel")
    demand(not rel.get("draft") and not rel.get("prerelease") and rel.get("immutable") is True,
           "candidate is not immutable official stable release")
    assets = {x["name"]: x for x in rel.get("assets", [])
              if isinstance(x, dict) and isinstance(x.get("name"), str)}
    archive = f"remote-commander-browser-v{version}-linux-x86_64.tar.gz"
    checksum = archive + ".sha256"
    for name in (archive, checksum):
        data = assets.get(name)
        demand(data and isinstance(data.get("digest"), str)
               and data["digest"].startswith("sha256:")
               and HASH.fullmatch(data["digest"][7:]), "missing authoritative GitHub asset hash")
    # A future release must include the exact accepted current Browser lineage.
    compare = json.loads(call("api", f"repos/{REPO}/compare/{current['source_commit']}...{tag}"))
    demand(compare.get("status") in ("ahead", "identical"),
           "release does not descend from accepted Browser commit")
    source = compare["commits"][-1]["sha"] if compare.get("commits") else current["source_commit"]
    demand(isinstance(source, str) and re.fullmatch(r"[0-9a-f]{40}", source),
           "invalid verified release source commit")
    return {"version": version, "tag": tag, "assets": assets,
            "archive": archive, "checksum": checksum,
            "source_commit": source}, "ELIGIBLE_SIGNED_RELEASE"

def verify_checksum(checkfile, archive, expected):
    demand(checkfile.stat().st_size < 512, "checksum manifest too large")
    raw = checkfile.read_text(encoding="ascii").splitlines()
    demand(raw == [f"{expected}  {archive}"], "release manifest and API digest disagree")

def extract_runtime(archive, version, dest, fallback):
    prefix = f"remote-commander-browser-v{version}-linux-x86_64/runtime/"
    count = 0
    total = 0
    seen = set()
    with tarfile.open(archive, "r:gz") as tar:
        for member in tar:
            name = member.name.removeprefix("./")
            demand(not name.startswith("/") and "\\" not in name, "unsafe archive path")
            parts = Path(name).parts
            demand(".." not in parts and member.size >= 0, "unsafe archive entry")
            # Other official release files are ignored; links anywhere are denied.
            demand(member.isfile() or member.isdir(), "release contains linked/special entry")
            if not name.startswith(prefix):
                continue
            rel = name[len(prefix):].rstrip("/")
            if not rel:
                continue
            demand(rel not in seen, "duplicate release file")
            seen.add(rel)
            target = dest / rel
            demand(target.parent == dest or dest in target.parents, "path traversal")
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                total += member.size
                demand(member.size <= 2_000_000_000 and total <= 5_000_000_000,
                       "unexpectedly large release")
                target.parent.mkdir(parents=True, exist_ok=True)
                fd = os.open(target, os.O_CREAT | os.O_EXCL | os.O_WRONLY | os.O_NOFOLLOW, 0o600)
                with os.fdopen(fd, "wb") as out, tar.extractfile(member) as inp:
                    demand(inp is not None, "corrupt release file")
                    shutil.copyfileobj(inp, out, length=1024*1024)
                    out.flush(); os.fsync(out.fileno())
                os.chmod(target, 0o755 if member.mode & 0o111 else 0o644)
                count += 1
    binary = dest / "chatgpt-cef-v2"
    sandbox = dest / "chrome-sandbox"
    demand(count > 10 and binary.is_file() and os.access(binary, os.X_OK)
           and sandbox.is_file() and not sandbox.is_symlink(),
           "incomplete native CEF runtime")
    demand(sha(sandbox) == sha(fallback),
           "CEF sandbox version changed; privileged owner provisioning required")
    sandbox.unlink()
    sandbox.symlink_to(fallback)
    return sha(binary)

def render_launcher(home, binary, elf_sha, fallback):
    runtime = binary.parent
    profile = home / ".local/state/rc-browser-glass-preview/profile"
    return f'''#!/usr/bin/env bash
set -euo pipefail
bin={binary}
helper={runtime / 'chrome-sandbox'}
profile={profile}
[[ -f "$bin" && -x "$bin" && ! -L "$bin" ]] || exit 51
[[ "$(sha256sum "$bin" | awk '{{print $1}}')" == '{elf_sha}' ]] || exit 52
[[ -L "$helper" && "$(readlink -f "$helper")" == '{fallback}' ]] || exit 53
[[ "$(stat -Lc '%a %U:%G' "$helper")" == '4755 root:root' ]] || exit 54
[[ "$(sha256sum "$helper" | awk '{{print $1}}')" == '{INITIAL_HELPER_SHA}' ]] || exit 55
[[ -d "$profile" && ! -L "$profile" ]] || exit 56
[[ "$(stat -c '%a %U:%G' "$profile")" == '700 {os.environ.get('USER', 'mmz')}:{os.environ.get('USER', 'mmz')}' ]] || exit 57
export CGWA_PROFILE_ROOT="$profile"
exec "$bin" --cgwa-profile-dir="$profile" --rc-home-preview --rc-colorful-chrome-preview --disable-gpu --disable-gpu-rasterization "$@"
'''.encode()

def write_atomic(path, data, mode):
    staged = path.with_name("." + path.name + ".new")
    demand(not staged.exists() and not staged.is_symlink(), "pending staged update")
    fd = os.open(staged, os.O_CREAT | os.O_EXCL | os.O_WRONLY | os.O_NOFOLLOW, mode)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data); f.flush(); os.fsync(f.fileno())
        os.replace(staged, path)
    finally:
        if staged.exists(): staged.unlink()

def wait_smoke(candidate, app):
    xvfb = Path("/home/mmz/.local/state/rc-browser-vertical-tabs/xvfb/payload/usr/bin/Xvfb")
    demand(xvfb.is_file() and os.access(xvfb, os.X_OK), "isolated native X server unavailable")
    with tempfile.TemporaryDirectory(prefix="rc-release-native-") as tmp:
        tmp = Path(tmp)
        for p in (tmp / "profile", tmp / "home", tmp / "home/.config", tmp / "home/.cache"):
            p.mkdir(parents=True, exist_ok=True, mode=0o700)
        (tmp / "profile/ui-theme.state").write_text("dark\n")
        script = tmp / "test-launch.sh"
        raw = candidate.decode().replace(str(app / "profile"), str(tmp / "profile"))
        script.write_text(raw)
        script.chmod(0o700)
        display = ":" + str(500 + (os.getpid() % 100))
        demand(not Path("/tmp/.X11-unix/X" + display[1:]).exists(),
               "native X server display already in use")
        x = subprocess.Popen([str(xvfb), display, "-screen", "0", "1440x900x24", "-nolisten", "tcp"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            socket = Path("/tmp/.X11-unix/X" + display[1:])
            for _ in range(40):
                if socket.exists(): break
                time.sleep(.1)
            demand(socket.exists(), "native X11 smoke server unavailable")
            env = {**os.environ, "HOME": str(tmp / "home"),
                   "XDG_CONFIG_HOME": str(tmp / "home/.config"),
                   "XDG_CACHE_HOME": str(tmp / "home/.cache"),
                   "DISPLAY": display, "XDG_SESSION_TYPE": "x11"}
            done = subprocess.run([str(script), "--rc-premium-menu-self-test",
                                   "--shutdown-self-test", "--log-file="+str(tmp / "chrome.log"),
                                   "--host-resolver-rules=MAP * ~NOTFOUND"],
                                  env=env, capture_output=True, timeout=35, check=False)
            content = (tmp / "chrome.log").read_text(errors="replace")
            demand(done.returncode == 0 and "CGWA_PREMIUM_MENU_SELFTEST SHOWN" in content
                   and "CGWA_SHUTDOWN WINDOW_DESTROYED" in content
                   and "FATAL:" not in content, "new Release native smoke failed")
        finally:
            x.terminate()
            try:x.wait(timeout=5)
            except subprocess.TimeoutExpired:x.kill();x.wait()

def apply_release(home, app, root, launcher, fallback, current, candidate):
    demand(not subprocess.run(["pgrep", "-x", "chatgpt-cef-v2"],
                              capture_output=True, check=False).returncode == 0,
           "CEF Browser running; defer update instead of interrupting user")
    version = candidate["version"]
    archive_name, manifest_name = candidate["archive"], candidate["checksum"]
    with tempfile.TemporaryDirectory(prefix="rc-github-release-") as tmp:
        tmp = Path(tmp)
        call("release", "download", candidate["tag"], "-R", REPO,
             "--dir", str(tmp), "--pattern", archive_name,
             "--pattern", manifest_name, timeout=180)
        archive, manifest = tmp / archive_name, tmp / manifest_name
        demand(archive.is_file() and manifest.is_file() and not archive.is_symlink()
               and not manifest.is_symlink(), "release download incomplete")
        ah = candidate["assets"][archive_name]["digest"][7:]
        mh = candidate["assets"][manifest_name]["digest"][7:]
        demand(sha(archive) == ah and sha(manifest) == mh, "GitHub release asset SHA mismatch")
        verify_checksum(manifest, archive_name, ah)
        call("attestation", "verify", str(archive), "--repo", REPO,
             "--signer-workflow", WORKFLOW,
             "--source-ref", "refs/tags/" + candidate["tag"], timeout=90)
        dest = root / ("runtime-local-preview-github-v" + version)
        demand(not dest.exists() and not dest.is_symlink(), "release runtime already staged")
        stage = root / (".preview-stage-" + version)
        demand(not stage.exists() and not stage.is_symlink(), "stale pending update")
        stage.mkdir(mode=0o700)
        try:
            elfsha = extract_runtime(archive, version, stage, fallback)
            new_launcher = render_launcher(home, dest / "chatgpt-cef-v2", elfsha, fallback)
            # The stage is moved into its final location before the test and atomic launcher switch.
            os.replace(stage, dest)
            try:
                wait_smoke(new_launcher, app)
            except BaseException:
                # Reject invalid release without retaining another 1.5 GiB runtime.
                shutil.rmtree(dest)
                raise
            previous = launcher.read_bytes()
            demand(sha(launcher) == current["launcher_sha256"], "launcher changed during update")
            recovery = app / "previous-launcher.before-github-update"
            # One retained rollback generation, never a growing archive of old products.
            demand(not recovery.is_symlink(), "linked rollback refused")
            if recovery.exists():
                mode = recovery.stat().st_mode
                demand(recovery.stat().st_uid == os.getuid()
                       and stat.S_ISREG(mode) and stat.S_IMODE(mode) == 0o600,
                       "unsafe rollback record")
            write_atomic(recovery, previous, 0o600)
            state = {"version":version, "source_commit":candidate["source_commit"],
                     "launcher_sha256":hashlib.sha256(new_launcher).hexdigest(),
                     "elf_sha256":elfsha, "runtime":str(dest)}
            try:
                write_atomic(launcher, new_launcher, 0o700)
                write_atomic(app / "github-update-state.json", json.dumps(state,indent=2).encode(), 0o600)
            except BaseException:
                write_atomic(launcher, previous, 0o700)
                raise
            print("PREVIEW_AUTO_UPGRADED=" + version)
        finally:
            if stage.exists(): shutil.rmtree(stage)

def main():
    p=argparse.ArgumentParser(allow_abbrev=False)
    p.add_argument("--check", action="store_true")
    p.add_argument("--auto", action="store_true")
    args=p.parse_args()
    demand(args.check != args.auto, "choose --check or --auto")
    demand(os.geteuid() != 0, "do not run updater with privileges")
    home=Path.home()
    app,root,launcher,fallback=roots(home)
    lock=app/".github-updater.lock"
    fd=os.open(lock, os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX|fcntl.LOCK_NB)
        current=read_state(app,root,launcher)
        release,msg=release_candidate(current)
        if release is None:
            print(f"GITHUB_PREVIEW_UPDATE={msg} current={current['version']}")
            return
        print("GITHUB_PREVIEW_UPDATE="+msg+" version="+release["version"])
        if args.auto:
            apply_release(home,app,root,launcher,fallback,current,release)
    finally:os.close(fd)

if __name__=="__main__":
    try:main()
    except (UpdateRefused, OSError, ValueError, KeyError, subprocess.TimeoutExpired,
            subprocess.SubprocessError, tarfile.TarError) as error:
        print("GITHUB_PREVIEW_UPDATE_FAIL_CLOSED="+str(error),file=sys.stderr)
        sys.exit(24)
