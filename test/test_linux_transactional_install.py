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
        (src / "chatgpt-cef-v2").write_bytes(
            (src / "chatgpt-cef-v2").read_bytes() +
            b"printf 'CGWA_SELFTEST PASS\\nCGWA_LAST_TAB_SELFTEST PASS\\nCGWA_UI_TAB_CLOSE_SELFTEST PASS\\n' >&2\n")
        (src / "chatgpt-cef-v2").chmod(0o700)
        (src / "chrome-sandbox").symlink_to(TRUSTED.resolve())
        return src
    def invoke(self, source=None, version="0.8.3-rc.2", fault="None", root=None, trusted=None,
               qa_canary=False, canary_timeout=5):
        args=[sys.executable, str(SCRIPT), "--source", str(source or self.old),
              "--install-root", str(root or self.app), "--trusted-sandbox", str(trusted or TRUSTED),
              "--version", version, "--fault", fault]
        if qa_canary:
            args += ["--qa-canary", "--qa-canary-timeout", str(canary_timeout)]
        return subprocess.run(args, capture_output=True, text=True, timeout=18)
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
    def test_unknown_current_version_directory_rejected(self):
        self.good(self.invoke(version="0.8.1"))
        rogue = self.app / "v-unknown-local-dir"
        rogue.mkdir()
        (self.app / "current").unlink()
        (self.app / "current").symlink_to(rogue.name)
        attempt = self.invoke(source=self.new, version="0.8.2")
        self.assertEqual(attempt.returncode, 2, attempt.stdout)
        self.assertIn("CURRENT_TARGET_NOT_OWNED", attempt.stderr)
        self.assertEqual(self.target(), rogue.name)

    def test_current_version_shaped_directory_without_manifest_rejected(self):
        self.good(self.invoke(version="0.8.1"))
        rogue = self.app / "v0.8.2-aaaaaaaaaaaa"
        rogue.mkdir()
        (self.app / "current").unlink()
        (self.app / "current").symlink_to(rogue.name)
        attempt = self.invoke(source=self.new, version="0.8.3")
        self.assertEqual(attempt.returncode, 2, attempt.stdout)
        self.assertIn("CURRENT_TARGET_NOT_OWNED", attempt.stderr)
        self.assertEqual(self.target(), rogue.name)

    def test_current_version_directory_symlink_alias_refused(self):
        self.good(self.invoke(version="0.8.1"))
        valid_version = self.target()
        (self.app / "v-alias").symlink_to(valid_version, target_is_directory=True)
        (self.app / "current").unlink()
        (self.app / "current").symlink_to("v-alias")
        rejected = self.invoke(source=self.new, version="0.8.2")
        self.assertEqual(rejected.returncode, 2, rejected.stdout)
        self.assertIn("CURRENT_TARGET_NOT_OWNED", rejected.stderr)
        self.assertEqual(self.target(), "v-alias")

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
    def test_live_pointer_never_removed_prior_to_atomic_promotion(self):
        source=SCRIPT.read_text()
        self.assertIn("rollback.symlink_to(previous)", source)
        self.assertNotIn("current.replace(rollback)", source)
        self.good(self.invoke(version="0.8.1"))
        old=self.target()
        r=self.invoke(source=self.new,version="0.8.2",fault="AfterBaselineMove")
        self.assertEqual(r.returncode,2)
        self.assertEqual(self.target(),old)
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
    def test_persist_data_and_directory_entries_on_install(self):
        from unittest import mock
        import stat
        spec=importlib.util.spec_from_file_location("linux_transactional_install", SCRIPT)
        module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        calls=[]
        original=os.fsync
        def track(fd):
            calls.append(stat.S_ISDIR(os.fstat(fd).st_mode))
            return original(fd)
        from argparse import Namespace
        with mock.patch("os.fsync", side_effect=track):
            result=module.install(Namespace(source=str(self.old),install_root=str(self.app),
                        trusted_sandbox=str(TRUSTED),version="0.8.1",fault="None"))
        self.assertEqual(result["status"],"PASS")
        self.assertIn(True,calls,"No directory fsync on promotion")
        self.assertIn(False,calls,"No regular file fsync in staged payload")
    def test_promote_directory_fsync_failure_restores_old_pointer(self):
        from unittest import mock
        from argparse import Namespace
        self.good(self.invoke(version="0.8.1"))
        old=self.target()
        spec=importlib.util.spec_from_file_location("linux_transactional_install",SCRIPT)
        module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        original=os.fsync
        def fault_when_new_visible(fd):
            if (self.app/"current").is_symlink() and os.readlink(self.app/"current") != old:
                raise OSError(5,"synthetic fsync failure after promotion")
            return original(fd)
        with mock.patch("os.fsync",side_effect=fault_when_new_visible):
            with self.assertRaises(module.InstallError):
                module.install(Namespace(source=str(self.new),install_root=str(self.app),
                    trusted_sandbox=str(TRUSTED),version="0.8.2",fault="None"))
        self.assertEqual(self.target(),old)
    def run_forced_exit_at_atomic_boundary(self, mode):
        """Separate child process exits without cleanup at precise rename boundary."""
        self.good(self.invoke(version="0.8.1"))
        original = self.target()
        child = r'''
import importlib.util
import os
from pathlib import Path
from argparse import Namespace
script=Path(os.environ["RC_QA_SCRIPT"])
spec=importlib.util.spec_from_file_location("qa_installer", script)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
real_replace=Path.replace
def intercepted(self,target):
    if self.name.startswith("current.pending-") and Path(target).name=="current":
        if os.environ["RC_QA_MODE"]=="before":
            os._exit(72)
        result=real_replace(self,target)
        os._exit(73)
    return real_replace(self,target)
Path.replace=intercepted
module.install(Namespace(source=os.environ["RC_QA_SOURCE"],
    install_root=os.environ["RC_QA_ROOT"],
    trusted_sandbox=os.environ["RC_QA_HELPER"], version="0.8.2", fault="None"))
raise SystemExit(99)
'''
        env=os.environ.copy()
        env.update(RC_QA_SCRIPT=str(SCRIPT), RC_QA_SOURCE=str(self.new),
            RC_QA_ROOT=str(self.app), RC_QA_HELPER=str(TRUSTED),
            RC_QA_MODE=mode)
        child_result=subprocess.run([sys.executable,"-c",child],
            env=env,capture_output=True,text=True,timeout=18)
        self.assertEqual(child_result.returncode,
            72 if mode=="before" else 73,child_result.stderr)
        alias=list(self.app.glob("current.rollback-*"))
        self.assertEqual(len(alias),1)
        self.assertTrue(alias[0].is_symlink())
        self.assertEqual(os.readlink(alias[0]),original)
        if mode=="before":
            self.assertEqual(self.target(),original)
            self.assertEqual(len(list(self.app.glob("current.pending-*"))),1)
        else:
            self.assertNotEqual(self.target(),original)
            self.assertEqual(len(list(self.app.glob("current.pending-*"))),0)
            alias[0].replace(self.app/"current")
            fd=os.open(self.app,os.O_RDONLY|os.O_DIRECTORY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
            self.assertEqual(self.target(),original)

    def test_runtime_failure_rollback_never_exposes_missing_current(self):
        from unittest import mock
        from argparse import Namespace
        self.good(self.invoke(version="0.8.1"))
        original=self.target()
        spec=importlib.util.spec_from_file_location("qa_installer",SCRIPT)
        module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        observed=[]
        real_replace=Path.replace
        def instrument(self_,target):
            if self_.name.startswith("current.rollback-") and Path(target).name=="current":
                observed.append((self.app/"current").is_symlink())
                observed.append(os.readlink(self.app/"current")!=original)
            return real_replace(self_,target)
        with mock.patch.object(Path,"replace",new=instrument):
            with self.assertRaises(module.InstallError) as ctx:
                module.install(Namespace(source=str(self.new),install_root=str(self.app),
                    trusted_sandbox=str(TRUSTED),version="0.8.2",fault="AfterCandidateMove"))
        self.assertIn("SWAP_ABORTED_AND_BASELINE_PRESERVED",str(ctx.exception))
        self.assertEqual(observed,[True,True],"rollback replaced an absent or stale pointer")
        self.assertEqual(self.target(),original)

    def test_forced_exit_before_pointer_swap_preserves_baseline(self):
        self.run_forced_exit_at_atomic_boundary("before")

    def test_forced_exit_after_pointer_swap_supports_manual_rollback(self):
        self.run_forced_exit_at_atomic_boundary("after")

    def test_qa_canary_ancestor_symlink_cannot_escape_fixture(self):
        with tempfile.TemporaryDirectory() as outside:
            link=self.tmp/"outside-qa-ancestor"
            link.symlink_to(outside,target_is_directory=True)
            target=link/"escaped-install"
            rejected=self.invoke(root=target,qa_canary=True)
            self.assertEqual(rejected.returncode,2)
            self.assertIn("PUBLIC_QA_CANARY_FORBIDDEN",rejected.stderr)
            self.assertFalse((Path(outside)/"escaped-install").exists())
    def test_fault_injection_ancestor_symlink_cannot_escape_fixture(self):
        with tempfile.TemporaryDirectory() as outside:
            link=self.tmp/"outside-fault-ancestor"
            link.symlink_to(outside,target_is_directory=True)
            target=link/"escaped-install"
            rejected=self.invoke(root=target,fault="AfterBaselineMove")
            self.assertEqual(rejected.returncode,2)
            self.assertIn("PUBLIC_FAULT_INJECTION_FORBIDDEN",rejected.stderr)
            self.assertFalse((Path(outside)/"escaped-install").exists())
    def test_resolved_install_root_cannot_be_inside_payload(self):
        alias=self.tmp/"source-root-alias"
        alias.symlink_to(self.new,target_is_directory=True)
        bad=self.invoke(source=self.new,root=alias/"must-not-create")
        self.assertEqual(bad.returncode,2)
        self.assertIn("INVALID_SOURCE_ROOT",bad.stderr)
        self.assertFalse((self.new/"must-not-create").exists())

    def test_private_qa_canary_success_promotes(self):
        self.good(self.invoke(version="0.8.1"))
        old=self.target()
        result=self.good(self.invoke(source=self.new,version="0.8.2",qa_canary=True))
        self.assertNotEqual(old,self.target())
        self.assertEqual(result["previous"],old)
        self.assertEqual(len(list(self.app.glob(".qa-canary-*"))),0)
    def test_qa_canary_zero_exit_without_selftest_evidence_rolls_back(self):
        self.good(self.invoke(version="0.8.1"))
        baseline=self.target()
        (self.new/"chatgpt-cef-v2").write_text("#!/bin/sh\nexit 0\n")
        (self.new/"chatgpt-cef-v2").chmod(0o700)
        result=self.invoke(source=self.new,version="0.8.2",qa_canary=True)
        self.assertEqual(result.returncode,2,result.stdout)
        self.assertIn("QA_CANARY_MISSING_SELFTEST_MARKER",result.stderr)
        self.assertEqual(self.target(),baseline)

    def test_qa_canary_partial_markers_rejected(self):
        self.good(self.invoke(version="0.8.1"))
        baseline=self.target()
        (self.new/"chatgpt-cef-v2").write_text("#!/bin/sh\nprintf 'CGWA_SELFTEST PASS\\n' >&2\nexit 0\n")
        (self.new/"chatgpt-cef-v2").chmod(0o700)
        result=self.invoke(source=self.new,version="0.8.2",qa_canary=True)
        self.assertEqual(result.returncode,2,result.stdout)
        self.assertIn("QA_CANARY_MISSING_SELFTEST_MARKER",result.stderr)
        self.assertEqual(self.target(),baseline)

    def test_qa_canary_nonzero_exit_auto_rolls_back(self):
        self.good(self.invoke(version="0.8.1"))
        old=self.target()
        (self.new/"chatgpt-cef-v2").write_text("#!/bin/sh\nexit 17\n")
        (self.new/"chatgpt-cef-v2").chmod(0o700)
        bad=self.invoke(source=self.new,version="0.8.2",qa_canary=True)
        self.assertEqual(bad.returncode,2,bad.stderr)
        self.assertIn("QA_CANARY_FAILED_EXIT",bad.stderr)
        self.assertEqual(old,self.target())
        self.assertEqual(len(list(self.app.glob(".qa-canary-*"))),0)
    def test_qa_canary_timeout_auto_rolls_back(self):
        self.good(self.invoke(version="0.8.1"))
        old=self.target()
        (self.new/"chatgpt-cef-v2").write_text("#!/bin/sh\nsleep 10\n")
        (self.new/"chatgpt-cef-v2").chmod(0o700)
        bad=self.invoke(source=self.new,version="0.8.2",qa_canary=True,canary_timeout=1)
        self.assertEqual(bad.returncode,2,bad.stderr)
        self.assertIn("QA_CANARY_TIMEOUT",bad.stderr)
        self.assertEqual(old,self.target())
        self.assertEqual(len(list(self.app.glob(".qa-canary-*"))),0)
    def test_public_qa_canary_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            bad=self.invoke(root=Path(d)/"external",qa_canary=True)
            self.assertEqual(bad.returncode,2)
            self.assertIn("PUBLIC_QA_CANARY_FORBIDDEN",bad.stderr)
            self.assertFalse((Path(d)/"external"/"current").exists())
    def test_qa_canary_scrubs_environment_and_profiles(self):
        self.good(self.invoke(version="0.8.1"))
        exe=self.new/"chatgpt-cef-v2"
        exe.write_text('#!/bin/sh\n'
                       'test -z "${RC_QA_SENTINEL:-}" || exit 41\n'
                       'test -d "$HOME" || exit 42\n'
                       'test "$HOME" = "$XDG_CONFIG_HOME" && exit 43\n'
                       'case "$HOME" in *"/.qa-canary-"*) printf "CGWA_SELFTEST PASS\\nCGWA_LAST_TAB_SELFTEST PASS\\nCGWA_UI_TAB_CLOSE_SELFTEST PASS\\n" >&2; exit 0;; *) exit 44;; esac\n')
        exe.chmod(0o700)
        old=os.environ.get("RC_QA_SENTINEL")
        os.environ["RC_QA_SENTINEL"]="SHOULD_NOT_PASS"
        try:
            good=self.invoke(source=self.new,version="0.8.2",qa_canary=True)
        finally:
            if old is None: os.environ.pop("RC_QA_SENTINEL",None)
            else: os.environ["RC_QA_SENTINEL"]=old
        self.good(good)
    def test_qa_canary_success_reaps_descendant_process_group(self):
        # A successful launcher can exit after emitting markers while its
        # detached-from-parent (but same-session) subprocess remains alive.
        import signal
        spec = importlib.util.spec_from_file_location("qa_installer", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        product = self.tmp / "mock-product"
        product.mkdir()
        marker = self.tmp / "descendant.pid"
        launcher = product / "launch.sh"
        launcher.write_text(
            "#!/bin/sh\n"
            "sleep 25 </dev/null >/dev/null 2>&1 &\n"
            f"echo $! > '{marker}'\n"
            'printf "CGWA_SELFTEST PASS\\nCGWA_LAST_TAB_SELFTEST PASS\\nCGWA_UI_TAB_CLOSE_SELFTEST PASS\\n" >&2\n'
            "exit 0\n")
        launcher.chmod(0o700)
        try:
            module.qa_canary_check(self.tmp, product, "survivor-regression", 3)
            pid = int(marker.read_text())
            proc_stat = Path(f"/proc/{pid}/stat")
            if proc_stat.exists():
                state = proc_stat.read_text().split()[2]
                self.assertEqual(state, "Z", "Canary leaked a live descendant")
        finally:
            if marker.exists():
                try:
                    os.kill(int(marker.read_text()), signal.SIGKILL)
                except ProcessLookupError:
                    pass

    def test_launcher_and_no_credential_access(self):
        self.good(self.invoke())
        launcher=self.app/"current"/"launch.sh"
        p=subprocess.run(["/bin/sh",str(launcher)],capture_output=True,text=True,timeout=8)
        self.assertEqual(p.returncode,0,p.stderr)
        self.assertIn("v1",p.stdout)
        self.assertNotIn("auto_update",launcher.read_text())
if __name__=="__main__":
    unittest.main(verbosity=2)
