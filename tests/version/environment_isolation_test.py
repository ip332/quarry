"""Run the real version scenarios with hostile inherited Git environment state.

Regression for #94. A pre-push hook started from a linked worktree inherits an
absolute GIT_DIR. Before the fix, the version scenarios' fixture commands then
reinitialized the pushing repository (core.bare=true), wrote the fixture author
into its shared config, replaced its index and committed onto its branch.

A disposable guard repository with a linked worktree stands in for the pushing
repository. The scenarios run twice: from a pre-push hook of a real `git push`
in the guard worktree, and directly with every repository-selection and
GIT_CONFIG* variable aimed at the guard. The guard's refs, HEADs, indexes,
configuration and working trees must be unchanged afterwards.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from typing import NamedTuple

from version_resolution_test import isolated_git_environment

SCENARIOS = Path(__file__).with_name("version_resolution_test.py")


def git(clean: dict[str, str], cwd: Path, *arguments: str) -> str:
    return subprocess.run(["git", *arguments], cwd=cwd, env=clean, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


class Guard(NamedTuple):
    main: Path
    worktree: Path
    worktree_git: Path  # the linked worktree's private gitdir
    remote: Path


def make_guard(root: Path, clean: dict[str, str]) -> Guard:
    """Create a guard repository, a linked worktree and a bare push target."""
    main = root / "guard"
    main.mkdir()
    git(clean, main, "init", "-q", "-b", "main")
    git(clean, main, "config", "user.name", "Guard repository")
    git(clean, main, "config", "user.email", "guard@example.invalid")
    (main / "sentinel").write_text("must remain untouched\n", encoding="utf-8")
    git(clean, main, "add", "sentinel")
    git(clean, main, "commit", "-qm", "guard baseline")
    git(clean, main, "tag", "guard-tag")
    (main / "untracked").write_text("untracked guard file\n", encoding="utf-8")

    worktree = root / "guard worktree"
    git(clean, main, "worktree", "add", "-q", "-b", "pushing", str(worktree))
    (worktree / "work").write_text("staged worktree change\n", encoding="utf-8")
    git(clean, worktree, "add", "work")

    worktree_git = Path(git(clean, worktree, "rev-parse", "--absolute-git-dir").strip())
    remote = root / "remote.git"
    git(clean, root, "init", "-q", "--bare", str(remote))
    return Guard(main, worktree, worktree_git, remote)


def observe(clean: dict[str, str], cwd: Path, *arguments: str) -> str:
    # A damaged guard (for example core.bare=true) makes Git fail; record that
    # as part of the state instead of aborting the comparison.
    result = subprocess.run(["git", *arguments], cwd=cwd, env=clean, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return f"exit {result.returncode}: {result.stdout}"


def snapshot(guard: Guard, clean: dict[str, str]) -> dict[str, object]:
    """Everything a leaked fixture command could change in the guard."""
    main, worktree, worktree_git = guard.main, guard.worktree, guard.worktree_git
    common = main / ".git"
    files: dict[str, bytes] = {}
    for tree in (main, worktree):
        for path in sorted(tree.rglob("*")):
            relative = path.relative_to(tree)
            # The main .git directory is covered field by field below; the
            # worktree's .git file (its gitdir pointer) is compared as content.
            if tree == main and relative.parts[0] == ".git":
                continue
            if path.is_file():
                files[f"{tree.name}/{relative.as_posix()}"] = path.read_bytes()
    state: dict[str, object] = {
        "refs": observe(clean, main, "for-each-ref", "--format=%(refname) %(objectname)"),
        "main HEAD": (common / "HEAD").read_bytes(),
        "worktree HEAD": (worktree_git / "HEAD").read_bytes(),
        "main index": (common / "index").read_bytes(),
        "worktree index": (worktree_git / "index").read_bytes(),
        "config": (common / "config").read_bytes(),
        "main status": observe(clean, main, "status", "--porcelain=v1", "--untracked-files=all"),
        "worktree status": observe(clean, worktree, "status", "--porcelain=v1",
                                   "--untracked-files=all"),
        "working trees": files,
    }
    worktree_config = worktree_git / "config.worktree"
    state["worktree config"] = worktree_config.read_bytes() if worktree_config.exists() else None
    return state


def describe_changes(before: dict[str, object], after: dict[str, object]) -> str:
    return ", ".join(key for key in before if before[key] != after[key])


def scenario_command(args: argparse.Namespace) -> list[str]:
    return [sys.executable, str(SCENARIOS), "--cmake", args.cmake, "--resolver",
            str(args.resolver)]


def run_from_pre_push_hook(args: argparse.Namespace, root: Path, clean: dict[str, str],
                           guard: Guard) -> None:
    hooks = root / "hooks"
    hooks.mkdir()
    log = root / "hook.log"
    hook = hooks / "pre-push"
    hook.write_text("#!/bin/sh\nexec " + shlex.join(scenario_command(args)) + " > " +
                    shlex.quote(str(log)) + " 2>&1\n", encoding="utf-8")
    hook.chmod(0o755)
    # Hooks run with the environment Git itself exports to them.
    result = subprocess.run(["git", "-c", f"core.hooksPath={hooks}", "push", "-q", str(guard.remote),
                             "HEAD:refs/heads/pushed"], cwd=guard.worktree, env=clean, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    hook_output = log.read_text(encoding="utf-8") if log.exists() else ""
    assert result.returncode == 0, ("pre-push version scenarios failed:\n" + result.stderr +
                                    hook_output)


def run_with_hostile_variables(args: argparse.Namespace, root: Path, clean: dict[str, str],
                               guard: Guard) -> None:
    main, worktree, worktree_git = guard.main, guard.worktree, guard.worktree_git
    # If any of these leaked into a fixture, its commands would target the
    # guard, find no work tree (core.bare=true in repository-level config), or
    # run the hostile hooks, which fail every fixture commit. Global and system
    # configuration ignore core.bare, so the hooks make their leaks visible.
    hostile_hooks = root / "hostile hooks"
    hostile_hooks.mkdir()
    for hook_name in ("pre-commit", "commit-msg"):
        hook = hostile_hooks / hook_name
        hook.write_text("#!/bin/sh\necho 'hostile inherited Git configuration' >&2\nexit 1\n",
                        encoding="utf-8")
        hook.chmod(0o755)
    hostile_config = root / "hostile.gitconfig"
    hostile_config.write_text("[core]\n\tbare = true\n\thooksPath = " +
                              hostile_hooks.as_posix() + "\n[user]\n\tname = Hostile\n",
                              encoding="utf-8")
    hostile = dict(clean)
    hostile.update(
        GIT_DIR=str(worktree_git),
        GIT_WORK_TREE=str(worktree),
        GIT_COMMON_DIR=str(main / ".git"),
        GIT_INDEX_FILE=str(worktree_git / "index"),
        GIT_OBJECT_DIRECTORY=str(main / ".git" / "objects"),
        GIT_ALTERNATE_OBJECT_DIRECTORIES=str(main / ".git" / "objects"),
        GIT_NAMESPACE="hostile",
        GIT_CONFIG=str(hostile_config),
        GIT_CONFIG_GLOBAL=str(hostile_config),
        GIT_CONFIG_SYSTEM=str(hostile_config),
        GIT_CONFIG_PARAMETERS=f"'core.bare'='true' 'core.hookspath'='{hostile_hooks}'",
        GIT_CONFIG_COUNT="2",
        GIT_CONFIG_KEY_0="core.bare",
        GIT_CONFIG_VALUE_0="true",
        GIT_CONFIG_KEY_1="core.hooksPath",
        GIT_CONFIG_VALUE_1=str(hostile_hooks),
    )
    result = subprocess.run(scenario_command(args), env=hostile, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert result.returncode == 0, ("version scenarios failed with hostile Git variables:\n" +
                                    result.stdout + result.stderr)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--resolver", type=Path, required=True)
    args = parser.parse_args()
    # The scenarios run from other directories (the guard worktree, fixtures).
    args.cmake = str(Path(args.cmake).resolve()) if os.sep in args.cmake else args.cmake
    args.resolver = args.resolver.resolve()
    clean = isolated_git_environment()

    for name, run_scenarios in (("pre-push hook", "hook"), ("hostile variables", "hostile")):
        with tempfile.TemporaryDirectory(prefix="quarry-version-guard-") as temporary:
            root = Path(temporary)
            guard = make_guard(root, clean)
            before = snapshot(guard, clean)
            failure = None
            try:
                if run_scenarios == "hook":
                    run_from_pre_push_hook(args, root, clean, guard)
                else:
                    run_with_hostile_variables(args, root, clean, guard)
            except AssertionError as error:
                failure = error
            after = snapshot(guard, clean)
            changed = describe_changes(before, after)
            # Report a modified guard first: that is the defect being guarded.
            assert not changed, f"{name}: version scenarios modified the guard's {changed}"
            if failure is not None:
                raise failure
            print(f"{name}: guard repository unchanged")


if __name__ == "__main__":
    main()
