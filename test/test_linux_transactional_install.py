#!/usr/bin/env python3
"""QA on mock payloads; no production installation, sudo, network, or actual GUI."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SCRIPT = ROOT / "scripts/linux_transactional_install.py"
TRUSTED = Path(os.environ.get("RC_BROWSER_TEST_APPROVED_SANDBOX",
    "/home/mmz/.local/share/chatgpt-cef-v2/runtime-v0.8.3-rc.2-qa114.3b818244/chrome-sandbox"))

class TestTransactionalLinux(unittest.TestCase):
    def setUp(self):
        if os.environ.get("RC_BROWSER_TEST_APPROVED_SANDBOX"):
            self.assertTrue(TRUSTED.is_file(), "CI trusted fixture missing")
            self.assertEqual(TRUSTED.stat().st_uid, 0, "CI fixture must be root-owned")
            self.assertEqual(TRUSTED.stat().st_mode & 0o7777, 0o4755)
        elif not TRUSTED.is_file() or TRUSTED.stat().st_uid != 0:
            self.skipTest("Trusted SUID sandbox fixture is unavailable")
        parent = HERE / "linux-swap-fixture-r1"
        parent.mkdir(mode=0o700, exist_ok=True)
        self.tmp = Path(tempfile.mkdtemp(prefix="case-", dir=parent))
        self.app = self.tmp / "app"
        self.old = self.payload("old", b"v1")
        self.new = self.payload("new", b"v2")
    def tearDown(self):
        if hasattr(self,"tmp"):
            shutil.rmtree(self.tmp)
    def payload(self, name, data):
        src = self.tmp / ("source-"+name)
        src.mkdir()
        (src / "chatgpt-cef-v2").write_bytes(b"#!/bin/sh\nprintf '" + data + b"\\n'\n")
        (src / "chatgpt-cef-v2").chmod(0o700)
        (src / "chrome-sandbox").symlink_to(TRUSTED.resolve())
        return src
    def invoke(self, source=None, version="0.8.3-rc.2", fault="None", root=None, trusted=None):
        return subprocess.run([sys.executable, str(SCRIPT), "--source", str(source or self.old),
            "--install-root", str(root or self.app), "--trusted-sandbox", str(trusted or TRUSTED),
            "--version", version, "--fault", fault], capture_output=True, text=True, timeout=18)
    def good(self,p):
        self.assertEqual(p.returncode,0,p.stderr)
        return json.loads(p.stdout)
    def target(self):
        return os.readlink(self.app / "current")
    def test_initial_then_side_by_side_and_rollback_pointer(self):
        base=self.good(self.invoke(version="0.8.1"))
        old=self.target()
        self.assertFalse(base["automaticUpdate"])
        next=self.good(self.invoke(source=self.new,version="0.8.3-rc.2"))
        self.assertNotEqual(old,self.target())
        self.assertEqual(next["previous"],old)
        alias=list(self.app.glob("current.rollback-*"))
        self.assertEqual(len(alias),1)
        self.assertEqual(os.readlink(alias[0]),old)
        self.assertTrue((self.app/"current"/"launch.sh").is_file())
        self.assertTrue((self.app/"current"/"product-install.json").is_file())
        self.assertEqual(self.good(self.invoke(source=self.new,version="0.8.5"))["version"],"0.8.5")
    def test_two_faults_restore_exact_pointer(self):
        self.good(self.invoke(version="0.8.1"))
        old=self.target()
        r=self.invoke(source=self.new,version="0.8.2",fault="AfterBaselineMove")
        self.assertEqual(r.returncode,2,r.stdout)
        self.assertIn("SWAP_ABORTED_AND_BASELINE_PRESERVED",r.stderr)
        self.assertEqual(old,self.target())
        r=self.invoke(source=self.new,version="0.8.3",fault="AfterCandidateMove")
        self.assertEqual(r.returncode,2,r.stdout)
        self.assertIn("SWAP_ABORTED_AND_BASELINE_PRESERVED",r.stderr)
        self.assertEqual(old,self.target())
    def test_duplicate_and_unowned_pointer_rejected(self):
        self.good(self.invoke())
        old=self.target()
        r=self.invoke()
        self.assertEqual(r.returncode,2)
        self.assertIn("VERSION_OR_SWAP_PATH_ALREADY_EXISTS",r.stderr)
        self.assertEqual(old,self.target())
        other=self.tmp/"other"
        other.mkdir()
        (other/"current").mkdir()
        r=self.invoke(root=other)
        self.assertEqual(r.returncode,2)
        self.assertIn("CURRENT_NOT_SYMLINK",r.stderr)
    def test_bad_sandbox_and_public_fault_injection(self):
        fake=self.tmp/"badhelper"
        fake.write_bytes(b"fake")
        bad=self.invoke(trusted=fake)
        self.assertEqual(bad.returncode,2)
        self.assertIn("SANDBOX_PAYLOAD_HASH_MISMATCH",bad.stderr)
        # Must reject any deliberate fault outside strictly private fixture root.
        with tempfile.TemporaryDirectory() as d:
            outside=self.invoke(root=Path(d)/"app",fault="AfterBaselineMove")
            self.assertEqual(outside.returncode,2)
            self.assertIn("PUBLIC_FAULT_INJECTION_FORBIDDEN",outside.stderr)
    def test_path_escape_and_non_owned_current_rejected(self):
        self.good(self.invoke())
        old=self.target()
        bad=self.invoke(version="../../evil")
        self.assertEqual(bad.returncode,2)
        self.assertIn("INVALID_VERSION",bad.stderr)
        outside=self.tmp/"outside"
        outside.mkdir()
        other=self.tmp/"another"
        other.mkdir()
        (other/"current").symlink_to(outside)
        x=self.invoke(root=other)
        self.assertEqual(x.returncode,2)
        self.assertIn("CURRENT_TARGET_NOT_OWNED",x.stderr)
        self.assertEqual(old,self.target())
    def test_arbitrary_payload_symlink_rejected(self):
        (self.new/"escape").symlink_to(Path("/etc/hosts"))
        x=self.invoke(source=self.new)
        self.assertEqual(x.returncode,2)
        self.assertIn("PAYLOAD_SYMLINK_REFUSED",x.stderr)
    def test_concurrent_install_lock_rejected(self):
        import fcntl
        self.app.mkdir()
        with (self.app / ".swap.lock").open("a+b") as handle:
            (self.app / ".swap.lock").chmod(0o600)
            fcntl.flock(handle,fcntl.LOCK_EX|fcntl.LOCK_NB)
            x=self.invoke()
            self.assertEqual(x.returncode,2)
            self.assertIn("CONCURRENT_INSTALL_LOCKED",x.stderr)
    def test_world_writable_root_rejected(self):
        self.app.mkdir(mode=0o700)
        self.app.chmod(0o777)
        x=self.invoke()
        self.assertEqual(x.returncode,2)
        self.assertIn("UNSAFE_INSTALL_ROOT",x.stderr)
    def test_malicious_lock_symlink_rejected(self):
        self.app.mkdir()
        protected=self.tmp/"unrelated"
        protected.write_text("never change")
        (self.app/".swap.lock").symlink_to(protected)
        bad=self.invoke()
        self.assertEqual(bad.returncode,2)
        self.assertIn("LOCK_SYMLINK_REFUSED",bad.stderr)
        self.assertEqual(protected.read_text(),"never change")
    def test_interrupted_swap_recovery_with_unique_pending_marker(self):
        self.good(self.invoke(version="0.8.1"))
        old=self.target()
        (self.app/"current.pending-qa1").symlink_to(old)
        (self.app/"current").replace(self.app/"current.rollback-qa1")
        self.assertFalse((self.app/"current").exists())
        result=self.good(self.invoke(source=self.new,version="0.8.6"))
        self.assertEqual(result["previous"], old)
        self.assertNotEqual(self.target(),old)
        self.assertFalse((self.app/"current.rollback-qa1").exists())
        self.assertTrue((self.app/old).is_dir())
    def test_interrupted_swap_ambiguous_markers_fail_closed(self):
        self.good(self.invoke(version="0.8.1"))
        old=self.target()
        (self.app/"current.pending-one").symlink_to(old)
        (self.app/"current.pending-two").symlink_to(old)
        (self.app/"current").replace(self.app/"current.rollback-one")
        result=self.invoke(source=self.new,version="0.8.6")
        self.assertEqual(result.returncode,2)
        self.assertIn("INTERRUPTED_SWAP_AMBIGUOUS",result.stderr)
        self.assertFalse(os.path.lexists(self.app/"current"))
        self.assertEqual(os.readlink(self.app/"current.rollback-one"),old)
    def test_interrupted_swap_untrusted_rollback_rejected(self):
        self.good(self.invoke(version="0.8.1"))
        old=self.target()
        (self.app/"current.pending-qa2").symlink_to(old)
        (self.app/"current").replace(self.app/"current.rollback-qa2")
        (self.app/"current.rollback-qa2").unlink()
        (self.app/"current.rollback-qa2").symlink_to("../../../../etc")
        bad=self.invoke(source=self.new,version="0.8.6")
        self.assertEqual(bad.returncode,2)
        self.assertIn("INTERRUPTED_SWAP_INVALID_TARGET",bad.stderr)
        self.assertFalse(os.path.lexists(self.app/"current"))
    def test_interrupted_first_install_no_baseline_refused(self):
        self.app.mkdir()
        (self.app/"current.pending-first").symlink_to("v0.8.1-missing")
        bad=self.invoke(version="0.8.1")
        self.assertEqual(bad.returncode,2)
        self.assertIn("INTERRUPTED_SWAP_NO_RECOVERABLE_BASELINE",bad.stderr)
        self.assertFalse(os.path.lexists(self.app/"current"))
    def test_launcher_and_no_credential_access(self):
        self.good(self.invoke())
        launcher=self.app/"current"/"launch.sh"
        p=subprocess.run(["/bin/sh",str(launcher)],capture_output=True,text=True,timeout=8)
        self.assertEqual(p.returncode,0,p.stderr)
        self.assertIn("v1",p.stdout)
        self.assertNotIn("auto_update",launcher.read_text())
if __name__=="__main__":
    unittest.main(verbosity=2)
