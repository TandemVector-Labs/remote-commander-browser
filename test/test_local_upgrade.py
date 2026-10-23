"""Reversible upgrade activation; zero privileged changes and no live profiles."""
import hashlib
import importlib.util
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/upgrade-local.py"
spec = importlib.util.spec_from_file_location("browser_safe_upgrade", SCRIPT)
upgrade = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upgrade)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class TransactionTests(unittest.TestCase):
    VERSION = "0.8.4-rc.1"
    SOURCE = "a" * 40

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="rc-upgrade-test-")
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.home = root / "home"
        self.home.mkdir(mode=0o700)
        self.app = self.home / ".local/share/chatgpt-cef-v2"
        self.app.mkdir(parents=True)
        self.runtime = self.app / ("runtime-v" + self.VERSION)
        self.runtime.mkdir()
        self.binary = self.runtime / "chatgpt-cef-v2"
        self.binary.write_bytes(b"#!/bin/sh\nexit 0\n")
        self.binary.chmod(0o755)
        self.sandbox = self.runtime / "chrome-sandbox"
        self.sandbox.write_bytes(b"fake helper bytes; NEVER SUID\n")
        self.sandbox.chmod(0o755)
        self.package = self.runtime / ".approved-release.zip"
        self.package.write_bytes(b"immutable package fixture\n")
        self.profile = self.home / ".config/chatgpt-cef-v2"
        self.profile.mkdir(parents=True)
        (self.profile / "cookies.private").write_bytes(b"private-profile-preservation-marker")
        self.launcher = self.home / ".local/bin/chatgpt-cef-v2"
        self.desktop = self.home / ".local/share/applications/chatgpt-cef-v2.desktop"
        self.launcher.parent.mkdir(parents=True)
        self.desktop.parent.mkdir(parents=True)
        self.old_launcher = b"#!/bin/sh\n# previous accepted launcher\nexit 0\n"
        self.old_desktop = b"[Desktop Entry]\nName=Old Browser\n"
        self.launcher.write_bytes(self.old_launcher)
        self.launcher.chmod(0o755)
        self.desktop.write_bytes(self.old_desktop)
        self.desktop.chmod(0o644)

    def kwargs(self):
        return dict(home=self.home, version=self.VERSION,
                    binary_hash=upgrade.digest_file(self.binary),
                    sandbox_hash=upgrade.digest_file(self.sandbox),
                    package=self.package,
                    package_hash=upgrade.digest_file(self.package),
                    source_commit=self.SOURCE)

    def trusted_fixture(self):
        """Only inside unittest, simulate a properly staged root-owned helper.

        Production CLI never allows injection or bypass of the real lstat.
        """
        original = Path.lstat
        sandbox = self.sandbox

        def fake_lstat(path, *args, **kwargs):
            value = original(path, *args, **kwargs)
            if str(path) != str(sandbox):
                return value
            fields = list(value)
            fields[0] = stat.S_IFREG | 0o4755
            fields[4] = 0
            fields[5] = 0
            return os.stat_result(fields)
        return mock.patch.object(Path, "lstat", new=fake_lstat)

    def assert_unmodified(self):
        self.assertEqual(self.launcher.read_bytes(), self.old_launcher)
        self.assertEqual(self.desktop.read_bytes(), self.old_desktop)
        self.assertEqual((self.profile / "cookies.private").read_bytes(),
                         b"private-profile-preservation-marker")
        self.assertEqual(self.binary.read_bytes(), b"#!/bin/sh\nexit 0\n")
        self.assertFalse((self.app / upgrade.STATE).exists())

    def test_activation_then_rollback_restores_exact_old_files_and_profile(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            self.assertIn(b"runtime-v0.8.4-rc.1", self.launcher.read_bytes())
            self.assertTrue((self.app / upgrade.STATE).is_dir())
            self.assertIn(b"sha256sum", self.launcher.read_bytes())
            upgrade.action(self.home, "rollback")
        self.assert_unmodified()

    def test_interrupt_after_first_atomic_switch_auto_rolls_back(self):
        with self.trusted_fixture():
            with self.assertRaisesRegex(upgrade.UpgradeError, "simulated"):
                upgrade.activate(**self.kwargs(), fail_at="launcher")
        self.assert_unmodified()

    def test_invalid_suid_owner_is_denied_before_writes(self):
        with self.assertRaisesRegex(upgrade.UpgradeError, "SUID sandbox"):
            upgrade.activate(**self.kwargs())
        self.assert_unmodified()

    def test_symlink_helper_outside_candidate_is_denied(self):
        self.sandbox.unlink()
        self.sandbox.symlink_to(self.package)
        with self.assertRaisesRegex(upgrade.UpgradeError, "linked/missing"):
            upgrade.activate(**self.kwargs())
        self.assert_unmodified()

    def test_package_sha_mismatch_denied_before_writes(self):
        args = self.kwargs()
        args["package_hash"] = "b" * 64
        with self.trusted_fixture():
            with self.assertRaisesRegex(upgrade.UpgradeError, "package digest"):
                upgrade.activate(**args)
        self.assert_unmodified()

    def test_duplicate_version_cannot_replace_old_launcher(self):
        self.old_launcher = ("RUNTIME=/x/runtime-v" + self.VERSION + "\n").encode()
        self.launcher.write_bytes(self.old_launcher)
        with self.trusted_fixture():
            with self.assertRaisesRegex(upgrade.UpgradeError, "same-version"):
                upgrade.activate(**self.kwargs())
        self.assert_unmodified()

    def test_pending_upgrade_must_be_resolved_before_next_activation(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            original = self.launcher.read_bytes()
            with self.assertRaisesRegex(upgrade.UpgradeError, "pending transaction"):
                upgrade.activate(**self.kwargs())
            self.assertEqual(self.launcher.read_bytes(), original)
            upgrade.action(self.home, "rollback")
        self.assert_unmodified()

    def test_rollback_refuses_undocumented_user_edits(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            self.desktop.write_bytes(b"modified by a user after activation")
            with self.assertRaisesRegex(upgrade.UpgradeError, "rollback conflict"):
                upgrade.action(self.home, "rollback")
            self.assertTrue((self.app / upgrade.STATE).exists())
            self.assertIn(b"runtime-v0.8.4", self.launcher.read_bytes())

    def test_rollback_recovers_partial_update_state(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            self.desktop.write_bytes(self.old_desktop)  # crash between replacements
            upgrade.action(self.home, "rollback")
        self.assert_unmodified()

    def test_finalize_preserves_old_runtime_and_profile(self):
        legacy = self.app / "runtime-v0.8.1"
        legacy.mkdir()
        (legacy / "KEEP").write_bytes(b"rollback runtime")
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            upgrade.action(self.home, "finalize")
        self.assertFalse((self.app / upgrade.STATE).exists())
        self.assertEqual((legacy / "KEEP").read_bytes(), b"rollback runtime")
        self.assertEqual((self.profile / "cookies.private").read_bytes(),
                         b"private-profile-preservation-marker")

    def test_group_or_world_writable_app_root_is_denied(self):
        self.app.chmod(0o777)
        with self.trusted_fixture():
            with self.assertRaisesRegex(upgrade.UpgradeError, "unsafe directory"):
                upgrade.activate(**self.kwargs())
        self.assert_unmodified()

    def test_other_writable_lock_file_is_denied(self):
        lock = self.app / upgrade.LOCK
        lock.write_bytes(b"untrusted lock")
        lock.chmod(0o666)
        with self.trusted_fixture():
            with self.assertRaisesRegex(upgrade.UpgradeError, "unsafe upgrade lock"):
                upgrade.activate(**self.kwargs())
        self.assert_unmodified()

    def test_untrusted_journal_directory_is_denied_on_rollback(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            journal = self.app / upgrade.STATE
            journal.chmod(0o777)
            with self.assertRaisesRegex(upgrade.UpgradeError, "no trusted rollback"):
                upgrade.action(self.home, "rollback")
        self.assertIn(b"runtime-v0.8.4", self.launcher.read_bytes())

    def test_cli_refuses_arbitrary_filesystem_target_arguments(self):
        from subprocess import run
        from sys import executable
        for opt in ("--home", "--package"):
            proc = run([executable, str(SCRIPT), "--activate", opt, "/tmp/foreign"],
                       capture_output=True, text=True)
            self.assertEqual(proc.returncode, 2)
            self.assertIn("unrecognized arguments", proc.stderr)

    def test_symlinked_parent_is_denied(self):
        self.desktop.unlink()
        self.desktop.parent.rmdir()
        target = Path(self.tmp.name) / "redirect"
        target.mkdir()
        self.desktop.parent.symlink_to(target)
        with self.trusted_fixture():
            with self.assertRaisesRegex(upgrade.UpgradeError, "symlink ancestor"):
                upgrade.activate(**self.kwargs())
        self.assertEqual(self.launcher.read_bytes(), self.old_launcher)


    def test_world_writable_candidate_binary_is_denied(self):
        self.binary.chmod(0o777)
        with self.trusted_fixture():
            with self.assertRaisesRegex(upgrade.UpgradeError, "unsafe binary"):
                upgrade.activate(**self.kwargs())
        self.assert_unmodified()

    def test_world_writable_existing_launcher_is_denied(self):
        self.launcher.chmod(0o777)
        with self.trusted_fixture():
            with self.assertRaisesRegex(upgrade.UpgradeError, "unsafe launcher/desktop"):
                upgrade.activate(**self.kwargs())
        self.assert_unmodified()

    def test_finalize_rejects_world_writable_new_launcher_then_rollback_works(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            self.launcher.chmod(0o777)
            with self.assertRaisesRegex(upgrade.UpgradeError, "finalize conflict"):
                upgrade.action(self.home, "finalize")
            self.assertTrue((self.app / upgrade.STATE).exists())
            upgrade.action(self.home, "rollback")
        self.assert_unmodified()
        self.assertEqual(stat.S_IMODE(self.launcher.stat().st_mode), 0o755)

    def test_finalize_rejects_world_writable_new_desktop_then_rollback_works(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            self.desktop.chmod(0o666)
            with self.assertRaisesRegex(upgrade.UpgradeError, "finalize conflict"):
                upgrade.action(self.home, "finalize")
            self.assertTrue((self.app / upgrade.STATE).exists())
            upgrade.action(self.home, "rollback")
        self.assert_unmodified()
        self.assertEqual(stat.S_IMODE(self.desktop.stat().st_mode), 0o644)


    def test_world_writable_approved_package_denied(self):
        self.package.chmod(0o777)
        with self.trusted_fixture():
            with self.assertRaisesRegex(upgrade.UpgradeError, "unsafe staged package"):
                upgrade.activate(**self.kwargs())
        self.assert_unmodified()


    def test_finalize_revalidates_staged_binary_after_activation(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            original = self.binary.read_bytes()
            self.binary.write_bytes(original + b"UNTRUSTED_BINARY_DRIFT")
            with self.assertRaisesRegex(upgrade.UpgradeError, "binary digest mismatch"):
                upgrade.action(self.home, "finalize")
            self.assertTrue((self.app / upgrade.STATE).exists())
            self.binary.write_bytes(original)
            upgrade.action(self.home, "rollback")
        self.assert_unmodified()

    def test_finalize_revalidates_approved_package_after_activation(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            original = self.package.read_bytes()
            self.package.write_bytes(original + b"UNTRUSTED_PACKAGE_DRIFT")
            with self.assertRaisesRegex(upgrade.UpgradeError, "candidate package digest mismatch"):
                upgrade.action(self.home, "finalize")
            self.assertTrue((self.app / upgrade.STATE).exists())
            self.package.write_bytes(original)
            upgrade.action(self.home, "rollback")
        self.assert_unmodified()

    def test_finalize_revalidates_suid_helper_after_activation(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            original = self.sandbox.read_bytes()
            self.sandbox.write_bytes(original + b"UNTRUSTED_SANDBOX_DRIFT")
            with self.assertRaisesRegex(upgrade.UpgradeError, "sandbox digest mismatch"):
                upgrade.action(self.home, "finalize")
            self.assertTrue((self.app / upgrade.STATE).exists())
            self.sandbox.write_bytes(original)
            upgrade.action(self.home, "rollback")
        self.assert_unmodified()

    def test_finalize_refuses_new_world_writable_package(self):
        with self.trusted_fixture():
            upgrade.activate(**self.kwargs())
            previous = stat.S_IMODE(self.package.stat().st_mode)
            self.package.chmod(0o777)
            with self.assertRaisesRegex(upgrade.UpgradeError, "unsafe staged package"):
                upgrade.action(self.home, "finalize")
            self.assertTrue((self.app / upgrade.STATE).exists())
            self.package.chmod(previous)
            upgrade.action(self.home, "rollback")
        self.assert_unmodified()


if __name__ == "__main__":
    unittest.main()
