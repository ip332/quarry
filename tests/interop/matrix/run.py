#!/usr/bin/env python3
"""Black-box five-language BRF matrix. All assertions use public CLI/runtime APIs."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import tempfile

import adapters
import configuration
from model import RECORDS, BY_NAME, SCHEMA_ID, values

VARIANTS = {'c': ['--language', 'c'], 'cpp': ['--language', 'cpp'],
            'cpp-c': ['--language', 'cpp', '--cpp-storage', 'c'],
            'python': ['--language', 'python'],
            'go': ['--language', 'go', '--go-import-prefix', 'example.com/quarry/matrix']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('source', 'build-dir', 'cc', 'cxx', 'runtime-c', 'work', 'generic-cpp'):
        parser.add_argument('--' + key, required=True)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--coverage', default='OFF')
    parser.add_argument('--require-all', action='store_true')
    args = parser.parse_args()
    for key in ('generic_cpp', 'build_dir', 'runtime_c'):
        setattr(args, key, str(Path(getattr(args, key)).resolve()))
    start = time.monotonic()
    source, work = Path(args.source).resolve(), Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    output_root = work
    work = Path(tempfile.mkdtemp(prefix='run-', dir=output_root))
    truck = source / 'examples/truck/schemas/configuration.brd'
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
               PYTHONPATH=str(source / 'runtime/python/src'),
               GOTOOLCHAIN='local', GOPROXY='off', GOSUMDB='off', GOWORK='off',
               GOFLAGS='-buildvcs=false', GOCACHE=str(output_root / 'gocache'), GOPATH=str(output_root / 'gopath'))
    report = {'toolchains': {}, 'cells': [], 'checks': [], 'mutations': []}
    original_hook = sys.excepthook

    def failure_hook(kind, error, traceback):
        report['failure'] = str(error)
        report['elapsed_seconds'] = round(time.monotonic() - start, 3)
        (output_root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        original_hook(kind, error, traceback)

    sys.excepthook = failure_hook

    def run(command, *, cwd=work, environment=env, ok=True):
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=environment,
                                text=True, capture_output=True, check=False)
        with (work / 'commands.log').open('a') as log:
            log.write(json.dumps(list(map(str, command))) + '\n' + result.stdout + result.stderr + '\n')
        if ok and result.returncode:
            raise AssertionError((command, result.returncode, result.stdout, result.stderr))
        return result

    # Exercise the actual distribution boundary, including generated epoch
    # headers. The caller's C library may be Clang-built for the extended run;
    # it consumes these same installed public headers.
    installed = work / 'installed'
    run([args.cmake, '--install', args.build_dir, '--prefix', installed])
    args.compiler = str(installed / 'bin/quarry-schema-compiler')
    args.inspector = str(installed / 'bin/quarry-brf-inspect')
    args.runtime_include = str(installed / 'include')
    report['checks'].append('CLI tools and native public headers consumed from an actual CMake install')

    def cli(*flags):
        return run([args.compiler, *flags]).stdout.strip()

    def tree(path):
        return {p.relative_to(path).as_posix(): p.read_bytes() for p in path.rglob('*')
                if p.is_file() and '__pycache__' not in str(p)}

    def compile_native(variant, directory, output, text=None):
        cpp = variant != 'c'
        path = output.with_suffix('.cpp' if cpp else '.c')
        path.write_text(adapters.native(variant) if text is None else text)
        flags = [args.cxx if cpp else args.cc, '-std=c++20' if cpp else '-std=c99',
                 '-Wall', '-Wextra', '-Wpedantic', '-Wshadow', '-Werror',
                 '-I' + str(directory), '-I' + args.runtime_include]
        if args.coverage.lower() in ('on', '1', 'true'):
            flags.append('--coverage')
        run([*flags, path, args.runtime_c, '-o', output])

    commands = {}
    for variant, flags in VARIANTS.items():
        directory = work / variant
        cli(*flags, '-o', directory, truck)
        repeated = work / (variant + '-repeat')
        cli(*flags, '-o', repeated, truck)
        assert tree(directory) == tree(repeated), ('repeat/output directory', variant)
        selected = work / (variant + '-selected')
        cli(*flags, '--generate', 'truck.engine,truck.common', '-o', selected, truck)
        assert all(tree(directory)[p] == data for p, data in tree(selected).items()), variant
        cli(*flags, '-o', directory, source / 'tests/fixtures/interop_matrix/enum.brd')
        if variant in ('c', 'cpp', 'cpp-c'):
            executable = work / (variant + '-adapter')
            compile_native(variant, directory, executable)
            commands[variant] = [executable]
            report['toolchains'][variant] = run([args.cc if variant == 'c' else args.cxx, '--version']).stdout.splitlines()[0]
        elif variant == 'python':
            path = work / 'python-adapter.py'
            path.write_text(adapters.python())
            commands[variant] = [sys.executable, path]
            report['toolchains'][variant] = sys.version
        elif shutil.which('go'):
            (directory / 'go.mod').write_text('module example.com/quarry/matrix\n\ngo 1.22\n\n'
                'require github.com/ip332/quarry/runtime/go v0.0.0\n'
                f'replace github.com/ip332/quarry/runtime/go => {source / "runtime/go"}\n')
            path = directory / 'cmd/adapter/main.go'
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(adapters.go())
            executable = work / 'go-adapter'
            run(['go', 'build', '-o', executable, './cmd/adapter'], cwd=directory)
            commands[variant] = [executable]
            report['toolchains'][variant] = run(['go', 'version']).stdout.strip()
        else:
            report['toolchains'][variant] = 'SKIP: Go toolchain unavailable'
            print('SKIP Go: Go toolchain unavailable', flush=True)
    report['runtime_availability'] = {variant: variant in commands for variant in VARIANTS}
    if args.require_all:
        assert len(commands) == 5, report['toolchains']

    def adapter(variant, mode, name, path, seed=0, ok=True):
        environment = dict(env)
        environment['PYTHONPATH'] = str(work / 'python') + os.pathsep + env['PYTHONPATH']
        return run([*commands[variant], mode, name, path, seed], environment=environment, ok=ok)

    def reject(variant, name, path, seed=0):
        result = adapter(variant, 'decode', name, path, seed, ok=False)
        expected_exit = 2 if variant == 'go' else 1
        assert result.returncode == expected_exit, (variant, 'unexpected rejection exit', result)
        for fault in ('runtime error:', 'IndexError:', 'MemoryError:', 'AddressSanitizer',
                      'UndefinedBehaviorSanitizer', 'Segmentation fault'):
            assert fault not in result.stderr, (variant, 'runtime fault instead of rejection', result.stderr)
        return result

    def batch(variant, requests, label, *, command=None, ok=True):
        path = work / (label + '.requests')
        path.write_text(''.join('\n'.join(map(str, request)) + '\n' for request in requests))
        environment = dict(env, PYTHONPATH=str(work / 'python') + os.pathsep + env['PYTHONPATH'])
        return run([*(commands[variant] if command is None else command), '--batch', path],
                   environment=environment, ok=ok)

    assert cli('--print-schema-id', truck) == SCHEMA_ID
    assert SCHEMA_ID in cli('--describe', truck)
    def verify_identity(variant, observed):
        assert observed == SCHEMA_ID, (variant, 'schema_id mismatch', observed, SCHEMA_ID)

    for variant in commands:
        observed = adapter(variant, 'identity', '-', '-').stdout.strip()
        verify_identity(variant, observed)
        try:
            verify_identity(variant, observed[:-1] + ('0' if observed[-1] != '0' else '1'))
        except AssertionError as error:
            assert 'schema_id mismatch' in str(error)
        else:
            raise AssertionError('identity mutation escaped the matrix assertion')
    report['mutations'].append({'mutation': 'incompatible observed configuration identity',
        'result': f'DETECTED by the same full-128-bit metadata comparison for {len(commands)} available bindings'})
    sys.path.insert(0, str(source / 'runtime/python/src'))
    from quarry.runtime.generic import QbsSchema, validate_brf, ValidatedRecord, RecordArrayView, BrfError
    schemas = []
    enum_schemas = []
    for reflective in (False, True):
        qbs = work / ('reflective.qbs' if reflective else 'minimal.qbs')
        options = ['--qbs-reflective'] if reflective else []
        cli('--emit-qbs', qbs, *options, truck)
        schema = QbsSchema.from_bytes(qbs.read_bytes())
        assert schema.schema_id.hex() == SCHEMA_ID
        schemas.append((qbs, schema))
        eqbs = work / ('enum-reflective.qbs' if reflective else 'enum-minimal.qbs')
        cli('--emit-qbs', eqbs, *options, source / 'tests/fixtures/interop_matrix/enum.brd')
        enum_schemas.append((eqbs, QbsSchema.from_bytes(eqbs.read_bytes())))
    configuration.check(args, source, work, env, run, cli, VARIANTS, commands, report)
    report['checks'].append(f'128-bit schema_id: {len(commands)} available generated bindings, CLI, describe, minimal/reflective QBS')

    # Generic numeric field access works identically with minimal/reflective QBS.
    def generic(name, view, seed):
        assert view.record_schema.record_id == BY_NAME[name][0]
        if seed < 0:
            assert all(view.field(i) is None for i in range(len(BY_NAME[name][1])))
            return
        for index, (field, kind, _) in enumerate(BY_NAME[name][1]):
            actual, expected = view.field(index), values(name, seed)[field]
            if kind.endswith('[]'):
                assert isinstance(actual, RecordArrayView) and len(actual) == 2
                for i, child in enumerate(actual):
                    generic(kind[:-2], child, seed + i)
            elif kind in BY_NAME:
                assert isinstance(actual, ValidatedRecord)
                generic(kind, actual, seed)
            else:
                assert actual == expected, (name, field, actual, expected)

    generic_path = work / 'generic.c'
    generic_path.write_text(adapters.generic_c())
    generic_exe = work / 'generic-c'
    generic_flags = ['--coverage'] if args.coverage.lower() in ('on', '1', 'true') else []
    run([args.cc, '-std=c99', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
         '-I' + args.runtime_include,
         *generic_flags, generic_path, args.runtime_c, '-o', generic_exe])
    records = []

    def images(name):
        return enum_schemas if name.startswith('matrix.') else schemas

    for producer in commands:
        batch(producer, [('encode', name, work / f'{producer}-{name}{suffix}.brf', seed)
                         for name, _, _ in RECORDS
                         for suffix, seed in (('', 0), ('-empty', -1))], producer + '-produce')

    for producer in commands:
        for name, expected_id, _ in RECORDS:
            path = work / f'{producer}-{name}.brf'
            data = path.read_bytes()
            assert int.from_bytes(data[4:8], 'big') == expected_id, (producer, name)
            records.append((producer, name, 0, path))
            for qbs, schema in images(name):
                view = validate_brf(schema, schema.record_by_id(expected_id), data)
                generic(name, view, 0)
                if name == 'truck.engine.EngineStatus':
                    run([args.generic_cpp, qbs, path])
                # All producer streams must match byte for byte below. Inspect
                # one representative producer, rather than launch the same CLI
                # repeatedly on identical bytes. Keep every five-way decode.
                if producer == 'c':
                    inspection = run([args.inspector, '--qbs', qbs, '--brf', path, '--record-id', expected_id]).stdout
                    if name == 'truck.engine.EngineStatus':
                        assert '1450' in inspection and '-12' in inspection and '1234' in inspection and '1235' in inspection, inspection
            # C generic views expose the actual nested backing span. Python's
            # existing ValidatedRecord.source intentionally retains the root;
            # do not infer child byte ranges from it or add a second parser.
            for qbs, _ in images(name):
                if producer != 'c' and qbs != images(name)[0][0]:
                    continue
                prefix = str(work / (producer + '-' + name + '-cut')) if qbs == images(name)[0][0] else '-'
                result = run([generic_exe, name, expected_id, qbs, path, 0, prefix])
                for line in result.stdout.splitlines():
                    child_name, child_seed, child_path = line.split(' ', 2)
                    child_seed, cut = int(child_seed), Path(child_path)
                    records.append((producer, child_name, child_seed, cut))
    # Batch independent encodings for distinct array-element values. Seed zero
    # already has an independent top-level producer file for each type.
    for producer in commands:
        requests = sorted({(name, seed) for p, name, seed, _ in records
                           if p == producer and seed != 0})
        batch(producer, [('encode', name, work / f'{producer}-{name}-{seed}.standalone', seed)
                         for name, seed in requests], producer + '-standalone')
        for p, name, seed, path in records:
            if p != producer:
                continue
            standalone = work / (f'{producer}-{name}-{seed}.standalone' if seed else f'{producer}-{name}.brf')
            assert path.read_bytes() == standalone.read_bytes(), path
    # Absent/default records distinguish absence from populated zero values and
    # cover variable record/array/string empty representations in every cell.
    for producer in commands:
        for name, expected_id, _ in RECORDS:
            path = work / f'{producer}-{name}-empty.brf'
            records.append((producer, name, -1, path))
            for qbs, schema in images(name):
                view = validate_brf(schema, schema.record_by_id(expected_id), path.read_bytes())
                generic(name, view, -1)
                if producer == 'c':
                    run([generic_exe, name, expected_id, qbs, path, -1, '-'])
                    run([args.inspector, '--qbs', qbs, '--brf', path, '--record-id', expected_id])
    # All producers must agree before routing every stream through every consumer.
    for name, _, _ in RECORDS:
        for suffix in ('', '-empty'):
            assert len({(work / f'{p}-{name}{suffix}.brf').read_bytes() for p in commands}) == 1, (name, suffix)
    print('Producer \\ Consumer | ' + ' | '.join(VARIANTS), flush=True)
    failed_cells = []
    for producer in VARIANTS:
        row = []
        for consumer in VARIANTS:
            if producer not in commands or consumer not in commands:
                row.append('SKIP')
                report['cells'].append(dict(producer=producer, consumer=consumer, result='SKIP',
                                            reason='Go toolchain unavailable'))
                continue
            selected_records = [(name, seed, path) for p, name, seed, path in records if p == producer]
            result = batch(consumer, [('decode', name, path, seed) for name, seed, path in selected_records],
                           f'{producer}-to-{consumer}', ok=False)
            if result.returncode:
                failed_cells.append((producer, consumer, result.stderr))
                row.append('FAIL')
                report['cells'].append(dict(producer=producer, consumer=consumer, result='FAIL',
                                            fixtures=[p.name for _, _, p in selected_records],
                                            diagnostic=result.stderr))
                continue
            for name, seed, path in selected_records:
                observed = int.from_bytes(path.read_bytes()[4:8], 'big')
                report['cells'].append(dict(producer=producer, consumer=consumer,
                    fixture=path.name, record=name, expected_id=BY_NAME[name][0], observed_id=observed,
                    bytes='PASS', decode='PASS', result='PASS'))
            row.append('PASS')
        print(producer + ' | ' + ' | '.join(row), flush=True)
    assert not failed_cells, failed_cells
    report['checks'].append(f'{len(records)} populated/default top-level and extracted nested streams; generic C/Python + C++ EngineStatus probe + inspector in both QBS modes')

    # Every populated/default producer fixture must retain its actual bytes
    # under naming isolation, not just its schema_id. The A4.5 application
    # separately exercises both revisions together in one process.
    for variant in commands:
        directory = work / variant
        cli(*VARIANTS[variant], '--name-prefix', 'rev_a', '-o', directory, truck)
        executable = work / (variant + '-prefixed-adapter')
        if variant in ('c', 'cpp', 'cpp-c'):
            compile_native(variant, directory, executable, adapters.prefixed(variant))
            command = [executable]
        elif variant == 'python':
            path = work / 'python-prefixed-adapter.py'
            path.write_text(adapters.prefixed(variant))
            command = [sys.executable, path]
        else:
            path = directory / 'cmd/prefixed/main.go'
            path.parent.mkdir(parents=True)
            path.write_text(adapters.prefixed(variant))
            run(['go', 'build', '-o', executable, './cmd/prefixed'], cwd=directory)
            command = [executable]
        requests = [('encode', name, work / f'{variant}-{name}-prefixed{suffix}.brf', seed)
                    for name, _, _ in RECORDS for suffix, seed in (('', 0), ('-empty', -1))]
        batch(variant, requests, variant + '-prefixed', command=command)
        environment = dict(env, PYTHONPATH=str(work / 'python') + os.pathsep + env['PYTHONPATH'])
        verify_identity(variant, run([*command, 'identity', '-', '-', 0], environment=environment).stdout.strip())
        for name, _, _ in RECORDS:
            for suffix in ('', '-empty'):
                assert (work / f'{variant}-{name}-prefixed{suffix}.brf').read_bytes() == (work / f'{variant}-{name}{suffix}.brf').read_bytes(), (variant, name, suffix)
    report['checks'].append('all available producers: prefixed populated/default BRF bytes and full schema_id match every unprefixed fixture')

    # Negative controls call the very same adapter entry points as ordinary cells.
    base = work / 'c-truck.engine.EngineStatus.brf'
    if not base.exists():
        raise AssertionError('C99 baseline required')
    for mutation in ('record-id', 'field-value', 'broken-pair'):
        damaged = work / (mutation + '.brf')
        data = bytearray(base.read_bytes())
        if mutation == 'record-id':
            data[4:8] = (1).to_bytes(4, 'big')
            damaged.write_bytes(data)
        elif mutation == 'field-value':
            adapter('c', 'encode', 'truck.engine.EngineStatus', damaged, 1)
        else:
            # Valid Timestamp delivered to the EngineStatus consumer cell.
            adapter('c', 'encode', 'truck.common.Timestamp', damaged)
        diagnostics = {}
        for consumer in commands:
            if mutation == 'field-value':
                adapter(consumer, 'decode', 'truck.engine.EngineStatus', damaged, 1)
            elif mutation == 'broken-pair':
                adapter(consumer, 'decode', 'truck.common.Timestamp', damaged)
            rejected = reject(consumer, 'truck.engine.EngineStatus', damaged)
            diagnostics[consumer] = rejected.stderr[-2000:]

        report['mutations'].append({'mutation': mutation, 'result': 'DETECTED by same matrix decode/value assertions', 'diagnostics': diagnostics})
    # Malformed payloads exercise errors beyond record-ID mismatches. These
    # mutations modify existing public producer bytes, not an alternate codec.
    camera = work / 'c-truck.cms.CameraHealth.brf'
    engine = base.read_bytes()
    invalid_utf8 = bytearray(camera.read_bytes())
    location = invalid_utf8.index(b'front0')
    invalid_utf8[location] = 0xff
    enum_wire = bytearray((work / 'c-matrix.EnumSample.brf').read_bytes())
    enum_wire[-2:] = (4).to_bytes(2, 'big')  # not a member of Mode
    # Find an actual independently extracted Timestamp span, then change only
    # its ID inside GearState; the parent's authoritative ID remains intact.
    gear = bytearray((work / 'c-truck.transmission.GearState.brf').read_bytes())
    timestamp = (work / 'c-truck.common.Timestamp.brf').read_bytes()
    child_offset = gear.index(timestamp)
    gear[child_offset+4:child_offset+8] = (1).to_bytes(4, 'big')
    errors = [('truncated', 'truck.engine.EngineStatus', engine[:-1]),
              ('trailing', 'truck.engine.EngineStatus', engine + b'\0'),
              ('invalid-utf8', 'truck.cms.CameraHealth', invalid_utf8),
              ('unknown-enum', 'matrix.EnumSample', enum_wire),
              ('nested-legacy-id', 'truck.transmission.GearState', gear)]
    for label, name, data in errors:
        path = work / (label + '.brf')
        path.write_bytes(data)
        for consumer in commands:
            reject(consumer, name, path)
        for qbs, schema in images(name):
            assert run([args.inspector, '--qbs', qbs, '--brf', path,
                        '--record-id', BY_NAME[name][0]], ok=False).returncode == 6, label
            try:
                validate_brf(schema, schema.record_by_id(BY_NAME[name][0]), data)
            except BrfError:
                pass
            else:
                raise AssertionError(('generic Python accepted malformed record', label))
            assert run([generic_exe, name, BY_NAME[name][0], qbs, path, 0, '-'], ok=False).returncode == 1, label
        report['checks'].append(f'{label}: all generated decoders, C/Python generic runtimes and both inspector modes reject')
    legacy_types = set()
    for producer, name, seed, path in records:
        # A representative top-level and every nested type: current expected
        # mapping rejects RC3 IDs; no claim about universal numeric validity.
        if producer != 'c' or seed != 0 or name in legacy_types:
            continue
        legacy_types.add(name)
        damaged = work / 'legacy.brf'
        data = bytearray(path.read_bytes())
        data[4:8] = (BY_NAME[name][0] & 0xffff).to_bytes(4, 'big')
        damaged.write_bytes(data)
        for consumer in commands:
            reject(consumer, name, damaged)
        for qbs, schema in images(name):
            assert run([args.inspector, '--qbs', qbs, '--brf', damaged,
                        '--record-id', BY_NAME[name][0]], ok=False).returncode == 6
    report['checks'].append('legacy RC3 mapping rejected by all available generated decoders and inspector; positive controls above')
    report['elapsed_seconds'] = round(time.monotonic() - start, 3)
    (output_root / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'cells'}, indent=2), flush=True)


if __name__ == '__main__':
    main()
