#!/usr/bin/env python3

import pathlib
import re
import subprocess
import sys
import tempfile


def decode_hex_seed(path: pathlib.Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    tokens = re.findall(r"[0-9a-fA-F]{2}", text)
    return bytes(int(token, 16) for token in tokens)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_seed_corpus.py FUZZER CORPUS_DIR", file=sys.stderr)
        return 2

    fuzzer = pathlib.Path(sys.argv[1])
    corpus_dir = pathlib.Path(sys.argv[2])
    seeds = sorted(corpus_dir.glob("*.hex"))
    if not seeds:
        print(f"no .hex seeds found in {corpus_dir}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="breadcrumbs-brf-corpus-") as temp_dir:
        raw_dir = pathlib.Path(temp_dir)
        raw_paths = []
        for seed in seeds:
            raw_path = raw_dir / seed.with_suffix(".bin").name
            raw_path.write_bytes(decode_hex_seed(seed))
            raw_paths.append(raw_path)

        for raw_path in raw_paths:
            result = subprocess.run(
                [str(fuzzer), "-runs=1", "-detect_leaks=0", str(raw_path)],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
            )
            if result.returncode != 0:
                print(f"{fuzzer} failed on {raw_path.name}", file=sys.stderr)
                print(result.stderr, file=sys.stderr)
                return result.returncode

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
