#!/usr/bin/env python3
"""Build and run all five truck naming-isolation examples using installed Quarry."""
import argparse
import contextlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True, type=Path, help='installed Quarry prefix')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--python', default=sys.executable)
    parser.add_argument('--python-runtime', type=Path, help='source Python runtime; omit for installed wheel')
    parser.add_argument('--go', default='go')
    parser.add_argument('--skip-go', action='store_true', help='run native and Python checks without a Go toolchain')
    parser.add_argument('--go-runtime', type=Path)
    parser.add_argument('--go-module', default='example.com/fleet')
    parser.add_argument('--work', type=Path)
    args = parser.parse_args()
    example = Path(__file__).resolve().parent
    source = example.parents[2]
    go_runtime = (args.go_runtime or source / 'runtime/go').resolve()
    context = contextlib.nullcontext(str(args.work.resolve())) if args.work else tempfile.TemporaryDirectory(prefix='quarry-truck-multi-')
    with context as temporary:
        work = Path(temporary)
        work.mkdir(parents=True, exist_ok=True)

        def run(command, **options):
            subprocess.run(list(map(str, command)), cwd=work, check=True, **options)

        native = work / 'native'
        run([args.cmake, '-S', example, '-B', native, '-DCMAKE_PREFIX_PATH=' + str(args.prefix.resolve())])
        run([args.cmake, '--build', native, '--parallel', '4'])
        for name in ('truck_multi_cpp', 'truck_multi_c', 'truck_multi_c_storage'):
            run([native / name])
        ids = (native / 'schema_ids.txt').read_text().splitlines()
        print('Complete configuration identities:', *ids, flush=True)
        compiler = args.prefix.resolve() / 'bin/quarry-schema-compiler'
        roots = [example.parent / 'schemas/configuration.brd', example / 'rev_b/configuration.brd']
        python_output = work / 'python'
        go_output = work / 'go'
        go_output.mkdir(exist_ok=True)
        # The original source names also remain usable and emit the same wire bytes.
        run([compiler, '--language', 'python', '-o', python_output, roots[0]])
        run([compiler, '--language', 'go', '--go-import-prefix', args.go_module + '/gen',
             '-o', go_output / 'gen', roots[0]])
        for revision, root in zip(('rev_a', 'rev_b'), roots):
            run([compiler, '--language', 'python', '--name-prefix', revision, '-o', python_output, root])
            run([compiler, '--language', 'go', '--name-prefix', revision,
                 '--go-import-prefix', args.go_module + '/gen', '-o', go_output / 'gen', root])
        environment = os.environ.copy()
        environment['PYTHONDONTWRITEBYTECODE'] = '1'
        environment['PYTHONPATH'] = str(python_output) + (os.pathsep + str(args.python_runtime.resolve()) if args.python_runtime else '')
        run([args.python, example / 'python.py'], env=environment)
        if args.skip_go:
            print('Native and Python coexistence passed; Go execution explicitly skipped.')
            return
        if not shutil.which(args.go):
            raise SystemExit('Go 1.22+ is required for the dual-import example')
        module = ('module ' + args.go_module + '\n\ngo 1.22\n\n'
                  'require github.com/ip332/quarry/runtime/go v0.0.0\n'
                  'replace github.com/ip332/quarry/runtime/go => "' + go_runtime.as_posix() + '"\n')
        (go_output / 'go.mod').write_text(module)
        (go_output / 'main.go').write_text((example / 'main.go.in').read_text().replace('@MODULE@', args.go_module))
        environment.update(GOWORK='off', GOPROXY='off', GOSUMDB='off', GOTOOLCHAIN='local',
                           GOCACHE=str(work / 'go-cache'), GOPATH=str(work / 'go-path'))
        subprocess.run([args.go, 'build', './...'], cwd=go_output, env=environment, check=True)
        subprocess.run([args.go, 'run', '.'], cwd=go_output, env=environment, check=True)
    print('All five truck variants coexist and execute successfully.')


if __name__ == '__main__':
    main()
