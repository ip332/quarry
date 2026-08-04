"""Validate the public examples through an installed Quarry prefix."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import venv
from typing import Dict, List, Optional


def run(command: List[str], *, cwd: Path, env: Optional[Dict[str, str]] = None) -> None:
    subprocess.run(command, cwd=cwd, env=env, check=True)


def configure_build_run(source: Path, prefix: Path, root: Path, executable: str) -> None:
    build = root / (source.parent.name + "-" + source.name + "-build")
    run(["cmake", "-S", str(source), "-B", str(build),
         f"-DCMAKE_PREFIX_PATH={prefix}"], cwd=root)
    run(["cmake", "--build", str(build), "--parallel"], cwd=root)
    run([str(build / executable)], cwd=root)


def generate_python(compiler: Path, schema: Path, output: Path) -> None:
    run([str(compiler), "--language", "python", "--output-directory", str(output),
         str(schema)], cwd=schema.parent)


def python_executable(venv_dir: Path) -> Path:
    return venv_dir / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--python", required=True, type=Path)
    args = parser.parse_args()

    examples = args.source_dir / "examples"
    with tempfile.TemporaryDirectory(prefix="quarry-examples-") as temporary:
        root = Path(temporary)
        prefix = root / "install"
        run([str(args.cmake), "--install", str(args.build_dir), "--prefix", str(prefix)], cwd=root)

        configure_build_run(examples / "cpp/basic_encode_decode", prefix, root,
                            "quarry_basic_encode_decode")
        configure_build_run(examples / "cpp/schema_compiler_cmake", prefix, root,
                            "quarry_schema_compiler_cmake")
        configure_build_run(examples / "c/basic_encode_decode", prefix, root,
                            "quarry_basic_encode_decode_c")

        wheel_dir = root / "wheel"
        run([str(args.python), "-m", "build", "--no-isolation", "--wheel",
             "--outdir", str(wheel_dir), str(args.source_dir / "runtime/python")], cwd=root)
        wheel = next(wheel_dir.glob("quarry_runtime_python-*.whl"))
        venv_dir = root / "python-venv"
        venv.EnvBuilder(with_pip=True).create(venv_dir)
        python = python_executable(venv_dir)
        run([str(python), "-m", "pip", "install", "--no-index", "--no-deps", str(wheel)], cwd=root)

        compiler = prefix / "bin/quarry-schema-compiler"
        basic_python = examples / "python/basic_encode_decode"
        basic_output = root / "python-basic-generated"
        generate_python(compiler, basic_python / "schema.brd", basic_output)
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        environment["PYTHONPATH"] = str(basic_output)
        run([str(python), str(basic_python / "main.py")], cwd=basic_python, env=environment)

        interop = examples / "interop/cpp_python"
        interop_output = root / "interop-python-generated"
        generate_python(compiler, interop / "schema.brd", interop_output)
        interop_build = root / "cpp_python-build"
        run(["cmake", "-S", str(interop), "-B", str(interop_build),
             f"-DCMAKE_PREFIX_PATH={prefix}"], cwd=root)
        run(["cmake", "--build", str(interop_build), "--parallel"], cwd=root)
        encoded = root / "interop.brf"
        run([str(interop_build / "quarry_cpp_python_encode"), str(encoded)], cwd=root)
        environment["PYTHONPATH"] = str(interop_output)
        run([str(python), str(interop / "decode.py"), str(encoded)], cwd=interop, env=environment)


if __name__ == "__main__":
    main()
