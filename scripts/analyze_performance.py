"""Analyze opt-in UE performance captures; never substitute server ticks for rendered FPS.

Usage: python scripts/analyze_performance.py <capture.csv> [--require-measurement-gates]
The sibling .json is mandatory. Output is JSON; invalid/incomplete data exits 2.
Smoke analysis is not release acceptance. The optional gate exits 1 unless both
measured targets qualify; input latency and manual 30-minute driving remain pending.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import sys


HEADER = ["kind", "elapsed_s", "value_ms", "sequence"]
FRAME_METRIC = "monotonic_engine_frame_end_interval_ms"
STATE_METRIC = "accepted_world_state_game_thread_interval_ms"


def finite(value, name: str, minimum: float = 0.0) -> float:
    if isinstance(value, bool):
        raise ValueError(f"{name} must be numeric")
    try:
        number = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{name} must be numeric") from exc
    if not math.isfinite(number) or number < minimum:
        raise ValueError(f"{name} must be finite and >= {minimum}")
    return number


def integer(value, name: str, minimum: int = 0) -> int:
    number = finite(value, name, minimum)
    if int(number) != number:
        raise ValueError(f"{name} must be an integer")
    return int(number)


def boolean(value, name: str) -> bool:
    if type(value) is not bool:
        raise ValueError(f"{name} must be boolean")
    return value


def percentile_nearest_rank(values: list[float], fraction: float = 0.95) -> float:
    if not values:
        raise ValueError("percentile requires samples")
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


def metrics(samples: list[dict], *, frame: bool) -> dict | None:
    if not samples:
        return None
    values = [sample["value_ms"] for sample in samples]
    result = {
        "samples": len(values),
        "mean_ms": math.fsum(values) / len(values),
        "p95_ms": percentile_nearest_rank(values),
        "max_ms": max(values),
        "covered_seconds": math.fsum(values) / 1000.0,
        "first_interval_start_seconds": samples[0]["elapsed_s"] - values[0] / 1000.0,
        "last_interval_end_seconds": samples[-1]["elapsed_s"],
    }
    if frame:
        # Reciprocal of mean duration, NOT mean of per-frame reciprocals.
        result["average_fps"] = 1000.0 / result["mean_ms"]
    return result


def validate_settings(settings, name: str) -> dict:
    if not isinstance(settings, dict):
        raise ValueError(f"{name} must be an object")
    return {
        "width": integer(settings.get("width"), f"{name}.width"),
        "height": integer(settings.get("height"), f"{name}.height"),
        "max_fps": finite(settings.get("max_fps"), f"{name}.max_fps", -1.0),
        "vsync": integer(settings.get("vsync"), f"{name}.vsync", -1),
        "fixed_frame_rate": boolean(settings.get("fixed_frame_rate"), f"{name}.fixed_frame_rate"),
        "fixed_time_step": boolean(settings.get("fixed_time_step"), f"{name}.fixed_time_step"),
        "smooth_frame_rate": boolean(settings.get("smooth_frame_rate"), f"{name}.smooth_frame_rate"),
    }


def analyze(metadata: dict, rows: list[dict]) -> dict:
    if (not isinstance(metadata, dict) or type(metadata.get("schema_version")) is not int
            or metadata["schema_version"] != 1):
        raise ValueError("unsupported metadata schema")
    if metadata.get("frame_metric") != FRAME_METRIC or metadata.get("state_metric") != STATE_METRIC:
        raise ValueError("unsupported or missing clock/metric definition")
    duration = finite(metadata.get("actual_duration_seconds"), "actual_duration_seconds")
    requested = finite(metadata.get("requested_duration_seconds"), "requested_duration_seconds", 0.1)
    warmup = finite(metadata.get("warmup_seconds"), "warmup_seconds")
    if requested > 3600 or warmup > 120:
        raise ValueError("capture duration/warmup is outside the supported range")
    if metadata.get("mode") not in ("pie", "editor_game", "packaged", "uncooked_game"):
        raise ValueError("unknown capture mode")
    if metadata.get("completion_reason") not in ("duration_reached", "play_ended", "sample_limit"):
        raise ValueError("unknown completion reason")
    initial = validate_settings(metadata.get("initial_settings"), "initial_settings")
    final = validate_settings(metadata.get("final_settings"), "final_settings")
    settings_stable = boolean(metadata.get("settings_stable"), "settings_stable")
    settings_captured = boolean(metadata.get("settings_captured"), "settings_captured")
    rendering_enabled = boolean(metadata.get("rendering_enabled"), "rendering_enabled")
    if metadata.get("input_first_change") != "not_measured":
        raise ValueError("this schema has no causal input-latency measurement")
    if metadata.get("manual_driving_acceptance") != "pending_manual_review":
        raise ValueError("manual driving acceptance cannot be established by this capture")
    frame_samples, state_samples = [], []
    previous_elapsed = -1.0
    for row_number, row in enumerate(rows, 2):
        if set(row) != set(HEADER) or row.get("kind") not in ("frame", "state_interval"):
            raise ValueError(f"CSV row {row_number}: invalid columns or kind")
        elapsed = finite(row["elapsed_s"], f"row {row_number} elapsed_s")
        value = finite(row["value_ms"], f"row {row_number} value_ms")
        sequence_text = str(row["sequence"])
        if not sequence_text.isascii() or not sequence_text.isdecimal():
            raise ValueError(f"CSV row {row_number}: invalid sequence")
        sequence = int(sequence_text)
        if row["kind"] == "state_interval" and sequence < 1:
            raise ValueError(f"CSV row {row_number}: state sequence must be positive")
        if elapsed < previous_elapsed or elapsed > duration + 0.00001:
            raise ValueError(f"CSV row {row_number}: out-of-order or out-of-window timestamp")
        if elapsed - value / 1000.0 < -0.00001:
            raise ValueError(f"CSV row {row_number}: interval crosses warmup boundary")
        previous_elapsed = elapsed
        samples = frame_samples if row["kind"] == "frame" else state_samples
        if row["kind"] == "frame" and value <= 0.0:
            raise ValueError(f"CSV row {row_number}: frame duration must be positive")
        if samples:
            expected_ms = (elapsed - samples[-1]["elapsed_s"]) * 1000.0
            if not math.isclose(value, expected_ms, rel_tol=1e-6, abs_tol=0.00001):
                raise ValueError(f"CSV row {row_number}: missing sample or inconsistent interval")
            if row["kind"] == "frame" and sequence != samples[-1]["sequence"] + 1:
                raise ValueError(f"CSV row {row_number}: missing or duplicate engine frame")
        samples.append({"elapsed_s": elapsed, "value_ms": value, "sequence": sequence})
    if not frame_samples:
        raise ValueError("capture contains no frame samples")
    if integer(metadata.get("frame_samples"), "frame_samples") != len(frame_samples):
        raise ValueError("frame sample count mismatch (possibly truncated CSV)")
    if integer(metadata.get("state_interval_samples"), "state_interval_samples") != len(state_samples):
        raise ValueError("state sample count mismatch (possibly truncated CSV)")
    frame = metrics(frame_samples, frame=True)
    state = metrics(state_samples, frame=False)
    assert frame is not None

    excluded = []
    if metadata.get("mode") != "packaged":
        excluded.append("not a packaged build")
    if not rendering_enabled:
        excluded.append("rendering disabled or unavailable")
    if not settings_captured or not settings_stable or initial != final:
        excluded.append("capture settings were unavailable or changed during measurement")
    if (initial["width"], initial["height"]) != (1920, 1080):
        excluded.append("viewport resolution is not 1920x1080")
    if (initial["max_fps"] != 0 or initial["vsync"] != 0 or initial["fixed_frame_rate"]
            or initial["fixed_time_step"] or initial["smooth_frame_rate"]):
        excluded.append("uncapped/no-vsync/no-fixed-step/no-smoothing conditions not established")
    if metadata.get("completion_reason") != "duration_reached" or duration < requested:
        excluded.append("requested capture did not complete")
    if requested < 1800.0 or duration < 1800.0:
        excluded.append("less than 30 minutes of measured time")
    # A large hitch elsewhere must not excuse missing data at the window edges.
    if frame["first_interval_start_seconds"] > 0.1 or duration - frame["last_interval_end_seconds"] > 0.1:
        excluded.append("frame samples do not cover the measurement window")
    eligible = not excluded
    frame_targets = frame["average_fps"] >= 60.0 and frame["p95_ms"] <= 18.5
    state_coverage = bool(state and state["first_interval_start_seconds"] <= 0.1
                          and duration - state["last_interval_end_seconds"] <= 0.1)
    state_targets = bool(state and state["p95_ms"] <= 18.5 and state["max_ms"] <= 25.0)
    return {
        "classification": "measurement_conditions_met" if eligible else "smoke_or_ineligible",
        "exclusion_reasons": excluded,
        "mode": metadata.get("mode"),
        "map": metadata.get("map"),
        "warmup_seconds": warmup,
        "measured_seconds": duration,
        "frame_metric": FRAME_METRIC,
        "frame": frame,
        "nfr_001": {"targets_met": frame_targets, "measurement_gate_met": eligible and frame_targets},
        "state_metric": STATE_METRIC,
        "state_interval": state,
        "nfr_012_state_interval": {
            "targets_met": state_targets, "full_window_coverage": state_coverage,
            "measurement_gate_met": eligible and state_coverage and state_targets,
        },
        "input_first_change": {"status": "not_measured", "target_ms": 50.0},
        "manual_driving_and_stability": "pending_manual_review",
        "release_acceptance": "not_established",
    }


def no_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate metadata key: {key}")
        result[key] = value
    return result


def reject_json_constant(value):
    raise ValueError(f"non-finite JSON number: {value}")


def load_capture(csv_path: Path) -> dict:
    metadata = json.loads(csv_path.with_suffix(".json").read_text(encoding="utf-8-sig"),
                          object_pairs_hook=no_duplicate_keys, parse_constant=reject_json_constant)
    with csv_path.open("rb") as stream:
        stream.seek(0, 2)
        if stream.tell() == 0:
            raise ValueError("empty CSV")
        stream.seek(-1, 2)
        if stream.read(1) != b"\n":
            raise ValueError("CSV has no final newline (possibly truncated)")
    with csv_path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, strict=True)
        if reader.fieldnames != HEADER:
            raise ValueError("unsupported CSV header")
        rows = list(reader)
    return analyze(metadata, rows)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="UE Saved/PerformanceCaptures/*.csv; sibling .json required")
    parser.add_argument("--require-measurement-gates", action="store_true",
                        help="exit 1 unless packaged 1080p uncapped 30-minute frame/state measurements qualify")
    args = parser.parse_args(argv)
    try:
        result = load_capture(args.capture)
    except (OSError, ValueError, csv.Error, UnicodeError) as exc:
        print(f"Invalid performance capture: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False))
    if args.require_measurement_gates and not (
        result["nfr_001"]["measurement_gate_met"] and result["nfr_012_state_interval"]["measurement_gate_met"]
    ):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
