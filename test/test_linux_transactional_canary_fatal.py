#!/usr/bin/env python3
"""QA-only fatal diagnostics regression on exact PR #128 source; no trusted SUID fixture."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/linux_transactional_install.py"

class FatalDiagnostics(unittest.TestCase):
    def test_success_markers_do_not_override_fatal_diagnostics(self):
        spec = importlib.util.spec_from_file_location("txn_r105", SCRIPT)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        markers = ("CGWA_SELFTEST PASS\nCGWA_LAST_TAB_SELFTEST PASS\n"
                   "CGWA_UI_TAB_CLOSE_SELFTEST PASS\n")
        cases = [
            ("clean", "", "", "", False),
            ("segfault-stderr", "", "Segmentation fault (core dumped)\n", "", True),
            ("aborted-stderr", "", "Aborted (core dumped)\n", "", True),
            ("fatal-stdout", "FATAL child shutdown\n", "", "", True),
            ("fatal-chrome-log", "", "", "SIGSEGV in CEF subprocess\n", True),
        ]
        for key, output, err, log, fatal in cases:
            with self.subTest(case=key), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                dest = root / "binary"
                dest.mkdir()
                script = dest / "launch.sh"
                body = ("#!/bin/sh\n" + "cat >&2 <<'STDERR'\n" +
                    markers + err + "STDERR\n" +
                    "cat <<'STDOUT'\n" + output + "STDOUT\n")
                if log:
                    body += ("cat > \"$PWD/chrome_debug.log\" <<'CHROMELOG'\n" +
                        log + "CHROMELOG\n")
                script.write_text(body + "exit 0\n")
                script.chmod(0o700)
                if fatal:
                    with self.assertRaisesRegex(mod.InstallError, "QA_CANARY_FATAL"):
                        mod.qa_canary_check(root, dest, key, 3)
                else:
                    mod.qa_canary_check(root, dest, key, 3)
                self.assertFalse((root / (".qa-canary-" + key)).exists())

    def test_untrusted_chromium_log_symlink_fails_closed(self):
        spec = importlib.util.spec_from_file_location("txn_r105b", SCRIPT)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            dest = root / "binary"
            dest.mkdir()
            script = dest / "launch.sh"
            script.write_text("#!/bin/sh\n"
                "printf 'CGWA_SELFTEST PASS\\nCGWA_LAST_TAB_SELFTEST PASS\\n"
                "CGWA_UI_TAB_CLOSE_SELFTEST PASS\\n' >&2\n"
                "ln -s /etc/passwd \"$PWD/chrome_debug.log\"\nexit 0\n")
            script.chmod(0o700)
            with self.assertRaises(OSError):
                mod.qa_canary_check(root, dest, "unsafe-log", 3)
            self.assertFalse((root / ".qa-canary-unsafe-log").exists())

    def test_oversized_output_is_rejected_before_unbounded_capture(self):
        spec = importlib.util.spec_from_file_location("txn_capture", SCRIPT)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for channel in ("stdout", "stderr"):
            with self.subTest(channel=channel), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                dest = root / "candidate"
                dest.mkdir()
                script = dest / "launch.sh"
                stream = "" if channel == "stdout" else " >&2"
                script.write_text(
                    "#!/bin/sh\n"
                    "printf 'CGWA_SELFTEST PASS\\nCGWA_LAST_TAB_SELFTEST PASS\\n"
                    "CGWA_UI_TAB_CLOSE_SELFTEST PASS\\n' >&2\n"
                    "head -c 8388609 /dev/zero" + stream + "\nexit 0\n")
                script.chmod(0o700)
                with self.assertRaisesRegex(
                        mod.InstallError, "QA_CANARY_OUTPUT_TOO_LARGE"):
                    mod.qa_canary_check(root, dest, "oversized-" + channel, 5)
                self.assertFalse(
                    (root / (".qa-canary-oversized-" + channel)).exists())

if __name__ == "__main__":
    unittest.main(verbosity=2)
