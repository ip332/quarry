#!/bin/sh

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

cmake --preset debug
cmake --build --preset debug --target quarry_schema_compiler --parallel
exec "$project_root/build/debug/tools/quarry-schema-compiler" --version
