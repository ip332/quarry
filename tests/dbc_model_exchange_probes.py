#!/usr/bin/env python3
"""Reproduce exchange/version/packaging probes without changing the source tree.
Usage: exchange_probes.py BUILD_DIRECTORY [INSTALL_PREFIX]
"""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/dbc_model_exchange"
BUILD = Path(sys.argv[1]).resolve()
COMPILER = BUILD / "tools/quarry-schema-compiler"
CONSUMER = BUILD / "tests/dbc_model_exchange_consumer"


def run(command, expected=0, **kwargs):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, **kwargs)
    if result.returncode != expected:
        raise RuntimeError(f"{command}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result


def compile_model(folder, output):
    run([COMPILER, "--qbs-reflective", "--emit-qbs", output,
         "--output-directory", output.parent / "generated", folder / "configuration.brd"])
    return run([COMPILER, "--print-schema-id", folder / "configuration.brd"]).stdout.strip()


with tempfile.TemporaryDirectory(prefix="quarry-dbc-exchange-") as temporary:
    work = Path(temporary)
    source = work / "source"
    shutil.copytree(FIXTURES, source)
    qbs = work / "base.qbs"
    baseline = compile_model(source, qbs)
    assert baseline == (FIXTURES / "expected-schema-id.txt").read_text().strip()
    assert hashlib.sha256((source / "source.dbc").read_bytes()).hexdigest() in (source / "model.qtf").read_text()
    first = run([CONSUMER, qbs, source / "model.qtf", source / "expected-schema-id.txt"])
    print(first.stdout, end="")
    roundtrip = run([CONSUMER, qbs, source / "model.qtf", source / "expected-schema-id.txt", "--require-roundtrip"])
    assert "PASS: BRF/canonical-QTF round trip" in roundtrip.stdout
    print("PASS: formerly failing nested BRF round trip")

    relocated = work / "relocated"
    shutil.copytree(source, relocated)
    assert compile_model(relocated, work / "relocated.qbs") == baseline
    assert (work / "relocated.qbs").read_bytes() == qbs.read_bytes()
    print("PASS: QBS bytes deterministic across source relocation")

    model = relocated / "model.brd"
    original = model.read_text()
    model.write_text(original.replace("version: 1", "version: 2"))
    assert compile_model(relocated, work / "version.qbs") == baseline
    assert (work / "version.qbs").read_bytes() == qbs.read_bytes()
    print("CONFIRMED: BRD version alone does not change QBS identity/image")
    model.write_text(original.replace("exporter_version:", "tool_version:"))
    assert compile_model(relocated, work / "renamed.qbs") == baseline
    assert (work / "renamed.qbs").read_bytes() != qbs.read_bytes()
    renamed = run([CONSUMER, work / "renamed.qbs", source / "model.qtf", source / "expected-schema-id.txt"], expected=1)
    assert "unknown QTF field" in renamed.stderr
    print("CONFIRMED: reflective field rename keeps schema_id but rejects old named QTF")
    model.write_text(original)
    enum = relocated / "enum_value.brd"
    enum.write_text(enum.read_text().replace("type: u64", "type: u32"))
    assert compile_model(relocated, work / "changed.qbs") != baseline
    changed = run([CONSUMER, work / "changed.qbs", source / "model.qtf", source / "expected-schema-id.txt"], expected=1)
    assert "QBS identity mismatch" in changed.stderr
    print("PASS: incompatible type changes identity; pinned consumer rejects it")

    native_enum = work / "native_enum.brd"
    native_enum.write_text('namespace: dbc.probe\nrecord: Probe\nversion: 1\ntype: data\nenums:\n  Raw:\n    values:\n      Max: 18446744073709551615\nfields:\n  raw:\n    type: Raw\n')
    native = run([COMPILER, "--print-schema-id", native_enum], expected=1)
    assert "integer" in native.stderr.lower()
    print("CONFIRMED GAP: BRD native enum literal UINT64_MAX is rejected; EnumValue.raw:u64 works")

    if len(sys.argv) > 2:
        prefix = Path(sys.argv[2]).resolve()
        probe = work / "installed.cpp"
        probe.write_text('#include <quarry/runtime/qtf_parser.hpp>\n')
        installed = run(["c++", "-std=c++20", "-I", prefix / "include", "-fsyntax-only", probe], expected=1)
        assert "compiler/diagnostics/diagnostic.hpp" in installed.stderr
        assert not list((prefix / "lib").glob("*quarry_compiler_qbs*"))
        print("CONFIRMED GAP: installed QTF header requires a non-installed compiler header/library")
