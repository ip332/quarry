"""Regression tests for scripts/quarry-validate and its pre-push hook (#89).

Every test builds a disposable repository and a bare remote, commits a copy of
the validator and a tiny CMake project that uses the real
cmake/QuarryVersion.cmake resolver, installs the real hook, and pushes with
plain `git push`. Nothing here touches the repository that runs the tests.
"""

from __future__ import annotations

import argparse
import contextlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest

SCRIPT: Path
RESOLVER: Path
VERSION_TEMPLATE: Path
TIMEOUT = 300

_REPOSITORY_LOCAL_GIT_VARIABLES = frozenset({
    "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_COMMON_DIR", "GIT_CONFIG",
    "GIT_CONFIG_COUNT", "GIT_CONFIG_PARAMETERS", "GIT_DIR", "GIT_GRAFT_FILE",
    "GIT_IMPLICIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_INTERNAL_SUPER_PREFIX",
    "GIT_NAMESPACE", "GIT_NO_REPLACE_OBJECTS", "GIT_OBJECT_DIRECTORY", "GIT_PREFIX",
    "GIT_REPLACE_REF_BASE", "GIT_SHALLOW_FILE", "GIT_WORK_TREE", "GIT_EXEC_PATH",
})

PRESETS = {
    "version": 6,
    "configurePresets": [{"name": "debug", "generator": "Unix Makefiles",
                          "binaryDir": "${sourceDir}/build/debug"}],
    "buildPresets": [{"name": "debug", "configurePreset": "debug"}],
    "testPresets": [{"name": "debug", "configurePreset": "debug",
                     "output": {"outputOnFailure": True}}],
}

CMAKELISTS = """\
cmake_minimum_required(VERSION 3.20)
include(cmake/QuarryVersion.cmake)
quarry_resolve_version()
project(quarry_validate_fixture VERSION ${QUARRY_VERSION} LANGUAGES NONE)
configure_file(include/quarry/version.hpp.in
               ${CMAKE_BINARY_DIR}/generated/quarry/version.hpp @ONLY)
# Like CC/CFLAGS, read from the environment only on the first configure.
set(FIXTURE_FLAVOR "$ENV{QUARRY_FIXTURE_FLAVOR}" CACHE STRING "first-configure flavor")
add_custom_target(fixture_build ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/built.stamp)
enable_testing()
add_test(NAME fixture_check
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/check.cmake)
"""

CHECK = """\
if(DEFINED ENV{QUARRY_FIXTURE_TRACE})
    file(APPEND "$ENV{QUARRY_FIXTURE_TRACE}"
        "GIT_DIR=[$ENV{GIT_DIR}] GIT_CONFIG_PARAMETERS=[$ENV{GIT_CONFIG_PARAMETERS}] "
        "GIT_WORK_TREE=[$ENV{GIT_WORK_TREE}] GIT_INDEX_FILE=[$ENV{GIT_INDEX_FILE}] "
        "source=[${SOURCE_DIR}]\\n")
endif()
if(EXISTS "${SOURCE_DIR}/SLOW")
    file(READ "${SOURCE_DIR}/SLOW" seconds)
    string(STRIP "${seconds}" seconds)
    execute_process(COMMAND ${CMAKE_COMMAND} -E sleep ${seconds})
endif()
if(EXISTS "${SOURCE_DIR}/FAIL")
    message(FATAL_ERROR "fixture configured to fail")
endif()
"""


def base_environment(home: Path, trace: Path) -> dict[str, str]:
    """os.environ without repository selection (#94), with a private HOME."""
    environment = {key: value for key, value in os.environ.items()
                   if key not in _REPOSITORY_LOCAL_GIT_VARIABLES
                   and not key.startswith("GIT_CONFIG") and not key.startswith("QUARRY_")}
    environment.update({
        "HOME": str(home),
        "XDG_CONFIG_HOME": str(home / ".config"),
        "GIT_AUTHOR_NAME": "Quarry Validate Test",
        "GIT_AUTHOR_EMAIL": "quarry-validate-test@example.invalid",
        "GIT_COMMITTER_NAME": "Quarry Validate Test",
        "GIT_COMMITTER_EMAIL": "quarry-validate-test@example.invalid",
        "QUARRY_FIXTURE_TRACE": str(trace),
        "QUARRY_VALIDATE_BUILD_JOBS": "2",
        "QUARRY_VALIDATE_TEST_JOBS": "2",
    })
    return environment


class Fixture:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.home = root / "home"
        self.home.mkdir()
        (self.home / ".gitconfig").write_text(
            "[init]\n\tdefaultBranch = main\n[advice]\n\tdetachedHead = false\n",
            encoding="utf-8")
        self.trace = root / "trace.txt"
        self.environment = base_environment(self.home, self.trace)
        self.remote = root / "remote.git"
        self.work = root / "work"
        self.git("init", "--bare", "--quiet", str(self.remote), cwd=root)
        self.git("init", "--quiet", str(self.work), cwd=root)
        self.git("remote", "add", "origin", str(self.remote))
        self.write("git_version", "0.1\n")
        self.write("CMakePresets.json", json.dumps(PRESETS, indent=2) + "\n")
        self.write("CMakeLists.txt", CMAKELISTS)
        self.write("check.cmake", CHECK)
        self.write(".gitignore", "build/\n")
        (self.work / "cmake").mkdir()
        shutil.copy(RESOLVER, self.work / "cmake" / "QuarryVersion.cmake")
        (self.work / "include" / "quarry").mkdir(parents=True)
        shutil.copy(VERSION_TEMPLATE, self.work / "include" / "quarry" / "version.hpp.in")
        (self.work / "scripts").mkdir()
        shutil.copy(SCRIPT, self.work / "scripts" / "quarry-validate")
        self.git("add", "-A")
        self.git("commit", "--quiet", "-m", "fixture")
        result = self.validator("install-hook")
        assert result.returncode == 0, result.stderr

    # -- helpers ------------------------------------------------------------

    def run(self, command: list[str], cwd: Path | None = None, check: bool = True,
            environment: dict[str, str] | None = None,
            stdin: str | None = None) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(command, cwd=cwd or self.work,
                                env=environment or self.environment, input=stdin,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                timeout=TIMEOUT, check=False)
        if check and result.returncode != 0:
            raise AssertionError(f"{command} failed ({result.returncode}):\n"
                                 f"{result.stdout}\n{result.stderr}")
        return result

    def git(self, *arguments: str, cwd: Path | None = None, check: bool = True,
            environment: dict[str, str] | None = None) -> str:
        return self.run(["git", *arguments], cwd=cwd, check=check,
                        environment=environment).stdout.strip()

    def write(self, name: str, content: str, cwd: Path | None = None) -> None:
        path = (cwd or self.work) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def commit(self, message: str, cwd: Path | None = None, **files: str) -> str:
        for name, content in files.items():
            self.write(name, content, cwd=cwd)
        self.git("add", "-A", cwd=cwd)
        self.git("commit", "--quiet", "--allow-empty", "-m", message, cwd=cwd)
        return self.git("rev-parse", "HEAD", cwd=cwd)

    def validator(self, *arguments: str, cwd: Path | None = None,
                  environment: dict[str, str] | None = None
                  ) -> subprocess.CompletedProcess[str]:
        return self.run([sys.executable, str((cwd or self.work) / "scripts" / "quarry-validate"),
                         *arguments], cwd=cwd, check=False, environment=environment)

    def validate(self, *revisions: str, cwd: Path | None = None,
                 environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        return self.validator("validate", *revisions, cwd=cwd, environment=environment)

    def push(self, *arguments: str, cwd: Path | None = None,
             environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        return self.run(["git", "push", *arguments], cwd=cwd, check=False,
                        environment=environment)

    def remote_ref(self, ref: str) -> str:
        return self.git("--git-dir", str(self.remote), "rev-parse", "--verify", "--quiet", ref,
                        check=False)

    @property
    def common_dir(self) -> Path:
        return self.work / ".git"

    def receipts(self, commit: str) -> list[dict]:
        directory = self.common_dir / "quarry-validation" / "receipts" / commit
        if not directory.is_dir():
            return []
        return [json.loads(path.read_text(encoding="utf-8"))
                for path in sorted(directory.glob("*.json"))]

    def with_environment(self, **changes: str) -> dict[str, str]:
        environment = dict(self.environment)
        environment.update(changes)
        return environment


class QuarryValidateTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory(prefix="quarry-validate-test-")
        self.fixture = Fixture(Path(os.path.realpath(self._temporary.name)))

    def tearDown(self) -> None:
        self._temporary.cleanup()

    def assertPushRejected(self, result, *fragments: str) -> None:
        self.assertNotEqual(result.returncode, 0, result.stderr)
        for fragment in fragments:
            self.assertIn(fragment, result.stderr)

    # -- basic receipts --------------------------------------------------------

    def test_push_without_receipt_is_rejected(self) -> None:
        f = self.fixture
        head = f.git("rev-parse", "HEAD")
        result = f.push("origin", "main")
        self.assertPushRejected(result, "not validated", head,
                                "scripts/quarry-validate validate " + head)
        self.assertEqual(f.remote_ref("refs/heads/main"), "")

    def test_validated_commit_is_pushed_with_hooks_enabled(self) -> None:
        f = self.fixture
        head = f.git("rev-parse", "HEAD")
        result = f.validate()
        self.assertEqual(result.returncode, 0, result.stderr)
        [receipt] = f.receipts(head)
        self.assertEqual(receipt["result"], "pass")
        self.assertEqual(receipt["commit"], head)
        self.assertEqual(set(receipt["steps"]), {"configure", "build", "test"})
        self.assertEqual(receipt["version_metadata"]["revision"], 0)
        # Git prepends its exec-path to PATH for hooks; that alone must not
        # invalidate the receipt.
        result = f.push("origin", "main")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("checked 1 commit(s) for 1 ref(s)", result.stderr)
        self.assertEqual(f.remote_ref("refs/heads/main"), head)

    def test_failed_validation_does_not_authorize_push(self) -> None:
        f = self.fixture
        head = f.commit("make the test fail", FAIL="1\n")
        result = f.validate()
        self.assertEqual(result.returncode, 1)
        self.assertIn("FAIL " + head, result.stderr)
        [receipt] = f.receipts(head)
        self.assertEqual((receipt["result"], receipt["failed_step"]), ("fail", "test"))
        self.assertPushRejected(f.push("origin", "main"), "validation failed (test")
        self.assertEqual(f.remote_ref("refs/heads/main"), "")

    def test_validation_uses_the_commit_not_the_working_tree(self) -> None:
        f = self.fixture
        head = f.git("rev-parse", "HEAD")
        f.write("FAIL", "untracked\n")
        f.write("check.cmake", 'message(FATAL_ERROR "uncommitted edit")\n')
        result = f.validate()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(f.receipts(head)[0]["result"], "pass")
        # The developer's files are untouched.
        self.assertEqual((f.work / "FAIL").read_text(encoding="utf-8"), "untracked\n")
        self.assertIn("uncommitted edit", (f.work / "check.cmake").read_text(encoding="utf-8"))

        f.git("checkout", "--", "check.cmake")
        bad = f.commit("committed failure", FAIL="1\n")
        (f.work / "FAIL").unlink()  # removed in the working tree only
        self.assertEqual(f.validate().returncode, 1)
        self.assertEqual(f.receipts(bad)[0]["result"], "fail")

    def test_amended_and_rebased_commits_need_new_validation(self) -> None:
        f = self.fixture
        original = f.commit("feature", feature="1\n")
        self.assertEqual(f.validate().returncode, 0)
        f.git("commit", "--quiet", "--amend", "-m", "feature, reworded")
        amended = f.git("rev-parse", "HEAD")
        self.assertNotEqual(original, amended)
        self.assertEqual(f.git("rev-parse", "HEAD^{tree}"), f.git("rev-parse",
                                                                   original + "^{tree}"))
        self.assertPushRejected(f.push("origin", "main"), amended[:12] + " not validated")

        base = f.git("rev-parse", "HEAD~1")
        f.git("checkout", "--quiet", "-b", "upstream", base)
        f.commit("upstream change", upstream="1\n")
        f.git("checkout", "--quiet", "main")
        f.git("rebase", "--quiet", "upstream")
        rebased = f.git("rev-parse", "HEAD")
        self.assertPushRejected(f.push("origin", "main"), rebased[:12] + " not validated")
        self.assertEqual(f.validate().returncode, 0)
        self.assertEqual(f.push("origin", "main").returncode, 0)

    # -- environment -------------------------------------------------------------

    def test_environment_change_invalidates_receipt(self) -> None:
        f = self.fixture
        self.assertEqual(f.validate(environment=f.with_environment(
            QUARRY_FIXTURE_FLAVOR="a")).returncode, 0)
        result = f.push("origin", "main", environment=f.with_environment(
            QUARRY_FIXTURE_FLAVOR="b"))
        self.assertPushRejected(result, "validated in a different environment",
                                "variables.QUARRY_FIXTURE_FLAVOR: a -> b")
        result = f.push("origin", "main", environment=f.with_environment(
            QUARRY_FIXTURE_FLAVOR="a"))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_different_tool_on_path_invalidates_receipt(self) -> None:
        f = self.fixture
        self.assertEqual(f.validate().returncode, 0)
        shim = f.root / "shim"
        shim.mkdir()
        make = shutil.which("make", path=f.environment["PATH"])
        assert make is not None
        wrapper = shim / "make"
        wrapper.write_text(f'#!/bin/sh\nexec "{make}" "$@"\n', encoding="utf-8")
        wrapper.chmod(0o755)
        environment = f.with_environment(PATH=f"{shim}{os.pathsep}{f.environment['PATH']}")
        self.assertPushRejected(f.push("origin", "main", environment=environment),
                                "tools.make.path")

    def test_toolchain_change_discards_cmake_cache(self) -> None:
        f = self.fixture
        cache = (f.common_dir / "quarry-validation" / "worktree" / "build" / "debug"
                 / "CMakeCache.txt")
        first = f.validate(environment=f.with_environment(QUARRY_FIXTURE_FLAVOR="a"))
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertIn("FIXTURE_FLAVOR:STRING=a", cache.read_text(encoding="utf-8"))
        again = f.validator("validate", "--force",
                            environment=f.with_environment(QUARRY_FIXTURE_FLAVOR="a"))
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertNotIn("discarding the CMake cache", again.stderr)
        changed = f.validate(environment=f.with_environment(QUARRY_FIXTURE_FLAVOR="b"))
        self.assertEqual(changed.returncode, 0, changed.stderr)
        self.assertIn("discarding the CMake cache", changed.stderr)
        self.assertIn("FIXTURE_FLAVOR:STRING=b", cache.read_text(encoding="utf-8"))

    def test_build_and_test_parallelism_are_independent(self) -> None:
        f = self.fixture
        head = f.git("rev-parse", "HEAD")
        result = f.validator("validate", "--build-jobs", "3", "--test-jobs", "5")
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = f.receipts(head)[0]
        self.assertEqual((receipt["build_jobs"], receipt["test_jobs"]), (3, 5))
        log = Path(receipt["log"]).read_text(encoding="utf-8")
        self.assertIn("cmake --build --preset debug --parallel 3", log)
        self.assertIn("ctest --preset debug -j 5 --no-tests=error", log)
        # Parallelism is not a validation input.
        self.assertEqual(f.push("origin", "main").returncode, 0)

    def test_version_metadata_change_invalidates_receipt(self) -> None:
        f = self.fixture
        head = f.commit("second", second="1\n")
        self.assertEqual(f.validate().returncode, 0)
        self.assertEqual(f.receipts(head)[0]["version_metadata"]["revision"], 1)
        # `git rev-parse --short` feeds QUARRY_GIT_SHA; its length is config-
        # and repository-size dependent.
        f.git("config", "core.abbrev", "16")
        self.assertPushRejected(f.push("origin", "main"),
                                "Git-derived version metadata changed", "short_sha")
        self.assertEqual(f.validate().returncode, 0)
        self.assertEqual(f.push("origin", "main").returncode, 0)

    # -- failure safety ----------------------------------------------------------

    def _start_validation(self, *arguments: str, stderr=subprocess.PIPE,
                          environment: dict[str, str] | None = None) -> subprocess.Popen:
        f = self.fixture
        return subprocess.Popen(
            [sys.executable, str(f.work / "scripts" / "quarry-validate"), "validate",
             *arguments], cwd=f.work, env=environment or f.environment,
            stdout=subprocess.DEVNULL,
            stderr=stderr, text=True, start_new_session=True)

    def _wait_for_test_step(self, commit: str) -> None:
        log = self.fixture.common_dir / "quarry-validation" / "logs" / f"{commit}.log"
        deadline = time.monotonic() + TIMEOUT
        while time.monotonic() < deadline:
            if log.exists() and "$ ctest" in log.read_text(encoding="utf-8"):
                time.sleep(0.5)
                return
            time.sleep(0.1)
        self.fail("validation never reached the test step")

    def _wait_for_receipt(self, commit: str, result: str) -> None:
        deadline = time.monotonic() + TIMEOUT
        while time.monotonic() < deadline:
            with contextlib.suppress(ValueError, OSError):
                if [r["result"] for r in self.fixture.receipts(commit)] == [result]:
                    return
            time.sleep(0.05)
        self.fail(f"no {result!r} receipt for {commit}")

    def test_killed_validation_leaves_no_authorization(self) -> None:
        f = self.fixture
        head = f.commit("slow", SLOW="30\n")
        process = self._start_validation()
        self._wait_for_test_step(head)
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        self.assertEqual([r["result"] for r in f.receipts(head)], ["running"])
        self.assertPushRejected(f.push("origin", "main"), "in progress or was killed")
        # A kernel lock: the next validation does not wait for the dead one.
        quick = f.commit("quick", SLOW="0\n")
        result = f.validate()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("waiting for the validation lock", result.stderr)
        self.assertEqual(f.receipts(quick)[0]["result"], "pass")

    def test_revalidation_removes_the_previous_pass_first(self) -> None:
        f = self.fixture
        head = f.git("rev-parse", "HEAD")
        self.assertEqual(f.validate().returncode, 0)
        # Hold the lock so the forced revalidation stops right after it has
        # invalidated the old receipt and recorded itself as running.
        slow = f.commit("slow", SLOW="30\n")
        holder = self._start_validation(slow)
        self._wait_for_test_step(slow)
        messages = f.root / "revalidation.err"
        with open(messages, "w", encoding="utf-8") as stream:
            # Revalidating in another environment must also withdraw the pass
            # recorded for the first one.
            revalidation = self._start_validation(
                "--force", head, stderr=stream,
                environment=f.with_environment(QUARRY_FIXTURE_FLAVOR="other"))
        try:
            deadline = time.monotonic() + TIMEOUT
            while "waiting for the validation lock" not in messages.read_text(encoding="utf-8"):
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.05)
            # Waiting for the lock: the old pass is still valid until it starts.
            self.assertEqual([r["result"] for r in f.receipts(head)], ["pass"])
            os.killpg(holder.pid, signal.SIGKILL)
            holder.wait()
            self._wait_for_receipt(head, "running")
            os.killpg(revalidation.pid, signal.SIGKILL)
            self.assertEqual(len(list((f.common_dir / "quarry-validation" / "receipts" / head)
                                      .glob("*.json"))), 1)
        finally:
            for process in (holder, revalidation):
                with contextlib.suppress(ProcessLookupError, PermissionError):
                    os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                if process.stderr is not None:
                    process.stderr.close()
        self.assertPushRejected(f.push("origin", f"{head}:refs/heads/main"), "not validated")

    def test_interrupt_records_interrupted(self) -> None:
        f = self.fixture
        head = f.commit("slow", SLOW="30\n")
        process = self._start_validation()
        self._wait_for_test_step(head)
        os.killpg(process.pid, signal.SIGINT)
        _, stderr = process.communicate(timeout=TIMEOUT)
        self.assertEqual(process.returncode, 130, stderr)
        self.assertEqual([r["result"] for r in f.receipts(head)], ["interrupted"])
        self.assertPushRejected(f.push("origin", "main"), "validation was interrupted")

    def test_concurrent_validations_are_serialized(self) -> None:
        f = self.fixture
        first = f.commit("first", SLOW="3\n")
        second = f.commit("second", SLOW="0\n", other="1\n")
        one = self._start_validation(first)
        self._wait_for_test_step(first)
        two = self._start_validation(second)
        _, err_two = two.communicate(timeout=TIMEOUT)
        _, err_one = one.communicate(timeout=TIMEOUT)
        self.assertEqual((one.returncode, two.returncode), (0, 0), err_one + err_two)
        self.assertIn("waiting for the validation lock", err_two)
        one_receipt, two_receipt = f.receipts(first)[0], f.receipts(second)[0]
        self.assertEqual((one_receipt["result"], two_receipt["result"]), ("pass", "pass"))
        self.assertLessEqual(one_receipt["finished"], two_receipt["started"])

    # -- refs -----------------------------------------------------------------

    def test_every_outgoing_ref_is_checked(self) -> None:
        f = self.fixture
        main = f.git("rev-parse", "HEAD")
        f.git("checkout", "--quiet", "-b", "other")
        other = f.commit("other", other="1\n")
        f.git("checkout", "--quiet", "main")
        self.assertEqual(f.validate("main").returncode, 0)
        result = f.push("origin", "main", "other")
        self.assertPushRejected(result, "refs/heads/other -> refs/heads/other: "
                                + other[:12] + " not validated")
        self.assertNotIn("refs/heads/main -> refs/heads/main", result.stderr)
        self.assertEqual((f.remote_ref("refs/heads/main"), f.remote_ref("refs/heads/other")),
                         ("", ""))
        self.assertEqual(f.validate("other").returncode, 0)
        result = f.push("origin", "main", "other")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("checked 2 commit(s) for 2 ref(s)", result.stderr)
        self.assertEqual((f.remote_ref("refs/heads/main"), f.remote_ref("refs/heads/other")),
                         (main, other))

    def test_deletions_need_no_receipt(self) -> None:
        f = self.fixture
        self.assertEqual(f.validate().returncode, 0)
        self.assertEqual(f.push("origin", "main", "main:refs/heads/doomed").returncode, 0)
        shutil.rmtree(f.common_dir / "quarry-validation" / "receipts")
        result = f.push("origin", ":refs/heads/doomed")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(f.remote_ref("refs/heads/doomed"), "")

    def test_new_ref_at_commit_already_on_remote_is_still_checked(self) -> None:
        f = self.fixture
        self.assertEqual(f.validate().returncode, 0)
        self.assertEqual(f.push("origin", "main").returncode, 0)
        shutil.rmtree(f.common_dir / "quarry-validation" / "receipts")
        self.assertPushRejected(f.push("origin", "main:refs/heads/copy"), "not validated")

    def test_tags_are_checked_through_their_commit(self) -> None:
        f = self.fixture
        validated = f.git("rev-parse", "HEAD")
        self.assertEqual(f.validate().returncode, 0)
        f.git("tag", "-a", "-m", "release", "v0.1.0")
        f.git("tag", "light")
        result = f.push("origin", "v0.1.0", "light")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("checked 1 commit(s) for 2 ref(s)", result.stderr)
        self.assertEqual(f.remote_ref("refs/tags/v0.1.0^{commit}"), validated)

        unvalidated = f.commit("unvalidated", extra="1\n")
        f.git("tag", "-a", "-m", "next", "v0.1.1")
        self.assertPushRejected(f.push("origin", "v0.1.1"),
                                "refs/tags/v0.1.1 -> refs/tags/v0.1.1: "
                                + unvalidated[:12] + " not validated")
        tree = f.git("rev-parse", "HEAD^{tree}")
        f.git("tag", "-a", "-m", "tree tag", "tree-tag", tree)
        self.assertPushRejected(f.push("origin", "tree-tag"), "does not point to a commit")

    # -- linked worktrees and #94 ----------------------------------------------

    def _repository_state(self) -> dict[str, bytes | str]:
        f = self.fixture
        git_dir = f.common_dir
        return {
            "config": (git_dir / "config").read_bytes(),
            "index": (git_dir / "index").read_bytes(),
            "HEAD": (git_dir / "HEAD").read_bytes(),
            "refs": f.git("for-each-ref", "--format=%(refname) %(objectname)"),
            "bare": f.git("config", "--bool", "core.bare"),
        }

    def test_linked_worktree_with_hook_environment_is_isolated(self) -> None:
        f = self.fixture
        linked = f.root / "linked"
        f.git("worktree", "add", "--quiet", "-b", "feature", str(linked))
        commit = f.commit("feature", cwd=linked, feature="1\n")
        before = self._repository_state()
        # What a hook (or an agent running inside one) inherits in a linked worktree.
        hostile = f.with_environment(
            GIT_DIR=str(f.common_dir / "worktrees" / "linked"),
            GIT_CONFIG_PARAMETERS="'user.name'='Intruder' 'core.bare'='true'",
            GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.bare", GIT_CONFIG_VALUE_0="true",
            GIT_INDEX_FILE=str(f.root / "foreign-index"))
        result = f.validate(cwd=linked, environment=hostile)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self._repository_state(), before)
        self.assertEqual(f.receipts(commit)[0]["result"], "pass")
        trace = f.trace.read_text(encoding="utf-8")
        self.assertIn("GIT_DIR=[] GIT_CONFIG_PARAMETERS=[] GIT_WORK_TREE=[] GIT_INDEX_FILE=[]",
                      trace)
        self.assertIn(str(f.common_dir / "quarry-validation" / "worktree"), trace)
        # The receipt is shared: the push works from the linked worktree.
        result = f.push("origin", "feature", cwd=linked)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(f.remote_ref("refs/heads/feature"), commit)
        self.assertEqual(self._repository_state()["config"], before["config"])

    # -- push and install-hook commands ------------------------------------------

    def test_push_command_validates_what_the_push_needs(self) -> None:
        f = self.fixture
        f.git("checkout", "--quiet", "-b", "topic")
        topic = f.commit("topic", topic="1\n")
        result = f.validator("push", "--build-jobs", "2", "-u", "origin", "main", "topic")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("the push needs 2 unvalidated commit(s)", result.stderr)
        self.assertEqual(f.remote_ref("refs/heads/topic"), topic)
        self.assertEqual(f.git("rev-parse", "--abbrev-ref", "topic@{upstream}"), "origin/topic")
        again = f.validator("push", "origin", "topic")
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertNotIn("unvalidated", again.stderr)
        # `--` hands option-like arguments to git push unchanged.
        explicit = f.validator("push", "--test-jobs=1", "--", "--force", "origin", "topic")
        self.assertEqual(explicit.returncode, 0, explicit.stderr)
        self.assertNotIn("unvalidated", explicit.stderr)

    def test_push_command_reports_failed_validation(self) -> None:
        f = self.fixture
        f.commit("broken", FAIL="1\n")
        result = f.validator("push", "origin", "main")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(f.remote_ref("refs/heads/main"), "")

    def test_install_hook_replaces_pre_commit_hook_and_keeps_custom_hooks(self) -> None:
        f = self.fixture
        hook = f.common_dir / "hooks" / "pre-push"
        hook.write_text("#!/bin/sh\n# File generated by pre-commit: https://pre-commit.com\n",
                        encoding="utf-8")
        result = f.validator("install-hook")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Quarry pre-push receipt check", hook.read_text(encoding="utf-8"))
        self.assertIn("pre-commit",
                      (hook.parent / "pre-push.pre-commit-backup").read_text(encoding="utf-8"))
        hook.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        self.assertEqual(f.validator("install-hook").returncode, 2)
        self.assertEqual(hook.read_text(encoding="utf-8"), "#!/bin/sh\nexit 0\n")
        self.assertEqual(f.validator("push", "origin", "main").returncode, 2)
        self.assertEqual(f.validator("install-hook", "--force").returncode, 0)
        self.assertIn("Quarry pre-push receipt check", hook.read_text(encoding="utf-8"))

    def test_hook_fails_closed_without_validator_in_checkout(self) -> None:
        f = self.fixture
        f.git("rm", "--quiet", "scripts/quarry-validate")
        f.commit("drop validator")
        self.assertPushRejected(f.push("origin", "main"), "scripts/quarry-validate is missing")

    def test_status_reports_receipt_state(self) -> None:
        f = self.fixture
        head = f.git("rev-parse", "HEAD")
        result = f.validator("status")
        self.assertEqual(result.returncode, 1)
        self.assertIn(f"INVALID {head} HEAD: not validated", result.stdout)
        self.assertEqual(f.validate().returncode, 0)
        result = f.validator("status", "main")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn(f"valid   {head} main: validated", result.stdout)


def main() -> None:
    global SCRIPT, RESOLVER, VERSION_TEMPLATE
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True, type=Path)
    parser.add_argument("--resolver", required=True, type=Path)
    parser.add_argument("--version-template", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    SCRIPT, RESOLVER, VERSION_TEMPLATE = args.script, args.resolver, args.version_template
    unittest.main(argv=[sys.argv[0], *remaining], verbosity=2)


if __name__ == "__main__":
    main()
