"""Synthetic analysis fixtures only; these are never evidence of actual UE FPS."""

import copy
import csv
import json
from pathlib import Path
import tempfile
import unittest

from analyze_performance import FRAME_METRIC, HEADER, STATE_METRIC, analyze, load_capture, metrics


def fixture(duration=1.0, mode="pie", frame_ms=10.0, state_ms=10.0):
    settings = {"width": 1920, "height": 1080, "max_fps": 0, "vsync": 0,
                "fixed_frame_rate": False, "fixed_time_step": False, "smooth_frame_rate": False}
    rows = []
    for kind, interval in (("frame", frame_ms), ("state_interval", state_ms)):
        for sequence in range(1, int(duration * 1000 / interval) + 1):
            rows.append({"kind": kind, "elapsed_s": sequence * interval / 1000,
                         "value_ms": interval, "sequence": str(sequence)})
    rows.sort(key=lambda row: row["elapsed_s"])
    metadata = {"schema_version": 1, "frame_metric": FRAME_METRIC, "state_metric": STATE_METRIC,
                "mode": mode, "map": "SyntheticFixture_NOT_ACTUAL_MEASUREMENT",
                "input_first_change": "not_measured", "manual_driving_acceptance": "pending_manual_review",
                "rendering_enabled": True, "settings_captured": True, "settings_stable": True,
                "initial_settings": settings, "final_settings": copy.deepcopy(settings),
                "warmup_seconds": 10, "requested_duration_seconds": duration, "actual_duration_seconds": duration,
                "completion_reason": "duration_reached", "frame_samples": sum(r["kind"] == "frame" for r in rows),
                "state_interval_samples": sum(r["kind"] == "state_interval" for r in rows)}
    return metadata, rows


class PerformanceAnalysisTest(unittest.TestCase):
    def test_smoke_never_claims_packaged_gate(self):
        result = analyze(*fixture())
        self.assertEqual(result["frame"]["average_fps"], 100)
        self.assertTrue(result["nfr_001"]["targets_met"])
        self.assertFalse(result["nfr_001"]["measurement_gate_met"])
        self.assertEqual(result["input_first_change"]["status"], "not_measured")
        self.assertEqual(result["release_acceptance"], "not_established")

    def test_average_fps_is_reciprocal_mean_duration(self):
        result = metrics([{"elapsed_s": .01, "value_ms": 10}, {"elapsed_s": .04, "value_ms": 30}], frame=True)
        self.assertEqual(result["average_fps"], 50)
        self.assertEqual(result["p95_ms"], 30)

    def test_synthetic_30min_measures_only_not_manual_acceptance(self):
        result = analyze(*fixture(1800, "packaged"))
        self.assertTrue(result["nfr_001"]["measurement_gate_met"])
        self.assertTrue(result["nfr_012_state_interval"]["measurement_gate_met"])
        self.assertEqual(result["manual_driving_and_stability"], "pending_manual_review")

    def test_bad_settings_and_short_duration_exclude(self):
        for change in ({"width": 1280}, {"max_fps": 60}, {"vsync": 1}, {"fixed_frame_rate": True},
                       {"fixed_time_step": True}, {"smooth_frame_rate": True}):
            metadata, rows = fixture(mode="packaged")
            metadata["initial_settings"].update(change)
            metadata["final_settings"].update(change)
            result = analyze(metadata, rows)
            self.assertFalse(result["nfr_001"]["measurement_gate_met"])
            self.assertGreaterEqual(len(result["exclusion_reasons"]), 2)

    def test_settings_changed_then_restored_still_exclude(self):
        metadata, rows = fixture()
        metadata["settings_stable"] = False
        result = analyze(metadata, rows)
        self.assertTrue(any("changed" in reason for reason in result["exclusion_reasons"]))

    def test_no_states_is_not_a_success(self):
        metadata, rows = fixture()
        rows = [row for row in rows if row["kind"] == "frame"]
        metadata["state_interval_samples"] = 0
        result = analyze(metadata, rows)
        self.assertIsNone(result["state_interval"])
        self.assertFalse(result["nfr_012_state_interval"]["targets_met"])

    def test_late_stream_dropout_fails_coverage(self):
        metadata, rows = fixture()
        rows = [row for row in rows if row["kind"] == "frame" or row["elapsed_s"] < .5]
        metadata["state_interval_samples"] = sum(row["kind"] == "state_interval" for row in rows)
        result = analyze(metadata, rows)
        self.assertTrue(result["nfr_012_state_interval"]["targets_met"])
        self.assertFalse(result["nfr_012_state_interval"]["full_window_coverage"])

    def test_late_hitch_does_not_excuse_missing_start(self):
        metadata, _ = fixture()
        rows = [{"kind": "frame", "elapsed_s": 0.3, "value_ms": 10, "sequence": "1"},
                {"kind": "frame", "elapsed_s": 1.0, "value_ms": 700, "sequence": "2"}]
        metadata["frame_samples"] = 2
        metadata["state_interval_samples"] = 0
        result = analyze(metadata, rows)
        self.assertIn("frame samples do not cover the measurement window", result["exclusion_reasons"])

    def test_nan_infinity_negative_zero_frame_and_warmup_crossing_rejected(self):
        for bad in ("NaN", "inf", "-1", "0", "100000"):
            metadata, rows = fixture()
            rows[0]["value_ms"] = bad
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                analyze(metadata, rows)

    def test_truncated_missing_or_duplicate_frame_rejected(self):
        metadata, rows = fixture()
        for changed in (rows[:-1], rows[2:], rows[:3] + rows[4:], [rows[0]] + rows):
            with self.assertRaises(ValueError):
                analyze(metadata, changed)

    def test_empty_or_unknown_rows_rejected(self):
        metadata, rows = fixture()
        with self.assertRaises(ValueError):
            analyze(metadata, [])
        rows[0]["kind"] = "server_tick"
        with self.assertRaises(ValueError):
            analyze(metadata, rows)

    def test_nonfinite_metadata_and_fabricated_input_metric_rejected(self):
        for key, value in (("actual_duration_seconds", float("nan")), ("settings_stable", "true"),
                           ("input_first_change", "passed")):
            metadata, rows = fixture()
            metadata[key] = value
            with self.assertRaises(ValueError):
                analyze(metadata, rows)

    def test_round_trip_and_missing_newline_or_metadata_rejected(self):
        metadata, rows = fixture()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.csv"
            path.with_suffix(".json").write_text(json.dumps(metadata), encoding="utf-8")
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=HEADER)
                writer.writeheader()
                writer.writerows(rows)
            self.assertEqual(load_capture(path)["frame"]["samples"], 100)
            path.write_bytes(path.read_bytes().rstrip(b"\r\n"))
            with self.assertRaisesRegex(ValueError, "newline"):
                load_capture(path)
            path.with_suffix(".json").unlink()
            with self.assertRaises(FileNotFoundError):
                load_capture(path)

    def test_nonfinite_and_duplicate_json_are_rejected_before_analysis(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.csv"
            for text in ('{"map": NaN}', '{"schema_version": 1, "schema_version": 1}'):
                path.with_suffix(".json").write_text(text, encoding="utf-8")
                with self.assertRaises(ValueError):
                    load_capture(path)


if __name__ == "__main__":
    unittest.main()
