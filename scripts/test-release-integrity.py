#!/usr/bin/env python3
"""Bounded release-contract regression using harmless non-product fixtures."""
from __future__ import annotations
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "release_integrity", ROOT / "scripts" / "verify-release-assets.py"
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
VERSION = "0.8.1"

class ReleaseIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="browser release integrity ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.release = self.root / "release"
        self.release.mkdir()
        self.setup = f"Remote-Commander-Browser-Setup-v{VERSION}.exe"
        self.windows = f"remote-commander-browser-v{VERSION}-windows-x86_64.zip"
        self.linux = f"remote-commander-browser-v{VERSION}-linux-x86_64.tar.gz"
        self.pairs = {
            f"SHA256SUMS-setup-v{VERSION}.txt": self.setup,
            f"SHA256SUMS-windows-v{VERSION}.txt": self.windows,
            f"{self.linux}.sha256": self.linux,
        }
        for manifest, asset in self.pairs.items():
            data = b"NONEXECUTABLE_RELEASE_FIXTURE:" + asset.encode("ascii")
            (self.release / asset).write_bytes(data)
            (self.release / manifest).write_text(
                hashlib.sha256(data).hexdigest() + "  " + asset + "\n",
                encoding="ascii",
            )
        self.manifest = self.release / f"{self.linux}.sha256"

    def rejected(self):
        with self.assertRaises(MODULE.IntegrityError):
            MODULE.verify(VERSION, self.release)

    def test_exact_payloads_and_producer_hashes(self):
        self.assertEqual(len(MODULE.verify(VERSION, self.release)), 3)

    def test_tampered_payload_rejected(self):
        (self.release / self.setup).write_bytes(b"TAMPERED")
        self.rejected()

    def test_missing_payload_rejected(self):
        (self.release / self.windows).unlink()
        self.rejected()

    def test_extra_file_rejected(self):
        (self.release / "unexpected.txt").write_bytes(b"EXTRA")
        self.rejected()

    def test_absolute_producer_path_rejected(self):
        text = self.manifest.read_text(encoding="ascii")
        self.manifest.write_text(text.replace("  ", "  /home/runner/work/project/dist/"), encoding="ascii")
        self.rejected()

    def test_parent_path_rejected(self):
        text = self.manifest.read_text(encoding="ascii")
        self.manifest.write_text(text.replace("  ", "  ../"), encoding="ascii")
        self.rejected()

    def test_duplicate_checksum_record_rejected(self):
        text = self.manifest.read_text(encoding="ascii")
        self.manifest.write_text(text + text, encoding="ascii")
        self.rejected()

    def test_wrong_basename_rejected(self):
        text = self.manifest.read_text(encoding="ascii")
        self.manifest.write_text(text.replace(self.linux, "other.tar.gz"), encoding="ascii")
        self.rejected()

    def test_directory_as_payload_rejected(self):
        member = self.release / self.setup
        member.unlink()
        member.mkdir()
        self.rejected()

    def test_invalid_version_rejected(self):
        for value in ("../0.8", "v0.8.0", "0.8.0/../../x", "0.8.0;echo unsafe", ""):
            with self.subTest(value=value), self.assertRaises(MODULE.IntegrityError):
                MODULE.verify(value, self.release)

    def test_cli_returns_nonzero_for_bad_hash(self):
        (self.release / self.setup).write_bytes(b"TAMPERED")
        import sys
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts" / "verify-release-assets.py"), VERSION, str(self.release)],
            capture_output=True, text=True, timeout=15,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("RELEASE_PRODUCER_SHA256_FAIL", result.stderr)

    @unittest.skipUnless(os.name == "posix", "native POSIX symlink semantics")
    def test_symlink_member_rejected(self):
        member = self.release / self.setup
        outside = self.root / "outside"
        outside.write_bytes(member.read_bytes())
        member.unlink()
        member.symlink_to(outside)
        self.rejected()

    def test_publication_policy_contract(self):
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
        self.assertIn("python3 scripts/verify-release-assets.py", workflow)
        self.assertLess(
            workflow.index("python3 scripts/verify-release-assets.py"),
            workflow.index(" > SHA256SUMS.txt"),
        )
        self.assertIn("release_flags=(--latest)", workflow)
        self.assertIn("release_flags=(--prerelease --latest=false)", workflow)
        self.assertIn('RELEASE_ALREADY_EXISTS_RECONCILE', workflow)
        self.assertIn('test "$immutable" = true', workflow)
        self.assertIn('needs: [windows, linux, release-contract]', workflow)
        stage = workflow.split("Package and stage exact Windows release assets", 1)[1].split(
            "      - uses:", 1
        )[0]
        self.assertNotIn("if($LASTEXITCODE -ne 0)", stage)

    def test_r256_sigstore_provenance_is_fail_closed_before_browser_release(self):
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
        assert "permissions:\n  contents: read\n" in workflow
        publish = workflow.split("\n  publish:\n", 1)[1]
        for marker in ("if: github.event_name != 'pull_request'",
                       "needs: [windows, linux, release-contract]",
                       "contents: write", "id-token: write",
                       "attestations: write", "artifact-metadata: write",
                       "R256_RELEASE_PREATTEST_INTEGRITY_PASS",
                       "uses: actions/attest@1e69f48acb82d1966a394da916b4c1698aa569d6",
                       "subject-path: 'dist/release/*'",
                       "gh attestation verify",
                       "R256_RELEASE_SIGNED_ATTESTATION_PASS subjects=",
                       'test "$verified" -eq 7'):
            self.assertIn(marker, publish)
        self.assertLess(publish.index("R256_RELEASE_PREATTEST_INTEGRITY_PASS"),
                        publish.index("uses: actions/attest@"))
        self.assertLess(publish.index("uses: actions/attest@"),
                        publish.index("gh attestation verify"))
        self.assertLess(publish.index("gh attestation verify"),
                        publish.index("gh release create"))
        self.assertNotIn("continue-on-error: true", publish)

    @unittest.skipUnless(os.name == "posix", "native Linux package execution")
    def test_actual_packager_checksum_is_portable(self):
        fixture = self.root / "project with spaces"
        for directory in ("scripts", "build/bin", ".deps/cef"):
            (fixture / directory).mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "scripts" / "package-release.sh", fixture / "scripts" / "package-release.sh")
        (fixture / "VERSION").write_text(VERSION + "\n", encoding="ascii")
        (fixture / "build/bin/chatgpt-cef-v2").write_text("#!/bin/sh\nexit 0\n", encoding="ascii")
        (fixture / "build/bin/chatgpt-cef-v2").chmod(0o755)
        for name in ("LICENSE.txt", "CREDITS.html"):
            (fixture / ".deps/cef" / name).write_text("FIXTURE ONLY\n", encoding="ascii")
        for name in ("NOTICE.md", "THIRD_PARTY_NOTICES.md", "RELEASE_NOTES.md"):
            (fixture / name).write_text("FIXTURE ONLY\n", encoding="ascii")
        result = subprocess.run(
            ["bash", str(fixture / "scripts/package-release.sh")],
            cwd=self.root, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        sums = fixture / "dist" / f"remote-commander-browser-v{VERSION}-linux-x86_64.tar.gz.sha256"
        line = sums.read_text(encoding="ascii").strip()
        self.assertEqual(line.split("  ", 1)[1], f"remote-commander-browser-v{VERSION}-linux-x86_64.tar.gz")
        check = subprocess.run(
            ["sha256sum", "--check", sums.name], cwd=sums.parent,
            capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(check.returncode, 0, check.stderr)


    @unittest.skipUnless(os.name == "posix", "native generated Linux installer and launcher")
    def test_generated_linux_installer_launcher_and_hash_guards(self):
        import sys
        fixture = self.root / "generated installer project"
        home = self.root / "home with spaces"
        tools = self.root / "guarded fixture tools"
        for directory in ("scripts", "build/bin", ".deps/cef"):
            (fixture / directory).mkdir(parents=True, exist_ok=True)
        home.mkdir()
        tools.mkdir()
        shutil.copyfile(ROOT / "scripts/package-release.sh", fixture / "scripts/package-release.sh")
        (fixture / "VERSION").write_text(VERSION + "\n", encoding="ascii")
        payload = "#!/bin/sh\nprintf '<%s>\\n' \"$@\"\n"
        for name in ("chatgpt-cef-v2", "chrome-sandbox"):
            (fixture / "build/bin" / name).write_text(payload, encoding="ascii")
            (fixture / "build/bin" / name).chmod(0o755)
        for name in ("LICENSE.txt", "CREDITS.html"):
            (fixture / ".deps/cef" / name).write_text("FIXTURE ONLY\n", encoding="ascii")
        for name in ("NOTICE.md", "THIRD_PARTY_NOTICES.md", "RELEASE_NOTES.md"):
            (fixture / name).write_text("FIXTURE ONLY\n", encoding="ascii")
        package = subprocess.run(
            ["bash", str(fixture / "scripts/package-release.sh")],
            cwd=self.root, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(package.returncode, 0, package.stderr)
        # Never execute sudo or change real ownership/SUID permissions.
        sudo_stub = "\n".join([
            "#!" + sys.executable,
            "import os,pathlib,sys",
            "a=sys.argv[1:]",
            "if len(a)!=3 or a[:2] not in [['chown','root:root'],['chmod','4755']]: raise SystemExit(91)",
            "if not pathlib.Path(a[2]).resolve().is_relative_to(pathlib.Path(os.environ['TEST_HOME']).resolve()): raise SystemExit(92)",
            "raise SystemExit(0)",
            "",
        ])
        stat_stub = "\n".join([
            "#!" + sys.executable,
            "import os,pathlib,sys",
            "a=sys.argv[1:]",
            f"expected=pathlib.Path(os.environ['TEST_HOME'])/'.local/share/chatgpt-cef-v2/runtime-v{VERSION}/chrome-sandbox'",
            "if len(a)!=3 or a[:2]!=['-Lc','%a %U %G'] or pathlib.Path(a[2])!=expected: raise SystemExit(93)",
            "print('4755 root root')",
            "",
        ])
        for name, content in (("sudo", sudo_stub), ("stat", stat_stub)):
            (tools / name).write_text(content, encoding="utf-8")
            (tools / name).chmod(0o755)
        env = {
            "HOME": str(home), "PATH": str(tools) + ":/usr/bin:/bin",
            "TEST_HOME": str(home), "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8",
        }
        stage = fixture / "dist" / f"remote-commander-browser-v{VERSION}-linux-x86_64"
        install = subprocess.run(
            ["bash", str(stage / "install.sh")], cwd=stage, env=env,
            capture_output=True, text=True, timeout=15,
        )
        self.assertEqual(install.returncode, 0, install.stderr)
        launcher = home / ".local/bin/chatgpt-cef-v2"
        self.assertIn('exec "$BIN" "$@"', launcher.read_text(encoding="utf-8"))
        syntax = subprocess.run(
            ["bash", "-n", str(launcher)], env=env, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(syntax.returncode, 0, syntax.stderr)
        arguments = ["alpha beta", "*.txt", "--literal=$HOME", "فارسی"]
        launch = subprocess.run(
            [str(launcher), *arguments], env=env, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(launch.returncode, 0, launch.stderr)
        self.assertEqual(launch.stdout, "".join("<" + value + ">\n" for value in arguments))
        runtime = home / ".local/share/chatgpt-cef-v2" / f"runtime-v{VERSION}"
        binary = runtime / "chatgpt-cef-v2"
        original = binary.read_bytes()
        binary.write_bytes(original + b"# TAMPER\n")
        rejected = subprocess.run(
            [str(launcher)], env=env, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(rejected.returncode, 23, rejected.stderr)
        self.assertEqual(rejected.stdout, "")
        binary.write_bytes(original)
        sandbox = runtime / "chrome-sandbox"
        sandbox.write_bytes(sandbox.read_bytes() + b"# TAMPER\n")
        rejected = subprocess.run(
            [str(launcher)], env=env, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(rejected.returncode, 24, rejected.stderr)
        self.assertEqual(rejected.stdout, "")


    @unittest.skipUnless(os.name == "posix", "Linux runtime/symlink semantics")
    def test_uninstall_old_runtime_denies_live_newer_sandbox_symlink(self):
        # A newer stable runtime can depend on a root-owned helper from an
        # older runtime. No sudo, profile or launcher mutation is permissible.
        home = self.root / "uninstall-home"
        base = home / ".local" / "share" / "chatgpt-cef-v2"
        uninstall_version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        old = base / f"runtime-v{uninstall_version}"
        newer = base / "runtime-v9.9.9"
        old.mkdir(parents=True)
        newer.mkdir()
        old_sandbox = old / "chrome-sandbox"
        old_sandbox.write_bytes(b"private test sandbox; no SUID\n")
        (newer / "chrome-sandbox").symlink_to(old_sandbox)
        launcher = home / ".local" / "bin" / "chatgpt-cef-v2"
        launcher.parent.mkdir(parents=True)
        launcher.write_text(f'#!/bin/sh\nRUNTIME="{newer}"\n', encoding="utf-8")
        desktop = home / ".local" / "share" / "applications" / "chatgpt-cef-v2.desktop"
        desktop.parent.mkdir(parents=True)
        desktop.write_text("[Desktop Entry]\nName=Browser\n", encoding="utf-8")
        profile = home / ".config" / "chatgpt-cef-v2"
        profile.mkdir(parents=True)
        (profile / "session").write_text("PRESERVE", encoding="utf-8")
        env = {**os.environ, "HOME": str(home)}
        script = ROOT / "scripts" / "uninstall-local.sh"
        proc = subprocess.run(["bash", str(script)], env=env,
                              capture_output=True, text=True, timeout=12)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("UNINSTALL_SANDBOX_REFERENCED_BY_ANOTHER_VERSION", proc.stderr)
        self.assertTrue(old_sandbox.is_file())
        self.assertTrue((newer / "chrome-sandbox").is_symlink())
        self.assertTrue(launcher.is_file())
        self.assertTrue(desktop.is_file())
        self.assertEqual((profile / "session").read_text(), "PRESERVE")
        # Without the cross-version dependency, retirement may proceed, but
        # it must never remove the newer launcher, desktop entry or profile.
        (newer / "chrome-sandbox").unlink()
        retired = subprocess.run(["bash", str(script)], env=env,
                                capture_output=True, text=True, timeout=12)
        self.assertEqual(retired.returncode, 0, retired.stderr)
        self.assertFalse(old.exists())
        self.assertTrue(newer.exists())
        self.assertTrue(launcher.is_file())
        self.assertTrue(desktop.is_file())
        self.assertEqual((profile / "session").read_text(), "PRESERVE")

    @unittest.skipUnless(os.name == "posix", "Linux runtime/symlink semantics")
    def test_uninstall_runtime_symlink_fails_before_any_mutation(self):
        home = self.root / "uninstall-symlink-home"
        base = home / ".local" / "share" / "chatgpt-cef-v2"
        base.mkdir(parents=True)
        outside = self.root / "uninstall-outside-data"
        outside.mkdir()
        (outside / "sentinel").write_text("DO_NOT_TOUCH", encoding="utf-8")
        uninstall_version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        (base / f"runtime-v{uninstall_version}").symlink_to(outside, target_is_directory=True)
        env = {**os.environ, "HOME": str(home)}
        p = subprocess.run(["bash", str(ROOT / "scripts" / "uninstall-local.sh")],
                           env=env, capture_output=True, text=True, timeout=12)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("UNINSTALL_RUNTIME_REPARSE_REFUSED", p.stderr)
        self.assertEqual((outside / "sentinel").read_text(), "DO_NOT_TOUCH")

    @unittest.skipUnless(os.name == "posix", "Linux ancestor-symlink semantics")
    def test_uninstall_rejects_symlinked_runtime_ancestors(self):
        version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        for location in ("approot", "share"):
            with self.subTest(location=location):
                home = self.root / f"host-{location}"
                outside = self.root / f"external-{location}"
                outside.mkdir()
                if location == "approot":
                    (home / ".local" / "share").mkdir(parents=True)
                    (home / ".local" / "share" / "chatgpt-cef-v2").symlink_to(
                        outside, target_is_directory=True)
                    external_runtime = outside / f"runtime-v{version}"
                else:
                    (home / ".local").mkdir(parents=True)
                    (home / ".local" / "share").symlink_to(
                        outside, target_is_directory=True)
                    external_runtime = outside / "chatgpt-cef-v2" / f"runtime-v{version}"
                external_runtime.mkdir(parents=True)
                sentinel = external_runtime / "SYNTHETIC_DO_NOT_DELETE"
                sentinel.write_text("PRESERVE", encoding="utf-8")
                p = subprocess.run(
                    ["bash", str(ROOT / "scripts" / "uninstall-local.sh")],
                    env={**os.environ, "HOME": str(home)},
                    capture_output=True, text=True, timeout=12)
                self.assertNotEqual(p.returncode, 0, p.stdout)
                self.assertIn("UNINSTALL_ANCESTOR_REPARSE_REFUSED", p.stderr)
                self.assertEqual(sentinel.read_text(encoding="utf-8"), "PRESERVE")

    @unittest.skipUnless(os.name == "posix", "Linux config ancestor-symlink semantics")
    def test_uninstall_purge_profile_rejects_config_ancestor_symlink(self):
        home = self.root / "profile-ancestor-home"
        version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        runtime = home / ".local" / "share" / "chatgpt-cef-v2" / f"runtime-v{version}"
        runtime.mkdir(parents=True)
        (runtime / "SYNTHETIC_RUNTIME").write_text("PRESERVE", encoding="utf-8")
        launcher = home / ".local" / "bin" / "chatgpt-cef-v2"
        launcher.parent.mkdir(parents=True)
        launcher.write_text(f'#!/bin/sh\nRUNTIME="{runtime}"\n', encoding="utf-8")
        outside = self.root / "external-config-ancestor"
        (outside / "chatgpt-cef-v2").mkdir(parents=True)
        sentinel = outside / "chatgpt-cef-v2" / "SYNTHETIC_PROFILE"
        sentinel.write_text("PRESERVE", encoding="utf-8")
        (home / ".config").symlink_to(outside, target_is_directory=True)
        p = subprocess.run(
            ["bash", str(ROOT / "scripts" / "uninstall-local.sh"), "--purge-profile"],
            env={**os.environ, "HOME": str(home)},
            capture_output=True, text=True, timeout=12)
        self.assertNotEqual(p.returncode, 0, p.stdout)
        self.assertIn("UNINSTALL_ANCESTOR_REPARSE_REFUSED", p.stderr)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "PRESERVE")
        self.assertTrue(runtime.is_dir())

if __name__ == "__main__":
    unittest.main(verbosity=2)
