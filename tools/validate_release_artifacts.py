#!/usr/bin/env python3
"""Validate a staged Quarry release against release/artifact-manifest.json."""

from __future__ import annotations

import argparse
import json
import re
import sys
import tarfile
import zipfile
from pathlib import Path
from string import Formatter


FORBIDDEN_SOURCE_PARTS = {".git", ".coverage", "REPORT.md"}


def fail(message: str) -> None:
    raise ValueError(message)


def load_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read manifest {path}: {error}")
    if manifest.get("format_version") != 1:
        fail("unsupported or missing artifact manifest format_version (expected 1)")
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        fail("artifact manifest must contain a non-empty artifacts list")
    identifiers = [entry.get("id") for entry in artifacts if isinstance(entry, dict)]
    if len(identifiers) != len(set(identifiers)):
        fail("artifact manifest contains duplicate artifact ids")
    return manifest


def rendered_pattern(pattern: str, version: str, tag: str) -> str:
    fields = {field for _, field, _, _ in Formatter().parse(pattern) if field}
    unknown = fields - {"version", "tag", "tag_version"}
    if unknown:
        fail(f"manifest pattern uses unsupported placeholders: {sorted(unknown)}")
    tag_version = tag[1:] if tag.startswith("v") else tag
    return pattern.format(version=version, tag=tag, tag_version=tag_version)


def members(path: Path) -> list[str]:
    if path.suffix == ".zip":
        with zipfile.ZipFile(path) as archive:
            return archive.namelist()
    if path.name.endswith(".tar.gz"):
        with tarfile.open(path, "r:gz") as archive:
            return archive.getnames()
    return []


def validate_source_archive(path: Path) -> None:
    bad = []
    for name in members(path):
        parts = set(Path(name).parts)
        if parts & FORBIDDEN_SOURCE_PARTS or any(part.endswith(".egg-info") for part in parts):
            bad.append(name)
    if bad:
        fail(f"source archive {path.name} contains development-only entries: {', '.join(sorted(bad))}")


def validate_python_metadata(path: Path, version: str, kind: str) -> None:
    metadata = []
    if kind == "python-wheel":
        with zipfile.ZipFile(path) as archive:
            metadata = [
                archive.read(name).decode("utf-8", errors="replace")
                for name in archive.namelist()
                if name.endswith(".dist-info/METADATA")
            ]
    else:
        with tarfile.open(path, "r:gz") as archive:
            metadata = [
                archive.extractfile(member).read().decode("utf-8", errors="replace")
                for member in archive.getmembers()
                if member.name.endswith("/PKG-INFO")
                and not any(part.endswith(".egg-info") for part in Path(member.name).parts)
                and member.isfile()
            ]
    if len(metadata) != 1:
        fail(f"{kind} {path.name} must contain exactly one package metadata file")
    match = re.search(r"^Version:\s*(\S+)\s*$", metadata[0], re.MULTILINE)
    if not match or match.group(1) != version:
        actual = match.group(1) if match else "missing"
        fail(f"{kind} {path.name} declares version {actual}, expected {version}")


def validate(root: Path, manifest_path: Path, version: str, tag: str) -> list[str]:
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:\.post\d+)?", version):
        fail(f"invalid numeric release version: {version}")
    manifest = load_manifest(manifest_path)
    found = []
    for entry in manifest["artifacts"]:
        if not isinstance(entry, dict):
            fail("artifact manifest entries must be objects")
        for key in ("id", "kind", "pattern", "origin", "required"):
            if key not in entry:
                fail(f"artifact entry is missing {key!r}")
        if not isinstance(entry["required"], bool):
            fail(f"artifact {entry['id']} has a non-boolean required value")
        pattern = rendered_pattern(entry["pattern"], version, tag)
        matches = sorted(path for path in root.glob(pattern) if path.is_file())
        if len(matches) > 1:
            fail(f"artifact {entry['id']} has multiple matches for {pattern}: "
                 + ", ".join(str(path.relative_to(root)) for path in matches))
        if not matches:
            if entry["required"]:
                fail(f"required artifact {entry['id']} is missing (expected {pattern})")
            continue
        path = matches[0]
        if entry["kind"] == "source-archive":
            validate_source_archive(path)
        elif entry["kind"] in {"python-wheel", "python-sdist"}:
            validate_python_metadata(path, version, entry["kind"])
        found.append(f"{entry['id']}: {path.relative_to(root)}")
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True,
                        help="directory containing staged release artifacts")
    parser.add_argument("--version", required=True,
                        help="numeric Quarry package version, for example 0.1.7")
    parser.add_argument("--tag", help="release tag, default: v<version>")
    parser.add_argument("--manifest", type=Path,
                        default=Path(__file__).parents[1] / "release" / "artifact-manifest.json")
    args = parser.parse_args(argv)
    try:
        for item in validate(args.root, args.manifest, args.version,
                             args.tag or f"v{args.version}"):
            print(item)
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f"release artifact validation failed: {error}", file=sys.stderr)
        return 1
    print("release artifact validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
