import tempfile
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch
import subprocess

from check_core import ROOT, Step, child_environment, execute_step, git_metadata, make_steps, terminate_child, verify_automation, verify_ctest


class CoreCheckTests(unittest.TestCase):
    def test_generated_proto_check_uses_the_standalone_tools_directory(self):
        steps = make_steps(None, Path("reports"), skip_build=True, skip_unreal=True)
        proto = next(step for step in steps if step.name == "generated-proto")
        self.assertEqual(proto.command,
                         [sys.executable, str(ROOT / "scripts/check_generated_proto.py"), "-v"])
        self.assertEqual(proto.cwd, ROOT)
        self.assertNotIn("python.relay_server", " ".join(proto.command))

    def test_unreal_build_logs_stay_in_the_report_directory(self):
        output = Path('reports') / 'one-run'
        with patch('check_core.unreal_tools', return_value=(Path('dotnet'), Path('ubt'), Path('editor'))):
            steps = make_steps(Path('engine'), output)
        builds = [step for step in steps if step.name.startswith('ue-build-')]
        self.assertEqual(len(builds), 2)
        for step in builds:
            log_options = [argument for argument in step.command if argument.startswith('-Log=')]
            self.assertEqual(len(log_options), 1)
            self.assertEqual(Path(log_options[0][5:]).parent, output)
        automation = next(step for step in steps if step.name == 'ue-automation')
        self.assertIn('-DDC=InstalledNoZenLocalFallback', automation.command)
        self.assertEqual(Path(child_environment()['UE-LocalDataCachePath']), ROOT / 'runtime_tmp/ue-ddc')

    def test_git_failure_is_only_missing_metadata(self):
        for failure in (OSError("missing git"), subprocess.TimeoutExpired("git", 10)):
            with self.subTest(failure=failure), patch("check_core.subprocess.run", side_effect=failure):
                self.assertEqual(git_metadata(), {"git_commit": "unavailable", "worktree_dirty": None})

    def test_taskkill_failure_still_kills_owned_child(self):
        child = Mock(pid=12345)
        child.poll.return_value = None
        child.wait.side_effect = [subprocess.TimeoutExpired("owned child", 10), 1]
        with patch("check_core.os.name", "nt"), patch("check_core.subprocess.run", side_effect=OSError("taskkill failed")), \
                patch("check_core.subprocess.CREATE_NO_WINDOW", 0, create=True):
            terminate_child(child)
        child.kill.assert_called_once_with()
        self.assertEqual(child.wait.call_count, 2)

    def test_automation_requires_all_discovered_tests(self):
        prefix = "Found 2 automation tests based on 'DriveIntegration'\n"
        first = "Test Completed. Result={Success} Name={One} Path={DriveIntegration.One}\n"
        second = "Test Completed. Result={Success} Name={Two} Path={DriveIntegration.Two}\n"
        self.assertEqual(verify_automation(prefix + first + second), {"passed": 2})
        for invalid in ("", prefix, prefix + first, prefix + first + first,
                        prefix + first + second.replace("Success", "Fail"),
                        "Found 0 automation tests based on 'DriveIntegration'"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                verify_automation(invalid)

    def test_ctest_rejects_empty_failures_and_skips(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ctest.xml"
            path.write_text('<testsuite><testcase name="one"/></testsuite>')
            self.assertEqual(verify_ctest(path), {"passed": 1})
            for body in ("", '<testcase><failure/></testcase>',
                         '<testcase><error/></testcase>', '<testcase><skipped/></testcase>'):
                path.write_text("<testsuite>" + body + "</testsuite>")
                with self.subTest(body=body), self.assertRaises(ValueError):
                    verify_ctest(path)

    def test_step_success_failure_and_missing_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            for code, expected in ((0, "passed"), (7, "failed")):
                step = Step("exit-" + str(code), [sys.executable, "-c", f"raise SystemExit({code})"], output)
                result = execute_step(step, output)
                self.assertEqual(result["exit_code"], code)
                self.assertEqual(result["status"], expected)
                self.assertTrue(Path(result["log"]).is_file())
            result = execute_step(Step("missing", [str(output / "absent.exe")], output), output)
            self.assertEqual(result["status"], "failed")
            self.assertIn("error", result)

    def test_zero_exit_does_not_hide_missing_automation(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            step = Step("empty-automation", [sys.executable, "-c", "pass"], output,
                        verify="automation")
            result = execute_step(step, output)
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["exit_code"], 0)


if __name__ == "__main__":
    unittest.main()
