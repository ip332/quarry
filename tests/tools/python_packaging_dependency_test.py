"""Exercise actual packaging-test registration with and without Python build."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import venv


PACKAGING_TESTS = {"python_packaging_test", "public_examples_test"}


def run(command, *, cwd, env=None, success=True):
    result = subprocess.run(list(map(str, command)), cwd=cwd, env=env,
                            text=True, capture_output=True, check=False)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--ctest", required=True)
    parser.add_argument("--gtest-source", default="")
    args = parser.parse_args()
    source = args.source.resolve()
    with tempfile.TemporaryDirectory(prefix="quarry-packaging-dependency-") as temporary:
        root = Path(temporary)
        missing = root / "missing-build"
        venv.EnvBuilder(with_pip=False).create(missing)
        python = missing / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        environment.pop("PYTHONHOME", None)
        site = Path(run([python, "-c",
                         "import sysconfig; print(sysconfig.get_path('purelib'))"],
                        cwd=root, env=environment).strip())
        # Reuse genuine setuptools, including its bundled dependencies, without
        # installing/removing anything in the selected interpreter or using pip.
        for name in ("setuptools", "_distutils_hack"):
            spec = importlib.util.find_spec(name)
            assert spec is not None and spec.origin, name
            shutil.copytree(Path(spec.origin).parent, site / name,
                            ignore=shutil.ignore_patterns("__pycache__"))

        # Reproduce both original symptoms in a disposable namespace directory.
        shadow = root / "shadow"
        (shadow / "build").mkdir(parents=True)
        run([python, "-c", "import build, setuptools; assert build.__file__ is None"],
            cwd=shadow, env=environment)
        run([python, "-m", "build", "--version"], cwd=shadow,
            env=environment, success=False)
        run([sys.executable, "-c", "import build.__main__, setuptools"], cwd=shadow)

        outside = root / "out-of-source"
        (outside / "build").mkdir(parents=True)
        for label, interpreter, expected in (("absent", python, set()),
                                              ("installed", sys.executable, PACKAGING_TESTS)):
            for location, cwd in (("root", source), ("outside", outside)):
                build = root / (label + "-" + location)
                # A directory change alone is insufficient: also put a build
                # namespace in the probe's fixed binary-directory location.
                (build / "tests" / "build").mkdir(parents=True)
                command = [args.cmake, "-S", source, "-B", build,
                           "-DCMAKE_BUILD_TYPE=Debug",
                           "-DQUARRY_PYTHON3_EXECUTABLE=" + str(interpreter)]
                if args.gtest_source:
                    command.append("-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=" + args.gtest_source)
                output = run(command, cwd=cwd,
                             env=environment if label == "absent" else None)
                listing = json.loads(run([args.ctest, "--test-dir", build,
                                          "--show-only=json-v1"], cwd=cwd))
                tests = {test["name"]: test for test in listing["tests"]}
                registered = tests.keys() & PACKAGING_TESTS
                assert registered == expected, (label, location, registered, expected, output)
                if label == "absent":
                    assert "Skipping Python packaging and public example tests" in output
                else:
                    for name in PACKAGING_TESTS:
                        assert tests[name]["command"][0] == str(interpreter), tests[name]
                print(label, location, "registration:", sorted(registered), flush=True)
        print("Original namespace false positive reproduced; real CMake registration "
              "rejects missing build and accepts installed dependencies from both locations.")


if __name__ == "__main__":
    main()
