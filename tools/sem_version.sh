#!/bin/sh

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_log=$(mktemp "${TMPDIR:-/tmp}/quarry-sem-version.XXXXXX")
trap 'rm -f "$build_log"' EXIT

if ! cmake --preset debug >"$build_log" 2>&1; then
    cat "$build_log" >&2
    exit 1
fi
if ! cmake --build --preset debug --target quarry_schema_compiler --parallel >>"$build_log" 2>&1; then
    cat "$build_log" >&2
    exit 1
fi
exec "$project_root/build/debug/tools/quarry-schema-compiler" --version
