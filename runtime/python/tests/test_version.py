"""Regression tests for Git and package version resolution."""

import importlib.metadata
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from quarry import _version


class VersionResolutionTest(unittest.TestCase):
    def test_fallback_version_accepts_only_numeric_versions(self):
        with tempfile.TemporaryDirectory() as name:
            package = Path(name) / "quarry"
            package.mkdir()
            fallback = package / "_resolved_version.py"
            fallback.write_text("__version__ = '0.1.7'\n", encoding="utf-8")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")):
                self.assertEqual(_version._fallback_version(), "0.1.7")
            fallback.write_text("__version__ = '0.1.7-dirty'\n", encoding="utf-8")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")):
                self.assertIsNone(_version._fallback_version())

    def test_metadata_version_accepts_valid_metadata(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            package = root / "src" / "quarry"
            package.mkdir(parents=True)
            metadata = root / "quarry_runtime_python.egg-info" / "PKG-INFO"
            metadata.parent.mkdir()
            metadata.write_text("Metadata-Version: 2.1\nVersion: 0.1.8\n", encoding="utf-8")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")):
                self.assertEqual(_version._metadata_version(), "0.1.8")
            metadata.write_text("Metadata-Version: 2.1\nVersion: 0.1.8-dev\n", encoding="utf-8")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")):
                self.assertIsNone(_version._metadata_version())

    def test_installed_metadata_handles_missing_and_invalid_versions(self):
        with mock.patch.object(importlib.metadata, "version",
                               side_effect=importlib.metadata.PackageNotFoundError):
            self.assertIsNone(_version._installed_metadata_version())
        with mock.patch.object(importlib.metadata, "version", return_value="0.1.9-dev"):
            self.assertIsNone(_version._installed_metadata_version())
        with mock.patch.object(importlib.metadata, "version", return_value="0.1.9"):
            self.assertEqual(_version._installed_metadata_version(), "0.1.9")

    def test_git_version_counts_commits_and_handles_empty_base(self):
        responses = [mock.Mock(stdout="false\n"), mock.Mock(stdout="abc\n"),
                     mock.Mock(stdout="4\n")]
        with mock.patch.object(subprocess, "run", side_effect=responses):
            self.assertEqual(_version._git_version(Path("/repo"), "0.1"), "0.1.4")
        responses = [mock.Mock(stdout="false\n"), mock.Mock(stdout="\n")]
        with mock.patch.object(subprocess, "run", side_effect=responses):
            self.assertEqual(_version._git_version(Path("/repo"), "0.1"), "0.1.0")

    def test_git_version_rejects_shallow_and_failed_commands(self):
        with mock.patch.object(subprocess, "run", return_value=mock.Mock(stdout="true\n")):
            with self.assertRaises(RuntimeError):
                _version._git_version(Path("/repo"), "0.1")
        with mock.patch.object(subprocess, "run",
                               side_effect=subprocess.CalledProcessError(1, "git")):
            self.assertIsNone(_version._git_version(Path("/repo"), "0.1"))

    def test_resolved_version_is_numeric(self):
        self.assertRegex(_version.__version__, r"^[0-9]+\.[0-9]+\.[0-9]+$")

    def _temporary_layout(self):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        package = root / "runtime" / "python" / "src" / "quarry"
        package.mkdir(parents=True)
        return temporary, root, package

    def test_resolve_uses_fallback_and_rejects_malformed_git_version(self):
        with mock.patch.object(_version, "_fallback_version", return_value="0.1.11"):
            self.assertEqual(_version._resolve(), "0.1.11")
        temporary, root, package = self._temporary_layout()
        try:
            (root / "git_version").write_text("0.1.7\n", encoding="utf-8")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")):
                with self.assertRaisesRegex(RuntimeError, "invalid Quarry git_version"):
                    _version._resolve()
        finally:
            temporary.cleanup()

    def test_resolve_uses_cmake_fallback(self):
        temporary, root, package = self._temporary_layout()
        try:
            (root / "git_version").write_text("0.1\n", encoding="utf-8")
            cmake = root / "cmake"
            cmake.mkdir()
            (cmake / "QuarryResolvedVersion.cmake").write_text(
                'set(QUARRY_VERSION "0.1.12")\n', encoding="utf-8")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")), \
                    mock.patch.object(_version, "_git_version", return_value=None):
                self.assertEqual(_version._resolve(), "0.1.12")
        finally:
            temporary.cleanup()

    def test_cmake_fallback_uses_archive_tag(self):
        temporary, root, package = self._temporary_layout()
        try:
            (root / "git_version").write_text("0.1\n", encoding="utf-8")
            cmake = root / "cmake"
            cmake.mkdir()
            (cmake / "QuarryResolvedVersion.cmake").write_text(
                'set(QUARRY_ARCHIVE_TAG "v0.1.17-rc.1")\n', encoding="utf-8")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")), \
                    mock.patch.object(_version, "_git_version", return_value=None):
                self.assertEqual(_version._resolve(), "0.1.17")
        finally:
            temporary.cleanup()

    def test_cmake_fallback_rejects_missing_and_malformed_values(self):
        temporary, root, package = self._temporary_layout()
        try:
            cmake = root / "cmake"
            cmake.mkdir()
            with mock.patch.object(_version, "__file__", str(package / "_version.py")):
                self.assertIsNone(_version._cmake_fallback_version(root))
            fallback = cmake / "QuarryResolvedVersion.cmake"
            fallback.write_text(
                'set(QUARRY_VERSION "not-a-version")\n'
                'set(QUARRY_ARCHIVE_TAG "$Format:%(describe:tags)$")\n',
                encoding="utf-8")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")):
                self.assertIsNone(_version._cmake_fallback_version(root))
        finally:
            temporary.cleanup()

    def test_resolve_uses_metadata_and_installed_fallbacks(self):
        temporary, root, package = self._temporary_layout()
        try:
            with mock.patch.object(_version, "__file__", str(package / "_version.py")), \
                    mock.patch.object(_version, "_metadata_version", return_value="0.1.13"):
                self.assertEqual(_version._resolve(), "0.1.13")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")), \
                    mock.patch.object(_version, "_metadata_version", return_value=None), \
                    mock.patch.object(_version, "_installed_metadata_version", return_value="0.1.14"):
                self.assertEqual(_version._resolve(), "0.1.14")
            with mock.patch.object(_version, "__file__", str(package / "_version.py")), \
                    mock.patch.object(_version, "_metadata_version", return_value=None), \
                    mock.patch.object(_version, "_installed_metadata_version", return_value=None):
                with self.assertRaisesRegex(RuntimeError, "unable to resolve Quarry version"):
                    _version._resolve()
        finally:
            temporary.cleanup()


if __name__ == "__main__":
    unittest.main()
