#!/usr/bin/env python3
"""Black-box naming isolation, baseline bytes, identity and strict native consumers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


VARIANTS = {
    'cpp': ['--language', 'cpp'],
    'c': ['--language', 'c'],
    'c-compact': ['--language', 'c', '--c-optimize-size=truck.common'],
    'cpp-c': ['--language', 'cpp', '--cpp-storage', 'c'],
    'cpp-c-compact': ['--language', 'cpp', '--cpp-storage', 'c', '--c-optimize-size=truck.common'],
    'python': ['--language', 'python'],
    'go': ['--language', 'go', '--go-import-prefix', 'example.com/quarry/naming'],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'compiler', 'generated-include', 'cc', 'cxx', 'runtime-c'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument("--coverage", default="OFF")
    args = parser.parse_args()
    source = Path(args.source).resolve()
    compiler = str(Path(args.compiler).resolve())
    manifest = json.loads((source / 'tests/fixtures/name_prefix/unprefixed_sha256.json').read_text())
    truck = source / 'examples/truck/schemas/configuration.brd'
    with tempfile.TemporaryDirectory(prefix='quarry-name-prefix-') as temporary:
        work = Path(temporary)

        def call(*flags, status=0):
            result = subprocess.run([compiler, *map(str, flags)], cwd=work,
                                    text=True, capture_output=True, check=False)
            assert result.returncode == status, (flags, result.returncode, result.stderr)
            if status == 0:
                assert not result.stderr, result.stderr
            else:
                assert result.stderr and not result.stdout, result
            return result.stdout

        def files(directory):
            return {p.relative_to(directory).as_posix(): p.read_bytes()
                    for p in directory.rglob('*') if p.is_file()}

        def gid(header):
            return re.search(r'^#ifndef .*_([A-F0-9]{32})_$', header.read_text(), re.M).group(1)

        def compile_run(text, name, includes, *, cpp=False, succeeds=True):
            path = work / (name + ('.cpp' if cpp else '.c'))
            path.write_text(text)
            executable = work / name
            command = [args.cxx if cpp else args.cc, '-std=c++20' if cpp else '-std=c99',
                       '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I' + str(source / 'include'),
                       '-I' + args.generated_include]
            if args.coverage.lower() in ('on', 'true', '1', 'yes'):
                command.append('--coverage')
            command += ['-I' + str(p) for p in includes]
            command += [str(path), args.runtime_c, '-o', str(executable)]
            result = subprocess.run(command, text=True, capture_output=True, check=False)
            assert (result.returncode == 0) == succeeds, (name, result.stderr)
            if succeeds:
                subprocess.run([str(executable)], check=True)
            return result.stderr

        for variant, flags in VARIANTS.items():
            base, prefixed, selected = [work / (variant + '-' + label)
                                        for label in ('base', 'prefix', 'selection')]
            call(*flags, '-o', base, truck)
            actual = {name: hashlib.sha256(data).hexdigest() for name, data in files(base).items()}
            assert actual == manifest['variants'][variant], variant
            call(*flags, '--name-prefix', 'rev_a', '-o', prefixed, truck)
            planned = call(*flags, '--name-prefix', 'rev_a', '--list-outputs', '-o', prefixed, truck)
            assert set(planned.splitlines()) == {str(prefixed / p) for p in files(prefixed)}, variant
            call(*flags, '--name-prefix', 'rev_a', '--generate', 'truck.engine,truck.common',
                 '-o', selected, truck)
            for name, data in files(selected).items():
                assert data == files(prefixed)[name], (variant, name)
            assert not list(prefixed.glob('truck')), variant
            if variant == 'python':
                assert (prefixed / 'rev_a/__init__.py').is_file()
                assert 'import rev_a.truck.common.schema' in (prefixed / 'rev_a/truck/engine/schema.py').read_text()
            if variant == 'go':
                assert 'example.com/quarry/naming/rev_a/truck/common' in (prefixed / 'rev_a/truck/engine/schema.quarry.go').read_text()
            if variant in ('cpp', 'c', 'cpp-c'):
                extension = '.generated.h' if variant == 'c' else '.generated.hpp'
                assert gid(base / ('truck/engine' + extension)) != gid(prefixed / ('rev_a/truck/engine' + extension))
                if variant == 'cpp':
                    renamed = work / 'cpp-renamed'
                    call(*flags, '--name-prefix', 'rev_a', '--root-file-stem', 'renamed',
                         '--file-extension', '.other.hpp', '-o', renamed, truck)
                    assert gid(renamed / 'rev_a/truck/engine.other.hpp') == gid(prefixed / 'rev_a/truck/engine.generated.hpp')
                # A renamed dependency with the wrong gid must still fail even
                # after its source-level prefix is made to look correct.
                other = work / (variant + '-other')
                call(*flags, '--name-prefix', 'rev_b', '-o', other, truck)
                common = prefixed / ('rev_a/truck/common' + extension)
                original = common.read_bytes()
                other_text = (other / ('rev_b/truck/common' + extension)).read_text()
                common.write_text(other_text.replace('revq0b', 'revq0a').replace('rev_b', 'rev_a'))
                error = compile_run('#include "rev_a/truck/engine' + extension + '"\nint main(void) { return 0; }\n',
                                    variant + '-wrong-gid', [prefixed], cpp=variant != 'c', succeeds=False)
                assert 'generated definitions' in error or 'generated-code identity' in error, error
                common.write_bytes(original)
                # A dependency generated under another prefix is not silently
                # resolved through an unprefixed include path.
                missing = work / (variant + '-missing')
                call(*flags, '--name-prefix', 'rev_a', '--generate', 'truck.engine', '-o', missing, truck)
                compile_run('#include "rev_a/truck/engine' + extension + '"\nint main(void) { return 0; }\n',
                            variant + '-wrong-prefix', [missing, other], cpp=variant != 'c', succeeds=False)

        # Naming must not change actual BRF bytes, including packed dependency storage.
        for variant in ('cpp', 'c', 'c-compact', 'cpp-c', 'cpp-c-compact'):
            base, named = work / (variant + '-base'), work / (variant + '-prefix')
            extension = '.generated.h' if variant.startswith('c') and not variant.startswith('cpp') else '.generated.hpp'
            text = '#include "truck/engine' + extension + '"\n#include "rev_a/truck/engine' + extension + '"\n'
            if variant == 'cpp':
                text += r'''int main() {
  truck::engine::EngineStatusBuilder a;
  rev_a::truck::engine::EngineStatusBuilder b;
  if (!a.set_rpm(1450) || !b.set_rpm(1450)) return 1;
  auto wa = truck::engine::encode_result(a.build());
  auto wb = rev_a::truck::engine::encode_result(b.build());
  if (!wa || !wb || *wa.value != *wb.value) return 2;
  auto da = truck::engine::decode_EngineStatus_result(*wa.value);
  auto db = rev_a::truck::engine::decode_EngineStatus_result(*wb.value);
  return da && db && da.value->rpm() && db.value->rpm() && *da.value->rpm() == 1450 && *db.value->rpm() == 1450 ? 0 : 3;
}
'''
            else:
                text += '#include <string.h>\nint main(void) {\n unsigned char a[256], b[256];\n'
                for name, prefix, output in [('a', 'truck', 'a'), ('b', 'revq0a_truck', 'b')]:
                    stem = prefix + '_engine_EngineStatus'
                    timestamp = prefix + '_common_Timestamp'
                    text += f' {stem}_t v{name}; {stem}_init(&v{name});\n'
                    text += f' v{name}.has_rpm = true; v{name}.rpm = 1450; v{name}.has_time = true;\n'
                    text += f' {timestamp}_t t{name}; {timestamp}_init(&t{name}); t{name}.unix_ms = 42;\n'
                    if variant.endswith('compact'):
                        text += f' {timestamp}_set_unix_ms_present(&t{name}, true);\n'
                    else:
                        text += f' t{name}.has_unix_ms = true;\n'
                    text += f' v{name}.time = t{name};\n {stem}_encode_result_t e{name} = {stem}_encode(&v{name}, {output}, sizeof({output}));\n'
                    text += f' if (e{name}.status != QUARRY_C_STATUS_OK) return 1;\n'
                    text += f' {stem}_decode_result_t d{name} = {stem}_decode({output}, e{name}.bytes_written);\n'
                    text += f' if (d{name}.status != QUARRY_C_STATUS_OK || d{name}.value.rpm != 1450 || d{name}.value.time.unix_ms != 42) return 2;\n'
                text += ' return ea.bytes_written == eb.bytes_written && memcmp(a, b, ea.bytes_written) == 0 ? 0 : 3;\n}\n'
            compile_run(text, variant + '-wire', [base, named], cpp=variant.startswith('cpp'))

        identity = call('--print-schema-id', truck)
        description = call('--describe', truck)
        for prefix in ('rev_a', 'q', 'Rev_A', 'revision_'):
            assert call('--print-schema-id', '--name-prefix', prefix, '--generate', 'truck.engine', truck) == identity
            assert call('--describe', '--name-prefix', prefix, truck) == description
            for reflective in (False, True):
                plain, named = work / 'plain.qbs', work / 'named.qbs'
                options = ['--qbs-reflective'] if reflective else []
                call('--emit-qbs', plain, *options, truck)
                call('--emit-qbs', named, *options, '--name-prefix', prefix, truck)
                assert plain.read_bytes() == named.read_bytes()
                assert named.read_bytes()[12:28].hex() == identity.strip()
        for prefix in ('', '9bad', '_reserved', 'double__underscore', 'not.valid', 'dash-name',
                       'class', 'def', 'func', 'match', 'std', 'quarry', 'kQuarrySchemaId', 'quarry_gen_example'):
            call('--name-prefix', prefix, '--list-outputs', truck, status=2)
        call('--name-prefix', 'rev_a', '--name-prefix', 'rev_b', truck, status=2)
        call('--name-prefix', status=2)
        call('--name-prefix', 'truck', '--generate', 'truck.engine', truck, status=2)

        # No source namespace is selected, but the complete configuration must
        # still reject a conflicting leading namespace segment.
        schema = work / 'a.brd'
        schema.write_text('namespace: omitted.child\nrecord: Status\nversion: 1\ntype: data\nfields:\n  value:\n    type: u16\n')
        root = work / 'configuration.brd'
        (work / 'selected.brd').write_text('namespace: selected\nrecord: Other\nversion: 1\ntype: data\nfields:\n  value:\n    type: u16\n')
        root.write_text('configuration: names\nimports:\n  - a.brd\n  - selected.brd\n')
        call('--name-prefix', 'omitted', '--generate', 'selected', '--list-outputs', root, status=2)

        # C namespace joins and case-only prefixes previously collided (#119).
        # Opt-in encoding must distinguish them without changing legacy output.
        cases = [('rev_a', 'dep.engine', 'revq0a_dep_engine'),
                 ('rev', 'a_dep.engine', 'rev_aq0dep_engine'),
                 ('lower', 'model', 'lower_model'), ('Lower', 'model', 'Lower_model'),
                 ('q', 'model', 'qq_model')]
        for variant, flags, extension, cpp in [
                ('c', ['--language', 'c'], '.h', False),
                ('c-cpp', ['--language', 'c'], '.h', True),
                ('cpp-c', ['--language', 'cpp', '--cpp-storage', 'c'], '.hpp', True)]:
            headers = []
            stems = []
            for number, (prefix, namespace, stem) in enumerate(cases):
                schema = work / f'enum-{number}.brd'
                schema.write_text('namespace: ' + namespace + '\nrecord: Status\nversion: 1\ntype: data\n'
                                  'enums:\n  Mode:\n    values:\n      off: 0\n      on: 1\n'
                                  'fields:\n  value:\n    type: u16\n  mode:\n    type: Mode\n')
                out = work / f'{variant}-adversarial-{number}'
                call(*flags, '--name-prefix', prefix, '-o', out, schema)
                headers.append(next(out.rglob('*generated' + extension)))
                stems.append(stem)
            text = ''.join('#include "' + p.as_posix() + '"\n' for p in headers)
            text += 'int main(void) {\n  unsigned char bytes[256];\n'
            for number, stem in enumerate(stems):
                text += f'''  {stem}_Status_t v{number};
  {stem}_Status_init(&v{number});
  v{number}.has_value = true; v{number}.value = 42;
  v{number}.has_mode = true; v{number}.mode = {stem}_MODE_ON;
  {stem}_Status_encode_result_t e{number} = {stem}_Status_encode(&v{number}, bytes, sizeof(bytes));
  if (e{number}.status != QUARRY_C_STATUS_OK) return 1;
  {stem}_Status_decode_result_t d{number} = {stem}_Status_decode(bytes, e{number}.bytes_written);
  if (d{number}.status != QUARRY_C_STATUS_OK || d{number}.value.value != 42 ||
      d{number}.value.mode != {stem}_MODE_ON) return 2;
'''
            text += '  return 0;\n}\n'
            compile_run(text, variant + '-adversarial', [], cpp=cpp)
        print('72 baseline hashes, seven variants, QBS/identity, selections, negative dependencies and C/C++ enum coexistence passed.')


if __name__ == '__main__':
    main()
