"""Check manual high-speed-video input latency evidence; never infer server ACK time.

Usage: python scripts/analyze_input_latency.py capture.csv --require-measurement-gate
The sibling JSON identifies the source package, hashed files and manual conditions.
Exit 2: invalid evidence; exit 1: requested gate fails; exit 0: analysis completed.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import sys


ACTIONS = ("accelerate", "brake", "steer_left", "steer_right")
HEADER = ["action", "trial", "video_file", "input_frame", "response_frame", "capture_fps"]
METRIC = "physical_key_to_first_visible_vehicle_response_ms"
PACKAGE_EXECUTABLES = {
    "client_executable": "Windows/DriveIntegration/Binaries/Win64/DriveIntegration.exe",
    "server_executable": "cpp/host/build/Release/simcore_publisher.exe",
}
CONDITIONS = (
    "stationary_camera", "physical_key_and_screen_visible", "original_continuous_frames",
    "capture_rate_verified", "no_dropped_or_interpolated_frames", "isolated_inputs",
    "all_attempts_recorded", "first_physical_key_motion_used", "endpoint_uncertainty_at_most_one_frame",
)


def integer(value, name: str, minimum: int = 0) -> int:
    text = str(value)
    if not text.isascii() or not text.isdecimal():
        raise ValueError(f"{name} must be an integer >= {minimum}")
    number = int(text)
    if not minimum <= number <= 2**63 - 1:
        raise ValueError(f"{name} is outside the supported integer range")
    return number


def positive(value, name: str) -> float:
    if isinstance(value, bool):
        raise ValueError(f"{name} must be numeric")
    try:
        number = float(value)
    except (TypeError, ValueError, OverflowError) as exc:
        raise ValueError(f"{name} must be numeric") from exc
    if not math.isfinite(number) or number <= 0:
        raise ValueError(f"{name} must be finite and positive")
    return number


def object_value(value, name: str) -> dict:
    if not isinstance(value, dict):
        raise ValueError(f"{name} must be an object")
    return value


def nonempty(value, name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{name} must be nonempty text")
    return value


def no_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"non-finite JSON number: {value}")


def read_json(path: Path) -> dict:
    return object_value(json.loads(path.read_text(encoding="utf-8-sig"),
                                  object_pairs_hook=no_duplicate_keys,
                                  parse_constant=reject_constant), str(path))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_file(record, base: Path, name: str) -> tuple[Path, dict]:
    record = object_value(record, name)
    path = (base / nonempty(record.get("file"), f"{name}.file")).resolve(strict=True)
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"{name} must identify a nonempty file: {path}")
    expected = record.get("sha256")
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected):
        raise ValueError(f"{name}.sha256 must contain 64 hexadecimal characters")
    actual = sha256(path)
    if actual != expected.lower():
        raise ValueError(f"{name}: SHA-256 mismatch: {path}")
    return path, {"file": str(path), "sha256": actual, "bytes": path.stat().st_size}


def verify_manifest_executables(manifest: dict, package_root: Path, package_evidence: dict) -> None:
    """Cross-check the v2 inventory's two measured executables, not every package file."""
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ValueError("package manifest v2 requires a nonempty files inventory")
    inventory = {}
    for index, entry in enumerate(files):
        entry = object_value(entry, f"package_manifest.files[{index}]")
        relative = Path(nonempty(entry.get("path"), "package inventory path"))
        if relative.anchor or ".." in relative.parts:
            raise ValueError("package inventory path must be relative and inside the package")
        path = (package_root / relative).resolve()
        if not path.is_relative_to(package_root) or path in inventory:
            raise ValueError("package inventory contains an outside path or duplicate file")
        digest = entry.get("sha256")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
            raise ValueError("package inventory sha256 must contain 64 hexadecimal characters")
        inventory[path] = {"bytes": integer(entry.get("bytes"), "package inventory bytes"),
                           "sha256": digest.lower()}
    for role, relative in PACKAGE_EXECUTABLES.items():
        expected_path = (package_root / relative).resolve()
        evidence = package_evidence[role]
        if Path(evidence["file"]) != expected_path:
            raise ValueError(f"manifest v2 {role} must identify {relative}")
        entry = inventory.get(expected_path)
        if entry is None:
            raise ValueError(f"manifest v2 inventory is missing {role}")
        if entry["sha256"] != evidence["sha256"] or entry["bytes"] != evidence["bytes"]:
            raise ValueError(f"manifest v2 inventory differs from measured {role} bytes or SHA-256")


def analyze(metadata: dict, rows: list[dict], evidence_directory: Path) -> dict:
    metadata = object_value(metadata, "metadata")
    if type(metadata.get("schema_version")) is not int or metadata["schema_version"] != 1:
        raise ValueError("unsupported metadata schema")
    if metadata.get("metric") != METRIC or metadata.get("mode") != "packaged":
        raise ValueError("this measurement requires packaged physical-key-to-visible-response evidence")
    if metadata.get("evidence_kind") not in ("manual_video", "synthetic_fixture"):
        raise ValueError("evidence_kind must be manual_video or synthetic_fixture")
    for field in ("operator", "captured_at", "test_conditions"):
        nonempty(metadata.get(field), field)
    criteria = object_value(metadata.get("response_criteria"), "response_criteria")
    expected = object_value(metadata.get("attempts_per_action"), "attempts_per_action")
    if set(expected) != set(ACTIONS) or set(criteria) != set(ACTIONS):
        raise ValueError("attempt counts and response criteria must cover exactly the four actions")
    counts = {action: integer(expected[action], f"{action} attempts", 1) for action in ACTIONS}
    for action in ACTIONS:
        nonempty(criteria[action], f"{action} response criterion")
    conditions = object_value(metadata.get("manual_conditions"), "manual_conditions")
    excluded = []
    for field in CONDITIONS:
        if type(conditions.get(field)) is not bool:
            raise ValueError(f"manual_conditions.{field} must be boolean")
        if not conditions[field]:
            excluded.append(f"manual condition not established: {field}")
    if metadata["evidence_kind"] == "synthetic_fixture":
        excluded.append("synthetic fixture is not actual measurement evidence")

    manifest_path, manifest_evidence = verify_file(
        metadata.get("source_package_manifest"), evidence_directory, "source_package_manifest")
    manifest = read_json(manifest_path)
    if type(manifest.get("format_version")) is not int or manifest["format_version"] not in (1, 2):
        raise ValueError("unsupported source package manifest format")
    for field in ("created_utc", "client_configuration", "server_configuration", "map_id"):
        nonempty(manifest.get(field), f"package_manifest.{field}")
    package_files = object_value(metadata.get("package_files"), "package_files")
    if set(package_files) != {"client_executable", "server_executable"}:
        raise ValueError("package_files must identify client_executable and server_executable")
    package_evidence = {}
    package_paths = set()
    for role, record in package_files.items():
        path, verified = verify_file(record, manifest_path.parent, f"package_files.{role}")
        if not path.is_relative_to(manifest_path.parent) or path.suffix.lower() != ".exe":
            raise ValueError(f"{role} must be an executable inside the source package")
        if path in package_paths:
            raise ValueError("client and server must identify distinct executables")
        package_paths.add(path)
        package_evidence[role] = verified
    if manifest["format_version"] == 2:
        verify_manifest_executables(manifest, manifest_path.parent, package_evidence)

    videos = metadata.get("videos")
    if not isinstance(videos, list) or not videos:
        raise ValueError("videos must be a nonempty list")
    video_index, video_paths = {}, set()
    for index, record in enumerate(videos):
        path, verified = verify_file(record, evidence_directory, f"videos[{index}]")
        if path in video_paths:
            raise ValueError("duplicate video file or path alias")
        video_paths.add(path)
        fps = positive(record.get("capture_fps"), "video capture_fps")
        frame_count = integer(record.get("frame_count"), "video frame_count", 1)
        if fps < 120:
            excluded.append(f"video capture rate below 120 fps: {record['file']}")
        video_index[record["file"]] = {**verified, "capture_fps": fps, "frame_count": frame_count}

    trials = {action: {} for action in ACTIONS}
    used_frames, intervals, samples = set(), {}, []
    for row_number, row in enumerate(rows, 2):
        if set(row) != set(HEADER) or row.get("action") not in ACTIONS:
            raise ValueError(f"CSV row {row_number}: invalid columns or action")
        action = row["action"]
        trial = integer(row["trial"], f"row {row_number} trial", 1)
        if trial in trials[action]:
            raise ValueError(f"duplicate trial: {action} {trial}")
        video = video_index.get(row["video_file"])
        if video is None:
            raise ValueError(f"CSV row {row_number}: video absent from metadata")
        fps = positive(row["capture_fps"], f"row {row_number} capture_fps")
        if fps != video["capture_fps"]:
            raise ValueError(f"CSV row {row_number}: capture rate differs from video metadata")
        start = integer(row["input_frame"], f"row {row_number} input_frame")
        missing = row["response_frame"] is not None and str(row["response_frame"]).strip() == ""
        end = None if missing else integer(row["response_frame"], f"row {row_number} response_frame")
        if start >= video["frame_count"] or (end is not None and not start <= end < video["frame_count"]):
            raise ValueError(f"CSV row {row_number}: frame outside video or response precedes input")
        key = (video["file"], start)
        if key in used_frames:
            raise ValueError("duplicate physical input frame (including across actions)")
        used_frames.add(key)
        intervals.setdefault(video["file"], []).append((start, end if end is not None else start))
        sample = {"action": action, "trial": trial, "video_file": row["video_file"],
                  "input_frame": start, "response_frame": end, "capture_fps": fps,
                  "observed_ms": None if missing else (end - start) * 1000.0 / fps,
                  "conservative_upper_ms": None if missing else (end - start + 2) * 1000.0 / fps,
                  "status": "missing_response" if missing else "measured"}
        if not missing and not math.isfinite(sample["conservative_upper_ms"]):
            raise ValueError("calculated latency is not finite")
        trials[action][trial] = sample
        samples.append(sample)
    for windows in intervals.values():
        ordered = sorted(windows)
        if any(current[0] <= previous[1] for previous, current in zip(ordered, ordered[1:])):
            raise ValueError("overlapping input/response windows do not establish isolated trials")
    unused = set(video_index) - {sample["video_file"] for sample in samples}
    if unused:
        raise ValueError("metadata contains unused videos")

    actions = {}
    for action in ACTIONS:
        action_trials = trials[action]
        if len(action_trials) != counts[action] or sorted(action_trials) != list(range(1, len(action_trials) + 1)):
            raise ValueError(f"{action}: expected attempt count mismatch or missing trial number")
        upper = [trial["conservative_upper_ms"] for trial in action_trials.values()
                 if trial["response_frame"] is not None]
        missing_count = len(action_trials) - len(upper)
        target_met = bool(upper) and not missing_count and max(upper) <= 50.0
        enough = len(action_trials) >= 10
        if not enough:
            excluded.append(f"{action}: fewer than 10 attempts")
        actions[action] = {"attempts": len(action_trials), "measured_samples": len(upper),
                           "missing_responses": missing_count, "minimum_sample_count_met": enough,
                           "max_conservative_upper_ms": max(upper) if upper else None,
                           "target_met": target_met}
    target_met = all(action["target_met"] for action in actions.values())
    return {
        "metric": METRIC, "evidence_kind": metadata["evidence_kind"], "target_ms": 50.0,
        "endpoint_uncertainty_frames_each": 1, "actions": actions, "samples": samples,
        "target_met": target_met, "measurement_gate_met": not excluded and target_met,
        "exclusion_reasons": excluded,
        "source_package_manifest": manifest_evidence, "package_manifest": manifest,
        "package_inventory_cross_check": ("client_and_server_bytes_and_sha256_verified"
                                          if manifest["format_version"] == 2 else "unavailable_in_manifest_v1"),
        "package_files": package_evidence, "videos": list(video_index.values()),
        "capture_context": {field: metadata[field] for field in ("operator", "captured_at", "test_conditions")},
        "manual_conditions": conditions, "response_criteria": criteria,
        "evidence_review": "file hashes and arithmetic checked; video contents and manual declarations require human review",
        "server_applied_ack_latency": "not_measured",
        "thirty_minute_fps_and_driving": "not_established_by_this_measurement",
        "release_acceptance": "not_established",
    }


def load_capture(csv_path: Path) -> dict:
    csv_path = csv_path.resolve(strict=True)
    metadata_path = csv_path.with_suffix(".json")
    metadata = read_json(metadata_path)
    with csv_path.open("rb") as stream:
        stream.seek(0, 2)
        if stream.tell() == 0:
            raise ValueError("empty CSV")
        stream.seek(-1, 2)
        if stream.read(1) != b"\n":
            raise ValueError("CSV missing final newline (possibly truncated)")
    with csv_path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, strict=True)
        if reader.fieldnames != HEADER:
            raise ValueError("unsupported CSV header")
        rows = list(reader)
    result = analyze(metadata, rows, csv_path.parent)
    result["raw_csv"] = {"file": str(csv_path), "sha256": sha256(csv_path)}
    result["metadata_json"] = {"file": str(metadata_path), "sha256": sha256(metadata_path)}
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="annotated CSV; sibling JSON and original videos required")
    parser.add_argument("--require-measurement-gate", action="store_true",
                        help="exit 1 unless all four actions qualify for the manual-video 50 ms gate")
    args = parser.parse_args(argv)
    try:
        result = load_capture(args.capture)
    except (OSError, ValueError, csv.Error, UnicodeError) as exc:
        print(f"Invalid input latency evidence: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False))
    return 1 if args.require_measurement_gate and not result["measurement_gate_met"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
