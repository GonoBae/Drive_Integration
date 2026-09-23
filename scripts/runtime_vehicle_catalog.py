"""Shared file identity used by the local WebSocket validation clients."""

import json
from pathlib import Path
import re

MANIFEST = Path(__file__).resolve().parents[1] / "unreal/DriveIntegration/Config/VehicleCatalog/catalog.json"


def catalog_capability(manifest=MANIFEST):
    manifest = Path(manifest).resolve()
    raw_manifest = manifest.read_bytes()
    document = json.loads(raw_manifest)

    def read_source(relative):
        if (not isinstance(relative, str) or not relative.isascii()
                or "\\" in relative or ":" in relative
                or any(part in ("", ".", "..") for part in relative.split("/"))
                or not relative.endswith(".json")):
            raise ValueError("catalog source must be a relative JSON path")
        path = (manifest.parent / relative).resolve()
        path.relative_to(manifest.parent)
        return path, path.read_bytes()

    profiles = []
    sources = {manifest}
    for relative in document["profiles"]:
        path, raw = read_source(relative)
        sources.add(path)
        profiles.append((json.loads(raw)["vehicle_class"], relative.encode("ascii"), raw))
    if sorted(item[0] for item in profiles) != [1, 2, 3, 4]:
        raise ValueError("vehicle catalog must contain each class exactly once")
    fields = [raw_manifest]
    for _, path, raw in sorted(profiles):
        fields.extend((path, raw))
    parts = document.get("parts", [])
    if not isinstance(parts, list) or len(parts) > 64:
        raise ValueError("catalog parts must be an array with at most 64 entries")
    part_ids = set()
    part_sources = []
    for relative in parts:
        path, raw = read_source(relative)
        if path in sources:
            raise ValueError("duplicate catalog source")
        sources.add(path)
        part = json.loads(raw)
        identifier = part.get("id")
        if (not isinstance(identifier, str) or not re.fullmatch(r"[a-z][a-z0-9_]{0,63}", identifier)
                or identifier in part_ids or part.get("kind") not in (
                    "tire", "suspension", "engine", "transmission", "drivetrain", "fuel_tank")):
            raise ValueError("invalid or duplicate runtime part")
        part_ids.add(identifier)
        part_sources.append((relative.encode("ascii"), raw))
    for path, raw in sorted(part_sources):
        fields.extend((path, raw))
    loadouts = document.get("loadouts", [])
    if not isinstance(loadouts, list) or len(loadouts) > 64:
        raise ValueError("catalog loadouts must be an array with at most 64 entries")
    loadout_sources = []
    for relative in loadouts:
        path, raw = read_source(relative)
        if path in sources:
            raise ValueError("duplicate catalog source")
        sources.add(path)
        loadout = json.loads(raw)
        identifier = loadout.get("id")
        if (not isinstance(identifier, str) or not re.fullmatch(r"[a-z][a-z0-9_]{0,63}", identifier)
                or identifier in part_ids or loadout.get("vehicle_class") not in (1, 2, 3, 4)):
            raise ValueError("invalid or duplicate loadout")
        part_ids.add(identifier)
        loadout_sources.append((relative.encode("ascii"), raw))
    for path, raw in sorted(loadout_sources):
        fields.extend((path, raw))
    value = 14695981039346656037
    content = b"runtime-vehicle-catalog-v1" + b"".join(str(len(field)).encode("ascii") + b":" + field for field in fields)
    for byte in content:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"vehicle-catalog-fnv1a64-{value:016x}"


def checked_catalog_capability(server_capabilities):
    expected = catalog_capability()
    advertised = [item for item in server_capabilities if item.startswith("vehicle-catalog-")]
    if advertised != [expected]:
        raise AssertionError("server vehicle catalog differs from local validation files")
    return expected
