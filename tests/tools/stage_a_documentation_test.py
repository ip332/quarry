#!/usr/bin/env python3
"""Check relative Markdown links and execute the Stage A CLI guide."""
import argparse
from collections import Counter
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
from urllib.parse import unquote


def headings(markdown):
    counts = Counter()
    anchors = set(re.findall(r'<a\s+(?:id|name)=["\']([^"\']+)', markdown))
    for heading in re.findall(r'^#{1,6}\s+(.*)$', markdown, re.M):
        heading = re.sub(r'<[^>]+>', '', heading).lower()
        slug = ''.join(c for c in heading if c.isalnum() or c in '_- ').replace(' ', '-')
        count = counts[slug]
        counts[slug] += 1
        anchors.add(slug + ('-' + str(count) if count else ''))
    return anchors


def check_links(source):
    names = subprocess.check_output(['git', '-C', str(source), 'ls-files', '*.md'], text=True)
    # Also exercise newly added documents before staging during development.
    files = {source / name for name in names.splitlines()}
    files.update((source / 'docs').glob('*v0.1.31-rc.1.md'))
    files.update((source / 'docs/specifications').glob('configuration-and-code-identity.md'))
    files.add(source / 'docs/stage-a-cli.md')
    links = 0
    for path in sorted(files):
        markdown = path.read_text()
        for destination in re.findall(r'\]\(([^)\s]+)(?:\s+"[^"]*")?\)', markdown):
            if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', destination):
                continue
            target_name, _, fragment = destination.partition('#')
            target = path.parent / unquote(target_name) if target_name else path
            assert target.exists(), f'{path.relative_to(source)}: missing {destination}'
            if fragment and target.is_file() and target.suffix == '.md':
                assert unquote(fragment) in headings(target.read_text()), (
                    f'{path.relative_to(source)}: missing heading {destination}')
            links += 1
    return len(files), links


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    source = Path(args.source).resolve()
    compiler = str(Path(args.compiler).resolve())
    files, links = check_links(source)
    guide = (source / 'docs/stage-a-cli.md').read_text()
    commands = []
    for block in re.findall(r'```sh\n(.*?)```', guide, re.S):
        for line in block.replace('\\\n', ' ').splitlines():
            words = shlex.split(line, comments=True)
            if words:
                assert words[0] == 'quarry-schema-compiler', words
                commands.append([compiler, *words[1:]])
    assert commands
    with tempfile.TemporaryDirectory(prefix='quarry-stage-a-docs-') as temporary:
        root = Path(temporary)
        shutil.copytree(source / 'examples/truck', root / 'examples/truck')

        def run(flags, status=0, diagnostic=None):
            result = subprocess.run([compiler, *map(str, flags)], cwd=root,
                                    text=True, capture_output=True, check=False)
            assert result.returncode == status, (flags, result.returncode, result.stderr)
            if diagnostic:
                assert diagnostic in result.stderr, (flags, result.stderr)
            elif status == 0:
                assert not result.stderr, result.stderr
            return result.stdout

        outputs = {}
        for command in commands:
            outputs[tuple(command[1:])] = run(command[1:])
        schema = 'examples/truck/schemas/configuration.brd'
        identity = run(['--print-schema-id', schema]).strip()
        assert identity in guide and identity == '9200ca494f010801b830d0857cf66dde'
        assert run(['--print-generated-code-api-version']).strip() == '3'
        description = run(['--describe', schema])
        assert 'formats: brf 2, qbs 1, schema-ir 1' in description
        assert 'generated-code epochs: cpp 3, c 7, python 1, go 1' in description
        readiness = (source / 'docs/release-readiness-v0.1.31-rc.1.md').read_text()
        for record_id, fqn in re.findall(r'^  (0x[0-9a-f]{8}) (\S+)$', description, re.M):
            assert f'| {fqn} | {record_id} |' in readiness, (record_id, fqn)
        assert len(run(['--list-inputs', schema]).splitlines()) == 9
        help_text = run(['--help'])
        assert '```text\n' + help_text + '```' in (source / 'tools/README.md').read_text()
        for reflective in (False, True):
            name = 'reflective.qbs' if reflective else 'minimal.qbs'
            image = root / 'build/stage-a-docs' / name
            original = image.read_bytes()
            assert original[:5] == b'QBS\x00\x01', original[:5]
            assert original[8] == 1 and original[12:28].hex() == identity
            run(['--emit-qbs', image, *(['--qbs-reflective'] if reflective else []), schema])
            assert image.read_bytes() == original
        for flags, listed in outputs.items():
            if '--list-outputs' in flags:
                directory = root / flags[flags.index('--output-directory') + 1]
                assert set(listed.splitlines()) == {
                    str(p.relative_to(root)) for p in directory.rglob('*') if p.is_file()}
        run(['--qbs-reflective', schema], 2, '--qbs-reflective requires --emit-qbs')
        run(['--generate', 'missing', schema], 1, 'BC8002')
        run(['--describe', '--print-schema-id', schema], 2)
        run(['--name-prefix', 'bad-prefix', schema], 2)
        run(['--language', 'c', '--c-optimize-size=missing', schema], 1)
        rev_b = 'examples/truck/multi_configuration/rev_b/configuration.brd'
        assert run(['--print-schema-id', rev_b]).strip() == '806e9a4e11d0df71c4e5b51045f85010'
        assert '0x00030002 truck.cms.MirrorFold' in run(['--describe', rev_b])
    print(f'Checked {files} Markdown files / {links} relative links and heading fragments; '
          f'executed {len(commands)} literal CLI examples, QBS headers, mappings and error controls.')


if __name__ == '__main__':
    main()
