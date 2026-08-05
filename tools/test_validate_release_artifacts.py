#!/usr/bin/env python3
"""Focused tests for release artifact manifest validation."""

import json
import tempfile
import unittest
import zipfile
from pathlib import Path

import validate_release_artifacts as validator


class ReleaseArtifactManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.manifest = self.root / "manifest.json"
        self.manifest.write_text(json.dumps({
            "format_version": 1,
            "artifacts": [
                {"id": "source", "kind": "source-archive", "pattern": "quarry-{tag_version}.zip",
                 "origin": "github-generated", "required": True},
                {"id": "wheel", "kind": "python-wheel", "pattern": "quarry_runtime_python-{version}-*.whl",
                 "origin": "release-build", "required": True},
            ],
        }))

    def tearDown(self):
        self.temp.cleanup()

    def write_valid_artifacts(self):
        with zipfile.ZipFile(self.root / "quarry-0.1.7.zip", "w") as archive:
            archive.writestr("quarry-v0.1.7/README.md", "Quarry")
        with zipfile.ZipFile(self.root / "quarry_runtime_python-0.1.7-py3-none-any.whl", "w") as archive:
            archive.writestr("quarry_runtime_python-0.1.7.dist-info/METADATA",
                             "Metadata-Version: 2.1\nVersion: 0.1.7\n")

    def test_complete_release_passes(self):
        self.write_valid_artifacts()
        found = validator.validate(self.root, self.manifest, "0.1.7", "v0.1.7")
        self.assertEqual(len(found), 2)

    def test_missing_required_artifact_fails(self):
        with self.assertRaisesRegex(ValueError, "required artifact source is missing"):
            validator.validate(self.root, self.manifest, "0.1.7", "v0.1.7")

    def test_wrong_python_version_fails(self):
        self.write_valid_artifacts()
        wheel = self.root / "quarry_runtime_python-0.1.7-py3-none-any.whl"
        wheel.unlink()
        with zipfile.ZipFile(self.root / "quarry_runtime_python-0.1.7-py3-none-any.whl", "w") as archive:
            archive.writestr("quarry_runtime_python-0.1.7.dist-info/METADATA",
                             "Metadata-Version: 2.1\nVersion: 0.1.6\n")
        with self.assertRaisesRegex(ValueError, "declares version 0.1.6"):
            validator.validate(self.root, self.manifest, "0.1.7", "v0.1.7")

    def test_development_file_in_source_archive_fails(self):
        self.write_valid_artifacts()
        source = self.root / "quarry-0.1.7.zip"
        source.unlink()
        with zipfile.ZipFile(source, "w") as archive:
            archive.writestr("quarry-v0.1.7/REPORT.md", "local")
        with self.assertRaisesRegex(ValueError, "development-only"):
            validator.validate(self.root, self.manifest, "0.1.7", "v0.1.7")


if __name__ == "__main__":
    unittest.main()
