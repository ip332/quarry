#!/usr/bin/env python3
"""Exercise the actual --describe CLI, wire identity and read-only behavior."""
import argparse
import ast
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'compiler', 'generated-include'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    source = Path(args.source).resolve()
    generated = Path(args.generated_include)
    fixtures = source / 'tests/fixtures/description'
    with tempfile.TemporaryDirectory(prefix='quarry-description-') as temporary:
        root = Path(temporary)
        shutil.copytree(source / 'examples/truck/schemas', root / 'examples/truck/schemas')
        shutil.copytree(fixtures, root / 'small')

        def call(*flags, status=0, locale=None):
            environment = os.environ.copy()
            if locale:
                environment['LC_ALL'] = locale
            result = subprocess.run([args.compiler, *map(str, flags)], cwd=root,
                                    text=True, capture_output=True, env=environment, check=False)
            assert result.returncode == status, (flags, result.returncode, result.stderr)
            if status == 0:
                assert not result.stderr, result.stderr
            else:
                assert not result.stdout, result.stdout
                assert re.search(r'error(?::| [A-Z]+[0-9]+:)', result.stderr), result.stderr
            return result.stdout

        schema = 'examples/truck/schemas/configuration.brd'
        version = call('--version').strip()
        expected = (fixtures / 'truck.txt').read_text().replace('@COMPILER@', version)
        before = set(root.rglob('*'))
        assert call('--describe', schema) == expected
        for _ in range(3):
            assert call('--describe', schema) == expected
        assert call('--describe', schema, locale='C') == expected
        assert call('--describe', schema, locale='tr_TR.UTF-8') == expected
        for selection in ('truck.engine', 'truck.common,truck.engine', 'unknown.namespace'):
            assert call('--describe', '--generate', selection, schema) == expected
        for language in ('cpp', 'c', 'python', 'go'):
            assert call('--describe', '--language', language, '-o', root / 'not-created',
                        '--root-file-stem', 'renamed', '--file-extension', '.custom', schema) == expected
        assert call('--describe', '--language', 'go', '--cpp-storage', 'c',
                    '--c-optimize-size=unknown', '--go-import-prefix', 'unused', schema) == expected
        absolute = call('--describe', root / schema)
        assert absolute == expected.replace('root: ' + schema, 'root: configuration.brd')
        assert str(root) not in absolute
        assert set(root.rglob('*')) == before, '--describe wrote files'

        epochs = re.search(r'generated-code epochs: cpp (\d+), c (\d+), python (\d+), go (\d+)', expected).groups()
        cpp_header = (generated / 'quarry/runtime/version.hpp').read_text()
        c_header = (generated / 'quarry/runtime_c/version.h').read_text()
        assert epochs[0] == re.search(r'kGeneratedCodeApiVersion\s*=\s*(\d+)U', cpp_header).group(1)
        assert epochs[0] == call('--print-generated-code-api-version').strip()
        assert epochs[1] == re.search(r'QUARRY_C_GENERATED_CODE_API_VERSION\s+(\d+)U', c_header).group(1)
        python_tree = ast.parse((source / 'runtime/python/src/quarry/runtime/python/__init__.py').read_text())
        python_epoch = next(ast.literal_eval(node.value) for node in python_tree.body
                            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and
                            t.id == 'QUARRY_GENERATED_CODE_API_VERSION_PYTHON' for t in node.targets))
        assert epochs[2] == str(python_epoch)
        go = (source / 'runtime/go/brf/brf.go').read_text()
        assert epochs[3] == re.search(r'const GeneratedCodeAPIVersion = (\d+)', go).group(1)
        identity = call('--print-schema-id', schema).strip()
        assert 'schema_id: ' + identity + ' (algorithm 1: sha256-128)' in expected
        for reflective in (False, True):
            image = root / ('reflective.qbs' if reflective else 'minimal.qbs')
            call('--emit-qbs', image, *(['--qbs-reflective'] if reflective else []), schema)
            raw = image.read_bytes()
            assert raw[8] == 1 and raw[12:28].hex() == identity

        # Import order, declaration/file order, top-level YAML key order and
        # unused enum declaration order do not change the reported mappings.
        small = root / 'small'
        baseline = call('--describe', 'small/configuration.brd')
        assert 'domains: 1\n  0x0001 m\nrecords: 2\n' in baseline
        assert '0x00010001 m.Alpha\n  0x00010002 m.Zeta\n' in baseline
        alpha, zeta = (small / 'alpha.brd').read_text(), (small / 'zeta.brd').read_text()
        (small / 'alpha.brd').write_text(zeta.replace('namespace: m\nrecord: Zeta', 'record: Zeta\nnamespace: m'))
        (small / 'zeta.brd').write_text(alpha)
        config = small / 'configuration.brd'
        config.write_text(config.read_text().replace('  - zeta.brd\n  - alpha.brd', '  - alpha.brd\n  - zeta.brd'))
        assert call('--describe', 'small/configuration.brd') == baseline
        config.write_text(config.read_text().replace('ordering', 'renamed'))
        assert call('--describe', 'small/configuration.brd') == baseline.replace('configuration: ordering', 'configuration: renamed')
        body = (small / 'alpha.brd').read_text()
        first = '  UnusedA:\n    values:\n      first: 1\n'
        second = '  UnusedB:\n    values:\n      second: 2\n'
        (small / 'alpha.brd').write_text(body.replace(first + second, second + first))
        assert call('--describe', 'small/configuration.brd') == baseline.replace('configuration: ordering', 'configuration: renamed')
        standalone = call('--describe', 'small/zeta.brd')
        assert 'configuration:' not in standalone
        assert 'records: 1\n  0x00010001 m.Alpha\n' in standalone
        assert call('--print-schema-id', 'small/zeta.brd').strip() in standalone
        if os.name != 'nt':
            unusual = small / 'line\nbreak.brd'
            unusual.write_text(alpha)
            assert 'root: line\\x0abreak.brd\n' in call('--describe', unusual)

        for conflicting in (['--list-outputs'], ['--list-inputs'], ['--print-schema-id'],
                            ['--print-generated-code-api-version'], ['--version'],
                            ['--emit-qbs', 'forbidden.qbs'], ['--qbs-reflective'], ['--describe']):
            call('--describe', *conflicting, schema, status=2)
        assert not (root / 'forbidden.qbs').exists()
        call('--describe', status=2)
        call('--describe', schema, schema, status=2)
        call('--describe', '--generate', '', schema, status=2)
        call('--describe', '--language', 'invalid', schema, status=2)
        call('--describe', 'does-not-exist.brd', status=1)
        (root / 'invalid.brd').write_text('unknown: invalid\n')
        call('--describe', 'invalid.brd', status=1)
        assert '--describe' in call('--help', '--describe')
    print('Description golden, identity/epochs, ordering, options, errors and read-only invariants passed.')


if __name__ == '__main__':
    main()
