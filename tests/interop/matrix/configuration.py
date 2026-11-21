"""CLI determinism and intentional dependency-mixing controls for A5."""
import os
import shutil
import sys

from model import RECORDS, SCHEMA_ID


def check(args, source, work, env, run, cli, variants, commands, report):
    truck = source / 'examples/truck/schemas/configuration.brd'
    copied = work / 'reordered'
    shutil.copytree(truck.parent, copied)
    root = copied / 'configuration.brd'
    text = root.read_text()
    lines = text.splitlines()
    imports = [line for line in lines if line.startswith('  - ')]
    root.write_text('\n'.join([line for line in lines if not line.startswith('  - ')] + list(reversed(imports))) + '\n')
    assert cli('--print-schema-id', root) == SCHEMA_ID
    description = cli('--describe', root)
    for name, identity, _ in RECORDS:
        if name.startswith('truck.'):
            assert f'0x{identity:08x} {name}' in description
    for reflective in (False, True):
        flags = ['--qbs-reflective'] if reflective else []
        plain = work / ('reflective.qbs' if reflective else 'minimal.qbs')
        for label, schema, extra in (
                ('reordered', root, []), ('prefixed', truck, ['--name-prefix', 'rev_a']),
                ('selected', truck, ['--generate', 'truck.engine,truck.common'])):
            output = work / f'{label}-{reflective}.qbs'
            cli('--emit-qbs', output, *flags, *extra, schema)
            assert output.read_bytes() == plain.read_bytes(), (label, reflective)
            assert cli('--print-schema-id', *extra, schema) == SCHEMA_ID
    report['checks'].append('repeat generation/output directory and selected/full source bytes; reordered imports/prefix/selection preserve IDs, full schema_id, both QBS byte streams')

    revision_b = source / 'examples/truck/multi_configuration/rev_b/configuration.brd'
    for variant, flags in variants.items():
        if variant not in commands:
            continue
        directory = work / variant
        other = work / (variant + '-incompatible')
        cli(*flags, '-o', other, revision_b)
        # Swapping only a dependency exercises the generated guard, not an
        # accidental missing include or unrelated application type error.
        if variant in ('c', 'cpp', 'cpp-c'):
            extension = '.generated.h' if variant == 'c' else '.generated.hpp'
            dependency = directory / ('truck/common' + extension)
            wrong = other / ('truck/common' + extension)
        else:
            relative = 'truck/common/schema.py' if variant == 'python' else 'truck/common/schema.quarry.go'
            dependency, wrong = directory / relative, other / relative
        original = dependency.read_bytes()
        dependency.write_bytes(wrong.read_bytes())
        try:
            if variant in ('c', 'cpp', 'cpp-c'):
                cpp = variant != 'c'
                path = work / ('mix-' + variant + ('.cpp' if cpp else '.c'))
                path.write_text(f'#include "truck/engine{extension}"\nint main(void) {{ return 0; }}\n')
                result = run([args.cxx if cpp else args.cc,
                    '-std=c++20' if cpp else '-std=c99', '-fsyntax-only',
                    '-I' + str(directory), '-I' + args.runtime_include, path], ok=False)
                assert result.returncode == 1 and 'different Quarry configuration' in result.stderr, result
            elif variant == 'python':
                result = run([sys.executable, '-c', 'import truck.engine.schema'],
                    environment=dict(env, PYTHONPATH=str(directory) + os.pathsep + env['PYTHONPATH']), ok=False)
                assert result.returncode == 1 and 'different Quarry configuration' in result.stderr, result
            else:
                result = run(['go', 'build', './truck/engine'], cwd=directory, ok=False)
                assert result.returncode == 1 and 'QuarrySchemaID' in result.stderr and 'overflows' in result.stderr, result
            report['checks'].append(f'{variant}: incompatible configuration dependency visibly rejected by generated guard')
        finally:
            dependency.write_bytes(original)
        # A wire-identical source-name edit changes gid, not schema_id. Only
        # native bindings have per-namespace gid checks; Python/Go deliberately
        # guard configuration identity instead. Do not invent a new gid format.
        if variant not in ('c', 'cpp', 'cpp-c'):
            report['checks'].append(f'{variant}: same-schema_id/different-gid guard N/A (no per-namespace gid API)')
            continue
        renamed = work / ('renamed-' + variant)
        shutil.copytree(truck.parent, renamed)
        timestamp = renamed / 'common_time.brd'
        timestamp.write_text(timestamp.read_text().replace('unix_ms:', 'utc_ms:'))
        assert cli('--print-schema-id', renamed / 'configuration.brd') == SCHEMA_ID
        changed = work / (variant + '-changed-gid')
        cli(*flags, '-o', changed, renamed / 'configuration.brd')
        dependency.write_bytes((changed / ('truck/common' + extension)).read_bytes())
        try:
            result = run([args.cxx if cpp else args.cc,
                '-std=c++20' if cpp else '-std=c99', '-fsyntax-only',
                '-I' + str(directory), '-I' + args.runtime_include, path], ok=False)
            assert result.returncode == 1 and ('generated definitions' in result.stderr or 'generated-code identity' in result.stderr), result
            report['checks'].append(f'{variant}: same schema_id, different generated definitions rejected separately by gid guard')
        finally:
            dependency.write_bytes(original)
