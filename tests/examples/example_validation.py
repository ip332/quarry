"""Validate the public examples through an installed Quarry prefix."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile
from typing import List


def run(command: List[str], *, cwd: Path, env: Optional[Dict[str, str]] = None) -> None:
    subprocess.run(command, cwd=cwd, env=env, check=True)


def configure_build_run(source: Path, prefix: Path, root: Path, executable: str) -> None:
    build = root / (source.parent.name + "-" + source.name + "-build")
    run(["cmake", "-S", str(source), "-B", str(build),
         f"-DCMAKE_PREFIX_PATH={prefix}"], cwd=root)
    run(["cmake", "--build", str(build), "--parallel"], cwd=root)
    run([str(build / executable)], cwd=root)


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

        interop = examples / "interop/cpp_python"
        interop_build = root / "cpp_python-build"
        run(["cmake", "-S", str(interop), "-B", str(interop_build),
             f"-DCMAKE_PREFIX_PATH={prefix}"], cwd=root)
        run(["cmake", "--build", str(interop_build), "--parallel"], cwd=root)
        encoded = root / "interop.brf"
        run([str(interop_build / "quarry_cpp_python_encode"), str(encoded)], cwd=root)
        run([str(interop_build / "quarry_cpp_python_encode"), "--decode", str(encoded)], cwd=root)


if __name__ == "__main__":
    main()
