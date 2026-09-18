"""Synthetic temporary fixtures only: no recordings or real measurements are generated."""

import contextlib
import copy
import csv
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from analyze_input_latency import ACTIONS, CONDITIONS, HEADER, METRIC, PACKAGE_EXECUTABLES, analyze, load_capture, main, sha256


class InputLatencyAnalysisTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="SYNTHETIC_input_latency_")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        package = self.root / "SYNTHETIC_PACKAGE"
        package.mkdir()
        manifest = package / "package_manifest.json"
        manifest.write_text(json.dumps({"format_version": 1, "created_utc": "SYNTHETIC_NOT_A_BUILD",
                                       "client_configuration": "Development", "server_configuration": "Release",
                                       "map_id": "SYNTHETIC_NOT_A_MAP", "status": "assembled_unverified",
                                       "manual_acceptance": "not_run"}), encoding="utf-8")
        package_files = {}
        for role in ("client_executable", "server_executable"):
            path = package / f"SYNTHETIC_{role}.exe"
            path.write_bytes(b"SYNTHETIC TEST FIXTURE, NOT AN EXECUTABLE: " + role.encode())
            package_files[role] = {"file": path.name, "sha256": sha256(path)}
        video = self.root / "SYNTHETIC_NOT_A_VIDEO.mp4"
        video.write_bytes(b"SYNTHETIC TEST FIXTURE, NOT A VIDEO")
        self.metadata = {
            "schema_version": 1, "metric": METRIC, "mode": "packaged", "evidence_kind": "synthetic_fixture",
            "operator": "SYNTHETIC UNIT TEST", "captured_at": "NOT AN ACTUAL CAPTURE",
            "test_conditions": "Synthetic data tests validation and arithmetic only.",
            "source_package_manifest": {"file": str(manifest), "sha256": sha256(manifest)},
            "package_files": package_files,
            "videos": [{"file": video.name, "sha256": sha256(video), "capture_fps": 120, "frame_count": 4000}],
            "manual_conditions": {field: True for field in CONDITIONS},
            "attempts_per_action": {action: 10 for action in ACTIONS},
            "response_criteria": {action: "SYNTHETIC first response criterion" for action in ACTIONS},
        }
        self.rows = []
        for action_index, action in enumerate(ACTIONS):
            for trial in range(1, 11):
                start = action_index * 1000 + trial * 50
                self.rows.append({"action": action, "trial": str(trial), "video_file": video.name,
                                  "input_frame": str(start), "response_frame": str(start + 4), "capture_fps": "120"})

    def result(self):
        return analyze(self.metadata, self.rows, self.root)

    def write_capture(self):
        path = self.root / "SYNTHETIC_CAPTURE.csv"
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=HEADER)
            writer.writeheader()
            writer.writerows(self.rows)
        path.with_suffix(".json").write_text(json.dumps(self.metadata), encoding="utf-8")
        return path

    def write_manifest(self, manifest):
        path = Path(self.metadata["source_package_manifest"]["file"])
        path.write_text(json.dumps(manifest), encoding="utf-8")
        self.metadata["source_package_manifest"]["sha256"] = sha256(path)

    def upgrade_to_v2(self):
        manifest_path = Path(self.metadata["source_package_manifest"]["file"])
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["format_version"] = 2
        manifest["files"] = []
        for role, relative in PACKAGE_EXECUTABLES.items():
            path = manifest_path.parent / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"SYNTHETIC V2 TEST FIXTURE, NOT AN EXECUTABLE: " + role.encode())
            self.metadata["package_files"][role] = {"file": relative, "sha256": sha256(path)}
            manifest["files"].append({"path": relative, "bytes": path.stat().st_size, "sha256": sha256(path)})
        snapshot = manifest_path.parent / "source_snapshot.json"
        snapshot.write_text(json.dumps({"revision": "SYNTHETIC_NOT_A_REVISION", "dirty": True, "files": []}), encoding="utf-8")
        manifest["source_snapshot"] = snapshot.name
        manifest["source_snapshot_sha256"] = sha256(snapshot)
        manifest["files"].append({"path": snapshot.name, "bytes": snapshot.stat().st_size, "sha256": sha256(snapshot)})
        self.write_manifest(manifest)
        return manifest

    def test_upper_bound_includes_one_frame_at_each_end(self):
        result = self.result()
        self.assertEqual(result["samples"][0]["conservative_upper_ms"], 50)
        self.assertAlmostEqual(result["samples"][0]["observed_ms"], 1000 / 30)
        self.assertTrue(result["target_met"])
        self.assertFalse(result["measurement_gate_met"])
        self.assertIn("synthetic fixture is not actual measurement evidence", result["exclusion_reasons"])
        self.assertEqual(result["release_acceptance"], "not_established")
        self.assertEqual(result["server_applied_ack_latency"], "not_measured")

    def test_manual_evidence_gate_logic_only_with_in_memory_synthetic_fixture(self):
        # Exercise the manual-evidence branch only in memory; persist no passing fake evidence.
        metadata = copy.deepcopy(self.metadata)
        metadata["evidence_kind"] = "manual_video"
        result = analyze(metadata, self.rows, self.root)
        self.assertTrue(result["measurement_gate_met"])
        self.assertIn("require human review", result["evidence_review"])

    def test_single_slow_sample_fails_despite_other_fast_samples(self):
        self.rows[0]["response_frame"] = str(int(self.rows[0]["input_frame"]) + 5)
        result = self.result()
        self.assertFalse(result["target_met"])
        self.assertGreater(result["actions"]["accelerate"]["max_conservative_upper_ms"], 50)

    def test_missing_response_is_counted_as_failure_not_dropped(self):
        self.rows[0]["response_frame"] = ""
        result = self.result()
        self.assertFalse(result["target_met"])
        self.assertEqual(len(result["samples"]), 40)
        self.assertEqual(result["actions"]["accelerate"]["attempts"], 10)
        self.assertEqual(result["actions"]["accelerate"]["measured_samples"], 9)
        self.assertEqual(result["actions"]["accelerate"]["missing_responses"], 1)
        self.assertIsNone(result["samples"][0]["conservative_upper_ms"])

    def test_all_missing_responses_never_produce_success_or_nan(self):
        for row in self.rows:
            row["response_frame"] = ""
        result = self.result()
        self.assertFalse(result["target_met"])
        self.assertIsNone(result["actions"]["brake"]["max_conservative_upper_ms"])
        json.dumps(result, allow_nan=False)

    def test_nine_attempts_fail_minimum(self):
        self.rows = [row for row in self.rows if not (row["action"] == "brake" and row["trial"] == "10")]
        self.metadata["attempts_per_action"]["brake"] = 9
        self.assertIn("brake: fewer than 10 attempts", self.result()["exclusion_reasons"])

    def test_capture_rate_must_be_at_least_120(self):
        self.metadata["videos"][0]["capture_fps"] = 60
        for row in self.rows:
            row["capture_fps"] = "60"
        self.assertTrue(any("below 120" in reason for reason in self.result()["exclusion_reasons"]))

    def test_240fps_uses_capture_not_playback_rate(self):
        self.metadata["videos"][0]["capture_fps"] = 240
        for row in self.rows:
            row["capture_fps"] = "240"
            row["response_frame"] = str(int(row["input_frame"]) + 10)
        self.assertEqual(self.result()["samples"][0]["conservative_upper_ms"], 50)

    def test_nonfinite_or_nonpositive_fps_rejected(self):
        for invalid in ("nan", "inf", "-inf", "0", "-1", "1e999", True, "bad"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.rows[0]["capture_fps"] = invalid
                self.result()

    def test_frames_and_trials_require_nonnegative_integers(self):
        original = copy.deepcopy(self.rows)
        for field in ("trial", "input_frame", "response_frame"):
            for invalid in ("nan", "inf", "2.5", "2.0", "-1", True, None, str(2**63)):
                self.rows = copy.deepcopy(original)
                self.rows[0][field] = invalid
                with self.subTest(field=field, invalid=invalid), self.assertRaises(ValueError):
                    self.result()

    def test_response_before_input_or_outside_video_rejected(self):
        for response in ("49", "4000"):
            self.rows[0]["response_frame"] = response
            with self.assertRaises(ValueError):
                self.result()

    def test_mismatched_rate_or_unknown_video_or_action_rejected(self):
        for field, value in (("capture_fps", "240"), ("video_file", "missing.mp4"), ("action", "handbrake")):
            original = self.rows[0][field]
            self.rows[0][field] = value
            with self.assertRaises(ValueError):
                self.result()
            self.rows[0][field] = original

    def test_duplicate_trial_reused_frame_and_missing_trial_rejected(self):
        original = copy.deepcopy(self.rows)
        for change in ("duplicate_trial", "reused_frame", "missing_trial", "missing_last_attempt"):
            self.rows = copy.deepcopy(original)
            if change == "duplicate_trial":
                self.rows[1]["trial"] = "1"
            elif change == "reused_frame":
                self.rows[10]["input_frame"] = self.rows[0]["input_frame"]
            elif change == "missing_trial":
                self.rows[1]["trial"] = "11"
            else:
                self.rows.pop()
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.result()

    def test_overlapping_attempts_rejected(self):
        self.rows[0]["response_frame"] = self.rows[1]["input_frame"]
        with self.assertRaisesRegex(ValueError, "overlapping"):
            self.result()

    def test_manual_condition_false_excludes_and_missing_condition_rejects(self):
        self.metadata["manual_conditions"]["all_attempts_recorded"] = False
        self.assertIn("manual condition not established: all_attempts_recorded", self.result()["exclusion_reasons"])
        del self.metadata["manual_conditions"]["all_attempts_recorded"]
        with self.assertRaises(ValueError):
            self.result()

    def test_missing_video_or_wrong_hash_rejected(self):
        self.metadata["videos"][0]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.result()
        self.metadata["videos"][0]["file"] = "absent.mp4"
        with self.assertRaises(OSError):
            self.result()

    def test_package_manifest_and_actual_executables_hashed(self):
        result = self.result()
        self.assertEqual(result["source_package_manifest"]["sha256"], self.metadata["source_package_manifest"]["sha256"])
        for role in self.metadata["package_files"]:
            self.assertEqual(result["package_files"][role]["sha256"], self.metadata["package_files"][role]["sha256"])
        self.metadata["package_files"]["server_executable"]["sha256"] = "f" * 64
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.result()

    def test_source_manifest_hash_mismatch_rejected(self):
        self.metadata["source_package_manifest"]["sha256"] = "a" * 64
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.result()

    def test_v1_compatibility_explicitly_reports_missing_inventory(self):
        self.assertEqual(self.result()["package_inventory_cross_check"], "unavailable_in_manifest_v1")

    def test_v2_matches_manifest_inventory_to_measured_executables(self):
        self.upgrade_to_v2()
        result = self.result()
        self.assertEqual(result["package_manifest"]["format_version"], 2)
        self.assertEqual(result["package_inventory_cross_check"], "client_and_server_bytes_and_sha256_verified")

    def test_v2_rejects_manifest_hash_or_size_disagreeing_with_actual_executable(self):
        manifest = self.upgrade_to_v2()
        for field, wrong in (("sha256", "0" * 64), ("bytes", 1)):
            changed = copy.deepcopy(manifest)
            changed["files"][0][field] = wrong
            self.write_manifest(changed)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "inventory differs"):
                self.result()

    def test_v2_rejects_changed_executable_even_when_metadata_hash_is_updated(self):
        self.upgrade_to_v2()
        manifest_path = Path(self.metadata["source_package_manifest"]["file"])
        entry = self.metadata["package_files"]["server_executable"]
        path = manifest_path.parent / entry["file"]
        path.write_bytes(b"SYNTHETIC CHANGED SERVER")
        entry["sha256"] = sha256(path)
        with self.assertRaisesRegex(ValueError, "inventory differs"):
            self.result()

    def test_v2_rejects_missing_or_duplicate_executable_inventory_entry(self):
        manifest = self.upgrade_to_v2()
        for files in (manifest["files"][1:], manifest["files"] + [manifest["files"][0]]):
            changed = copy.deepcopy(manifest)
            changed["files"] = files
            self.write_manifest(changed)
            with self.assertRaises(ValueError):
                self.result()

    def test_v2_rejects_wrong_executable_role(self):
        self.upgrade_to_v2()
        entries = self.metadata["package_files"]
        entries["client_executable"], entries["server_executable"] = entries["server_executable"], entries["client_executable"]
        with self.assertRaisesRegex(ValueError, "must identify"):
            self.result()

    def test_v2_rejects_malformed_inventory(self):
        manifest = self.upgrade_to_v2()
        for malformed in (None, [], [{"path": "../outside.exe", "bytes": 1, "sha256": "0" * 64}],
                          [{"path": "some.exe", "bytes": float("inf"), "sha256": "0" * 64}],
                          [{"path": "some.exe", "bytes": 1, "sha256": "bad"}]):
            changed = copy.deepcopy(manifest)
            changed["files"] = malformed
            self.write_manifest(changed)
            with self.assertRaises(ValueError):
                self.result()

    def test_source_package_must_contain_distinct_executable_files(self):
        original = copy.deepcopy(self.metadata["package_files"])
        self.metadata["package_files"]["server_executable"] = copy.deepcopy(original["client_executable"])
        with self.assertRaisesRegex(ValueError, "distinct executables"):
            self.result()
        outside = self.root / "SYNTHETIC_OUTSIDE_PACKAGE.exe"
        outside.write_bytes(b"SYNTHETIC TEST, NOT AN EXECUTABLE")
        self.metadata["package_files"]["server_executable"] = {"file": str(outside), "sha256": sha256(outside)}
        with self.assertRaisesRegex(ValueError, "inside the source package"):
            self.result()

    def test_missing_executable_and_empty_video_rejected(self):
        role = self.metadata["package_files"]["server_executable"]
        role["file"] = "absent.exe"
        with self.assertRaises(OSError):
            self.result()
        role["file"] = "SYNTHETIC_server_executable.exe"
        (self.root / self.metadata["videos"][0]["file"]).write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "nonempty file"):
            self.result()

    def test_packaged_mode_and_metric_are_required(self):
        for field, value in (("mode", "pie"), ("metric", "server_applied_ack_ms")):
            original = self.metadata[field]
            self.metadata[field] = value
            with self.assertRaises(ValueError):
                self.result()
            self.metadata[field] = original

    def test_video_metadata_fps_and_frame_count_are_validated(self):
        for field, value in (("capture_fps", float("nan")), ("capture_fps", float("inf")),
                             ("frame_count", 0), ("frame_count", 3.5)):
            original = self.metadata["videos"][0][field]
            self.metadata["videos"][0][field] = value
            with self.assertRaises(ValueError):
                self.result()
            self.metadata["videos"][0][field] = original

    def test_duplicate_video_alias_rejected(self):
        record = copy.deepcopy(self.metadata["videos"][0])
        record["file"] = "./" + record["file"]
        self.metadata["videos"].append(record)
        with self.assertRaisesRegex(ValueError, "duplicate video"):
            self.result()

    def test_csv_roundtrip_and_cli_exit_codes(self):
        path = self.write_capture()
        result = load_capture(path)
        self.assertEqual(result["raw_csv"]["sha256"], sha256(path))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main([str(path)]), 0)
            self.assertEqual(main([str(path), "--require-measurement-gate"]), 1)
        with patch("analyze_input_latency.load_capture", return_value={"measurement_gate_met": True}), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(main([str(path), "--require-measurement-gate"]), 0)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main([str(self.root / "absent.csv")]), 2)

    def test_truncated_csv_header_and_json_duplicate_or_nonfinite_rejected(self):
        path = self.write_capture()
        content = path.read_bytes()
        for corrupt in (content.rstrip(b"\r\n"), content.replace(b"input_frame", b"input_timestamp")):
            path.write_bytes(corrupt)
            with self.assertRaises(ValueError):
                load_capture(path)
        path.write_bytes(content)
        for corrupt in ('{"schema_version": 1, "schema_version": 1}', '{"schema_version": NaN}'):
            path.with_suffix(".json").write_text(corrupt, encoding="utf-8")
            with self.assertRaises(ValueError):
                load_capture(path)


if __name__ == "__main__":
    unittest.main()
