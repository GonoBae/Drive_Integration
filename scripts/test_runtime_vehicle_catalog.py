"""Byte-identity checks for local validation clients; no servers or sockets."""

import json
from pathlib import Path
import shutil
import tempfile
import unittest

from runtime_vehicle_catalog import MANIFEST, catalog_capability, checked_catalog_capability


class RuntimeVehicleCatalogIdentityTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="simcore-catalog-identity-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / "relocated catalog"
        shutil.copytree(MANIFEST.parent, self.directory)
        self.manifest = self.directory / "catalog.json"

    def test_relocated_exact_copy_has_same_identity(self):
        self.assertRegex(catalog_capability(), r"^vehicle-catalog-fnv1a64-[0-9a-f]{16}$")
        self.assertEqual(catalog_capability(self.manifest), catalog_capability())

    def test_changed_raw_profile_bytes_change_identity(self):
        before = catalog_capability(self.manifest)
        profile = self.directory / "profiles" / "sedan.json"
        profile.write_bytes(profile.read_bytes() + b"\n")
        self.assertNotEqual(catalog_capability(self.manifest), before)

    def test_changed_raw_manifest_bytes_change_identity(self):
        before = catalog_capability(self.manifest)
        self.manifest.write_bytes(self.manifest.read_bytes() + b"\n")
        self.assertNotEqual(catalog_capability(self.manifest), before)

    def test_missing_profile_is_rejected(self):
        (self.directory / "profiles" / "truck.json").unlink()
        with self.assertRaises(FileNotFoundError):
            catalog_capability(self.manifest)

    def test_duplicate_class_is_rejected(self):
        manifest = json.loads(self.manifest.read_bytes())
        manifest["profiles"][1] = manifest["profiles"][0]
        self.manifest.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "each class exactly once"):
            catalog_capability(self.manifest)

    def test_profile_path_cannot_escape_catalog(self):
        manifest = json.loads(self.manifest.read_bytes())
        manifest["profiles"][0] = "../outside.json"
        self.manifest.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaises(ValueError):
            catalog_capability(self.manifest)

    def test_matching_capability_is_returned(self):
        expected = catalog_capability()
        self.assertEqual(checked_catalog_capability(["world-health.v1", expected, "traffic-signals.v1"]), expected)

    def test_loadout_bytes_contribute_to_identity(self):
        manifest = json.loads(self.manifest.read_bytes())
        before = catalog_capability(self.manifest)
        path = self.directory / manifest["loadouts"][0]
        path.write_bytes(path.read_bytes() + b"\n")
        self.assertNotEqual(catalog_capability(self.manifest), before)

    def test_invalid_loadout_sources_are_rejected(self):
        original = json.loads(self.manifest.read_bytes())
        path = original["loadouts"][0]
        for sources in ([path, path], ["../outside.json"], "wrong", [path] * 65, [original["parts"][0]]):
            with self.subTest(sources=sources):
                self.manifest.write_text(json.dumps(dict(original, loadouts=sources)), encoding="utf-8")
                with self.assertRaises(ValueError):
                    catalog_capability(self.manifest)

    def test_missing_loadout_is_rejected(self):
        manifest = json.loads(self.manifest.read_bytes())
        (self.directory / manifest["loadouts"][0]).unlink()
        with self.assertRaises(FileNotFoundError):
            catalog_capability(self.manifest)

    def test_changed_part_bytes_change_identity(self):
        manifest = json.loads(self.manifest.read_bytes())
        before = catalog_capability(self.manifest)
        part = self.directory / manifest["parts"][0]
        part.write_bytes(part.read_bytes() + b"\n")
        self.assertNotEqual(catalog_capability(self.manifest), before)

    def test_missing_part_is_rejected(self):
        manifest = json.loads(self.manifest.read_bytes())
        (self.directory / manifest["parts"][0]).unlink()
        with self.assertRaises(FileNotFoundError):
            catalog_capability(self.manifest)

    def test_invalid_part_lists_are_rejected(self):
        original = json.loads(self.manifest.read_bytes())
        part = original["parts"][0]
        for parts in ([part, part], ["../outside.json"], ["C:/part.json"],
                      "not an array", [part] * 65, ["profiles/sedan.json"]):
            with self.subTest(parts=parts):
                document = dict(original, parts=parts)
                self.manifest.write_text(json.dumps(document), encoding="utf-8")
                with self.assertRaises(ValueError):
                    catalog_capability(self.manifest)

    def test_duplicate_part_ids_are_rejected(self):
        manifest = json.loads(self.manifest.read_bytes())
        first = json.loads((self.directory / manifest["parts"][0]).read_bytes())
        second_path = self.directory / manifest["parts"][1]
        second = json.loads(second_path.read_bytes())
        second["id"] = first["id"]
        second_path.write_text(json.dumps(second), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "duplicate runtime part"):
            catalog_capability(self.manifest)

    def test_each_powertrain_part_contributes_to_identity(self):
        manifest = json.loads(self.manifest.read_bytes())
        before = catalog_capability(self.manifest)
        tested = set()
        for relative in manifest["parts"]:
            path = self.directory / relative
            raw = path.read_bytes()
            kind = json.loads(raw)["kind"]
            if kind not in ("engine", "transmission", "drivetrain", "fuel_tank"):
                continue
            with self.subTest(kind=kind):
                path.write_bytes(raw + b"\n")
                self.assertNotEqual(catalog_capability(self.manifest), before)
                path.write_bytes(raw)
                self.assertEqual(catalog_capability(self.manifest), before)
            tested.add(kind)
        self.assertEqual(tested, {"engine", "transmission", "drivetrain", "fuel_tank"})

    def test_unsupported_part_kind_is_rejected(self):
        manifest = json.loads(self.manifest.read_bytes())
        path = self.directory / manifest["parts"][0]
        document = json.loads(path.read_bytes())
        document["kind"] = "wheel"
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "runtime part"):
            catalog_capability(self.manifest)

    def test_missing_duplicate_and_mismatched_capabilities_are_rejected(self):
        expected = catalog_capability()
        for capabilities in ([], ["world-health.v1"], [expected, expected],
                             ["vehicle-catalog-fnv1a64-mismatch"],
                             [expected, "vehicle-catalog-unknown-version"]):
            with self.subTest(capabilities=capabilities):
                with self.assertRaisesRegex(AssertionError, "vehicle catalog differs"):
                    checked_catalog_capability(capabilities)


if __name__ == "__main__":
    unittest.main()
