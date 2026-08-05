"""Resolve the Quarry package version for source and packaged builds."""

from __future__ import annotations

import os
import importlib.metadata
from pathlib import Path
import re
import subprocess


_NUMERIC_VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
_LINE_VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\n$")


def _fallback_version() -> str | None:
    fallback = Path(__file__).with_name("_resolved_version.py")
    if fallback.exists():
        match = re.search(r"^__version__\s*=\s*['\"]([^'\"]+)['\"]", fallback.read_text())
        if match and _NUMERIC_VERSION.fullmatch(match.group(1)):
            return match.group(1)
    return None


def _metadata_version() -> str | None:
    package_root = Path(__file__).resolve().parent
    for root in package_root.parents:
        for metadata in (root / "quarry_runtime_python.egg-info" / "PKG-INFO",
                         root / "src" / "quarry_runtime_python.egg-info" / "PKG-INFO"):
            if not metadata.exists():
                continue
            match = re.search(r"^Version:\s*([^\n]+)$", metadata.read_text(), re.MULTILINE)
            if match and _NUMERIC_VERSION.fullmatch(match.group(1).strip()):
                return match.group(1).strip()
    return None


def _installed_metadata_version() -> str | None:
    try:
        value = importlib.metadata.version("quarry-runtime-python")
    except importlib.metadata.PackageNotFoundError:
        return None
    return value if _NUMERIC_VERSION.fullmatch(value) else None


def _git_version(root: Path, major_minor: str) -> str | None:
    try:
        shallow = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--is-shallow-repository"],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        if shallow == "true":
            raise RuntimeError("Quarry version resolution requires a non-shallow Git checkout")
        base = subprocess.run(
            ["git", "-C", str(root), "log", "-1", "--format=%H", "--", "git_version"],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        revision = 0 if not base else int(subprocess.run(
            ["git", "-C", str(root), "rev-list", "--count", f"{base}..HEAD"],
            check=True, capture_output=True, text=True,
        ).stdout.strip())
        return f"{major_minor}.{revision}"
    except (OSError, subprocess.CalledProcessError, ValueError):
        return None


def _resolve() -> str:
    package_root = Path(__file__).resolve().parent
    fallback = _fallback_version()
    if fallback:
        return fallback

    for root in (package_root.parents[3], package_root.parents[2]):
        version_file = root / "git_version"
        if not version_file.exists():
            continue
        content = version_file.read_text()
        if not _LINE_VERSION.fullmatch(content):
            raise RuntimeError(f"invalid Quarry git_version file: {version_file}")
        major_minor = content.rstrip("\n")
        resolved = _git_version(root, major_minor)
        if resolved:
            return resolved
        cmake_fallback = root / "cmake" / "QuarryResolvedVersion.cmake"
        if cmake_fallback.exists():
            match = re.search(r'set\(QUARRY_VERSION\s+"([^"]+)"\)', cmake_fallback.read_text())
            if match and _NUMERIC_VERSION.fullmatch(match.group(1)):
                return match.group(1)

    metadata_version = _metadata_version()
    if metadata_version:
        return metadata_version
    installed_version = _installed_metadata_version()
    if installed_version:
        return installed_version

    raise RuntimeError(
        "unable to resolve Quarry version; use a full Git checkout or package the "
        "generated _resolved_version.py fallback"
    )


__version__ = _resolve()
