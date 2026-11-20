#!/usr/bin/env python3
"""Execute the consumer guide's literal CLI examples and compile their output."""
import argparse
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def run(args, cwd, failure=None):
    result = subprocess.run(args, cwd=cwd, text=True, capture_output=True, check=False)
    if failure is None and result.returncode != 0:
        raise AssertionError(f"{shlex.join(args)}\n{result.stdout}\n{result.stderr}")
    if failure is not None:
        assert result.returncode != 0, f"unexpected success: {shlex.join(args)}"
        assert failure in result.stderr, result.stderr
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('source', 'compiler', 'cc', 'cxx', 'generated-include'):
        parser.add_argument('--' + option, required=True)
    args = parser.parse_args()
    source = Path(args.source).resolve()
    guide = source / 'docs/generated-binding-compatibility.md'
    markdown = guide.read_text()
    commands = []
    for block in re.findall(r'```sh\n(.*?)```', markdown, re.S):
        for line in block.replace('\\\n', ' ').splitlines():
            tokens = shlex.split(line, comments=True)
            if tokens:
                assert tokens[0] == 'quarry-schema-compiler', tokens
                commands.append([args.compiler, *tokens[1:]])
    assert commands, 'guide has no runnable examples'
    for target in re.findall(r'\]\(([^)]+)\)', markdown):
        if not target.startswith(('https://', 'http://', '#')):
            assert (guide.parent / target.split('#')[0]).exists(), target

    with tempfile.TemporaryDirectory(prefix='quarry-consumer-docs-') as temporary:
        root = Path(temporary)
        shutil.copytree(source / 'examples/truck/schemas', root / 'examples/truck/schemas')
        groups = {}
        for command in commands:
            run(command, root)
            if '--output-directory' not in command:
                continue
            directory = command[command.index('--output-directory') + 1]
            planned = run([*command, '--list-outputs'], root).splitlines()
            groups.setdefault(directory, [command, set()])[1].update(planned)

        for directory, (command, planned) in groups.items():
            output = root / directory
            actual = {str(path.relative_to(root)) for path in output.rglob('*') if path.is_file()}
            assert actual == planned, (actual, planned)
            baseline = root / ('baseline-' + output.name)
            full = command.copy()
            if '--generate' in full:
                pos = full.index('--generate')
                del full[pos:pos + 2]
            full[full.index('--output-directory') + 1] = str(baseline)
            run(full, root)
            for path in output.rglob('*'):
                if path.is_file():
                    assert path.read_bytes() == (baseline / path.relative_to(output)).read_bytes(), path

            plain_c = '--language' in command and command[command.index('--language') + 1] == 'c'
            header = 'truck/engine.generated.' + ('h' if plain_c else 'hpp')
            translation_unit = root / ('consumer-' + output.name + '.cpp')
            translation_unit.write_text(f'#include "{header}"\nint main(void) {{ return 0; }}\n')
            common = ['-Wall', '-Wextra', '-Wpedantic', '-Werror', '-fsyntax-only',
                      '-I' + str(source / 'include'), '-I' + args.generated_include,
                      '-I' + str(output)]
            run([args.cxx, '-std=c++20', *common, str(translation_unit)], root)
            if plain_c:
                run([args.cc, '-std=c99', '-x', 'c', *common, str(translation_unit)], root)

            if '--c-optimize-size=truck.common' in command:
                # A stale default-storage dependency must fail for the documented mixed policy.
                bad = root / ('bad-' + output.name)
                incompatible = [token for token in command
                                if token != '--c-optimize-size=truck.common']
                incompatible[incompatible.index('--generate') + 1] = 'truck.common'
                incompatible[incompatible.index('--output-directory') + 1] = str(bad)
                run(incompatible, root)
                shutil.copy2(output / header, bad / header)
                bad_flags = [('-I' + str(bad)) if token == '-I' + str(output) else token
                             for token in common]
                run([args.cxx, '-std=c++20', *bad_flags, str(translation_unit)], root,
                    failure='different generated definitions')
    print(f'Validated {len(commands)} literal CLI commands, planned outputs, selection independence, '
          'C99/C++20 headers and incompatible dependency rejection.')


if __name__ == '__main__':
    main()
