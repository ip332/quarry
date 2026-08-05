"""History-shape tests for the CMake Git-derived version resolver."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile


def run(command: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def write_project(root: Path, resolver: Path) -> None:
    (root / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        f"include(\"{resolver}\")\n"
        "quarry_resolve_version()\n"
        "file(WRITE \"${CMAKE_BINARY_DIR}/result.txt\"\n"
        "  \"${QUARRY_VERSION}|${QUARRY_VERSION_DISPLAY}|${QUARRY_VERSION_DIRTY}|${QUARRY_VERSION_SOURCE}\")\n",
        encoding="utf-8")


def configure(root: Path, cmake: str, expect_success: bool = True) -> str:
    build = root / "build"
    result = run([cmake, "-S", str(root), "-B", str(build)], check=False)
    assert (result.returncode == 0) == expect_success, result.stderr
    if not expect_success:
        return result.stderr
    return (build / "result.txt").read_text(encoding="utf-8")


def commit(root: Path, message: str) -> None:
    run(["git", "add", "-A"], cwd=root)
    run(["git", "commit", "-m", message], cwd=root)


def archive_fallback_scenario(cmake: str, resolver: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="quarry-version-archive-test-") as name:
        source = Path(name) / "source"
        source.mkdir()
        run(["git", "init", "-q"], cwd=source)
        run(["git", "config", "user.email", "test@example.invalid"], cwd=source)
        run(["git", "config", "user.name", "Quarry test"], cwd=source)
        (source / "git_version").write_text("0.3\n", encoding="utf-8")
        (source / ".gitattributes").write_text(
            "cmake/QuarryResolvedVersion.cmake export-subst\n", encoding="utf-8")
        (source / "cmake").mkdir()
        (source / "cmake" / "QuarryResolvedVersion.cmake").write_text(
            'set(QUARRY_ARCHIVE_TAG "$Format:%(describe:tags)$")\n'
            'set(QUARRY_GIT_SHA "$Format:%H$")\n', encoding="utf-8")
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\n"
            f"include(\"{resolver}\")\n"
            "quarry_resolve_version()\n"
            "file(WRITE \"${CMAKE_BINARY_DIR}/result.txt\" "
            "\"${QUARRY_VERSION}|${QUARRY_VERSION_SOURCE}|${QUARRY_GIT_SHA}\")\n",
            encoding="utf-8")
        commit(source, "establish archive version")
        run(["git", "tag", "v0.3.8-rc.1"], cwd=source)
        archive = Path(name) / "archive.tar"
        with archive.open("wb") as output:
            subprocess.run(["git", "archive", "--format=tar", "HEAD"],
                           cwd=source, check=True, stdout=output)
        extracted = Path(name) / "extracted"
        extracted.mkdir()
        run(["tar", "-xf", str(archive), "-C", str(extracted)])
        result = configure(extracted, cmake)
        assert result.startswith("0.3.8|packaged-archive|"), result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--resolver", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="quarry-version-test-") as name:
        root = Path(name)
        run(["git", "init", "-q"], cwd=root)
        run(["git", "config", "user.email", "test@example.invalid"], cwd=root)
        run(["git", "config", "user.name", "Quarry test"], cwd=root)
        (root / "git_version").write_text("0.1\n", encoding="utf-8")
        write_project(root, args.resolver)
        commit(root, "establish version")
        assert configure(root, args.cmake).startswith("0.1.0|0.1.0|FALSE|git")

        (root / "marker").write_text("next\n", encoding="utf-8")
        commit(root, "follow-up")
        assert configure(root, args.cmake).startswith("0.1.1|0.1.1|FALSE|git")

        (root / "git_version").write_text("0.2\n", encoding="utf-8")
        commit(root, "new development line")
        assert configure(root, args.cmake).startswith("0.2.0|0.2.0|FALSE|git")

        (root / "marker").write_text("dirty\n", encoding="utf-8")
        assert configure(root, args.cmake).startswith("0.2.0|0.2.0-dirty|TRUE|git")
        (root / "untracked").write_text("ignored\n", encoding="utf-8")
        assert configure(root, args.cmake).startswith("0.2.0|0.2.0-dirty|TRUE|git")
        run(["git", "checkout", "--", "marker"], cwd=root)

        fallback_root = root / "fallback"
        fallback_root.mkdir()
        (fallback_root / "git_version").write_text("0.3\n", encoding="utf-8")
        (fallback_root / "cmake").mkdir()
        (fallback_root / "cmake" / "QuarryResolvedVersion.cmake").write_text(
            'set(QUARRY_VERSION "0.3.7")\nset(QUARRY_GIT_SHA "abc1234")\n', encoding="utf-8")
        write_project(fallback_root, args.resolver)
        assert configure(fallback_root, args.cmake).startswith("0.3.7|0.3.7|FALSE|packaged-fallback")

        archive_root = root / "archive-fallback"
        archive_root.mkdir()
        (archive_root / "git_version").write_text("0.3\n", encoding="utf-8")
        (archive_root / "cmake").mkdir()
        (archive_root / "cmake" / "QuarryResolvedVersion.cmake").write_text(
            'set(QUARRY_ARCHIVE_TAG "v0.3.8-rc.1")\n'
            'set(QUARRY_GIT_SHA "archive123")\n', encoding="utf-8")
        write_project(archive_root, args.resolver)
        assert configure(archive_root, args.cmake).startswith("0.3.8|0.3.8|FALSE|packaged-archive")

        mismatched_archive = root / "mismatched-archive"
        mismatched_archive.mkdir()
        (mismatched_archive / "git_version").write_text("0.3\n", encoding="utf-8")
        (mismatched_archive / "cmake").mkdir()
        (mismatched_archive / "cmake" / "QuarryResolvedVersion.cmake").write_text(
            'set(QUARRY_ARCHIVE_TAG "v0.4.8")\n', encoding="utf-8")
        write_project(mismatched_archive, args.resolver)
        assert "does not match git_version" in configure(
            mismatched_archive, args.cmake, expect_success=False)

        malformed_root = root / "malformed"
        malformed_root.mkdir()
        (malformed_root / "git_version").write_text("0.3.7\n", encoding="utf-8")
        write_project(malformed_root, args.resolver)
        assert "Major.Minor" in configure(malformed_root, args.cmake, expect_success=False)

    archive_fallback_scenario(args.cmake, args.resolver)


if __name__ == "__main__":
    main()
