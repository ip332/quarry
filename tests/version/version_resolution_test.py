"""History-shape tests for the CMake Git-derived version resolver."""

from __future__ import annotations

import argparse
import functools
import os
from pathlib import Path
import subprocess
import tempfile


# Variables that select or reconfigure a repository instead of describing the
# user. Git hooks export some of them (an absolute GIT_DIR in a linked
# worktree), and any of them would redirect fixture commands into the caller's
# repository. Git reports its own list; this copy covers older Git releases.
_REPOSITORY_LOCAL_GIT_VARIABLES = frozenset({
    "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_COMMON_DIR", "GIT_CONFIG",
    "GIT_CONFIG_COUNT", "GIT_CONFIG_PARAMETERS", "GIT_DIR", "GIT_GRAFT_FILE",
    "GIT_IMPLICIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_INTERNAL_SUPER_PREFIX",
    "GIT_NAMESPACE", "GIT_NO_REPLACE_OBJECTS", "GIT_OBJECT_DIRECTORY", "GIT_PREFIX",
    "GIT_REPLACE_REF_BASE", "GIT_SHALLOW_FILE", "GIT_WORK_TREE",
})


def _is_repository_selection_variable(key: str, reported: frozenset[str]) -> bool:
    # GIT_CONFIG* also covers GIT_CONFIG_KEY_<n>/GIT_CONFIG_VALUE_<n>,
    # GIT_CONFIG_GLOBAL, GIT_CONFIG_SYSTEM and GIT_CONFIG_NOSYSTEM.
    return (key in _REPOSITORY_LOCAL_GIT_VARIABLES or key in reported or
            key.startswith("GIT_CONFIG"))


def isolated_git_environment(base: dict[str, str] | None = None) -> dict[str, str]:
    """Return `base` (default: os.environ) without inherited repository state.

    Every fixture repository, and every CMake run that lets the resolver call
    Git on one, must use this environment. Other variables, such as PATH,
    GIT_SSH or author identity, are kept. Global and system configuration are
    replaced with empty ones so fixtures behave the same on every machine.
    """
    source = dict(os.environ if base is None else base)
    without_git = {key: value for key, value in source.items() if not key.startswith("GIT_")}
    reported = subprocess.run(["git", "rev-parse", "--local-env-vars"], env=without_git,
                              check=True, text=True, stdout=subprocess.PIPE).stdout.split()
    environment = {key: value for key, value in source.items()
                   if not _is_repository_selection_variable(key, frozenset(reported))}
    environment["GIT_CONFIG_GLOBAL"] = os.devnull
    environment["GIT_CONFIG_NOSYSTEM"] = "1"
    return environment


@functools.cache
def fixture_environment() -> dict[str, str]:
    return isolated_git_environment()


def run(command: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, check=check, text=True,
                          env=fixture_environment(),
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
                           cwd=source, check=True, stdout=output, env=fixture_environment())
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
