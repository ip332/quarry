"""Clean wheel/sdist and installed downstream-consumer validation."""

from __future__ import annotations

import argparse
import importlib.metadata
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
from typing import Dict, List, Optional, Tuple
import venv
import zipfile


def run(command: List[str], *, env: Optional[Dict[str, str]] = None) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(command, check=True, text=True, env=env, capture_output=True)
    except subprocess.CalledProcessError as error:
        raise RuntimeError("command failed: " + " ".join(command) + "\n" +
                           error.stdout + error.stderr) from error


def python_in(venv_dir: Path) -> Path:
    return venv_dir / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def make_venv(path: Path) -> Path:
    venv.EnvBuilder(with_pip=True).create(path)
    return python_in(path)


def clean_env() -> dict[str, str]:
    environment = os.environ.copy()
    environment.pop("PYTHONPATH", None)
    environment.pop("PYTHONHOME", None)
    return environment


def install_wheel(python: Path, wheel: Path) -> None:
    run([str(python), "-m", "pip", "install", "--no-index", "--no-deps", str(wheel)],
        env=clean_env())


def assert_artifacts(dist: Path) -> Tuple[Path, Path]:
    wheels = list(dist.glob("quarry_runtime_python-*.whl"))
    # Build frontends differ in whether they normalize the distribution name
    # in sdist filenames (``quarry_runtime_python`` versus
    # ``quarry-runtime-python``). Accept either standards-compatible spelling
    # while still requiring exactly one source archive.
    sdists = (list(dist.glob("quarry*_python-*.tar.gz")) +
              list(dist.glob("quarry*-python-*.tar.gz")))
    assert len(wheels) == 1, wheels
    assert len(sdists) == 1, sdists
    wheel = wheels[0]
    sdist = sdists[0]
    with zipfile.ZipFile(wheel) as archive:
        names = set(archive.namelist())
        assert "quarry/runtime/python/binary_record.py" in names
        assert all(name.startswith(("quarry/", "quarry_runtime_python-")) for name in names)
    with tarfile.open(sdist) as archive:
        names = archive.getnames()
        assert any(name.endswith("/src/quarry/runtime/python/binary_record.py")
                   for name in names)
        assert any(name.endswith("/README.md") for name in names)
        assert all("quarry-main.tgz" not in name and ".git/" not in name for name in names)
    return wheel, sdist


def install_and_check_runtime(python: Path, wheel: Path) -> None:
    install_wheel(python, wheel)
    script = (
        "import importlib.metadata as m\n"
        "import quarry.runtime.python as rt\n"
        "from quarry.runtime.python import binary_record\n"
        "assert m.version('quarry-runtime-python') == '0.1.0'\n"
        "assert rt.QUARRY_GENERATED_CODE_API_VERSION_PYTHON == 1\n"
        "assert binary_record.EncodeError and binary_record.DecodeError\n"
        "assert binary_record.pack_scalar('uint32', 7) == b'\\x00\\x00\\x00\\x07'\n"
    )
    run([str(python), "-c", script], env=clean_env())


def install_from_sdist(python: Path, sdist: Path, root: Path) -> None:
    wheel_dir = root / "wheel-from-sdist"
    wheel_dir.mkdir()
    extracted = root / "sdist-source"
    extracted.mkdir()
    with tarfile.open(sdist) as archive:
        archive.extractall(extracted)
    source_dir = next(extracted.iterdir())
    run([sys.executable, "-m", "build", "--no-isolation", "--wheel",
         "--outdir", str(wheel_dir), str(source_dir)], env=clean_env())
    wheel = next(wheel_dir.glob("quarry_runtime_python-*.whl"))
    install_wheel(python, wheel)


def generated_consumer(python: Path, compiler: Path, install_prefix: Path, root: Path) -> None:
    dependency = root / "shared.brd"
    schema = root / "downstream.brd"
    dependency.write_text(
        "namespace: shared\n"
        "record: Child\n"
        "version: 1\n"
        "type: data\n"
        "fields:\n"
        "  value:\n    type: uint32\n"
        "enums:\n  Status:\n    values:\n      OK: 0\n      READY: 1\n",
        encoding="utf-8",
    )
    schema.write_text(
        "namespace: downstream\n"
        "record: Sample\n"
        "version: 1\n"
        "type: data\n"
        "imports:\n  - shared.brd\n"
        "fields:\n"
        "  count:\n    type: uint32\n"
        "  status:\n    type: shared.Status\n"
        "  child:\n    type: shared.Child\n"
        "  children:\n    type: shared.Child[]\n    max_elements: 2\n"
        "  label:\n    type: string\n    max_bytes: 16\n"
        "  blob:\n    type: bytes\n    max_bytes: 16\n",
        encoding="utf-8",
    )
    generated = root / "generated"
    for source in (dependency, schema):
        run([str(compiler), "--language", "python", "-o", str(generated), str(source)],
            env=clean_env())
    script = root / "consumer.py"
    script.write_text(
        "from shared.schema import Child, Status\n"
        "from downstream.schema import Sample\n"
        "child = Child(value=42)\n"
        "value = Sample(count=7, status=Status.READY, child=child, children=[child], "
        "label='café', blob=b'\\x00\\xff')\n"
        "encoded = value.encode()\n"
        "assert Sample.decode(encoded) == value\n"
        "assert value.encoded_size() == len(encoded)\n"
        "assert Sample().children is None\n",
        encoding="utf-8",
    )
    environment = clean_env()
    environment["PYTHONPATH"] = str(generated)
    environment["QUARRY_INSTALLED_PREFIX"] = str(install_prefix)
    run([str(python), str(script)], env=environment)


def epoch_mismatch(python: Path, wheel: Path, generated: Path, root: Path) -> None:
    mismatch = root / "mismatch-runtime"
    installed_runtime = next(python.parent.parent.glob("lib/python*/site-packages/quarry/runtime/python"), None)
    if installed_runtime is None:
        installed_runtime = next(python.parent.parent.glob("Lib/site-packages/quarry/runtime/python"))
    installed_quarry = installed_runtime.parents[1]
    target = mismatch / "quarry/runtime/python"
    target.mkdir(parents=True)
    for package_init in (installed_quarry / "__init__.py", installed_quarry / "runtime" / "__init__.py"):
        relative = package_init.relative_to(installed_quarry)
        destination = mismatch / "quarry" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(package_init, destination)
    for source in installed_runtime.iterdir():
        if source.is_file():
            shutil.copy2(source, target / source.name)
    init = target / "__init__.py"
    init.write_text(init.read_text(encoding="utf-8").replace("= 1", "= 999"), encoding="utf-8")
    script = root / "epoch.py"
    script.write_text("from downstream.schema import Sample\n", encoding="utf-8")
    environment = clean_env()
    environment["PYTHONPATH"] = os.pathsep.join((str(generated), str(mismatch)))
    result = subprocess.run([str(python), str(script)], text=True, env=environment,
                            capture_output=True)
    assert result.returncode != 0
    message = result.stderr + result.stdout
    assert "expects runtime epoch" in message, message
    assert "Regenerate" in message, message
    assert "1" in message and "999" in message, message


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime-source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="quarry-python-package-") as temporary:
        root = Path(temporary)
        dist = root / "dist"
        run([sys.executable, "-m", "build", "--no-isolation", "--wheel", "--sdist", "--outdir", str(dist),
             str(args.runtime_source)], env=clean_env())
        wheel, sdist = assert_artifacts(dist)
        install_prefix = root / "quarry-install"
        run([str(args.cmake), "--install", str(args.build_dir), "--prefix", str(install_prefix)],
            env=clean_env())
        first_python = make_venv(root / "wheel-env")
        install_and_check_runtime(first_python, wheel)
        generated_consumer(first_python, install_prefix / "bin/quarry-schema-compiler",
                           install_prefix, root)
        # The consumer's generated files are retained under root/generated for the
        # controlled epoch-mismatch import test.
        epoch_mismatch(first_python, wheel, root / "generated", root)
        second_python = make_venv(root / "sdist-env")
        install_from_sdist(second_python, sdist, root)
        run([str(second_python), "-c", "import quarry.runtime.python"], env=clean_env())


if __name__ == "__main__":
    main()
