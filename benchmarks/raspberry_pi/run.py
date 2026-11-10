#!/usr/bin/env python3
"""Cross-build existing native benchmarks and execute them on an ARMv7 Linux Pi."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile
import uuid

SOURCE = Path(__file__).resolve().parents[2]
CASES = ('telemetry', 'configuration', 'nested', 'large', 'stress')
BACKENDS = {'c': 'quarry_benchmark_c', 'cpp': 'quarry_benchmark_cpp',
            'cpp-c-storage': 'quarry_benchmark_cpp_c_storage',
            'protobuf-cpp': 'quarry_benchmark_protobuf_cpp',
            'protobuf-cpp-arena': 'quarry_benchmark_protobuf_cpp_arena'}
OPERATIONS = ('encode', 'decode', 'round_trip')


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def remote(host, args, **kwargs):
    return run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', host,
                shlex.join([str(a) for a in args])], **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='SSH destination, e.g. root@100.81.6.20')
    parser.add_argument('--compiler', type=Path, required=True, help='Host-built quarry-schema-compiler')
    parser.add_argument('--version-include', type=Path, required=True, help='Matching build/generated directory')
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--image', default='quarry-pi-armhf:noble')
    parser.add_argument('--smoke', action='store_true', help='8 records, one iteration/sample; correctness only')
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--cpu', type=int, default=2, help='Pin processes to this target CPU (default: 2)')
    args = parser.parse_args()
    if args.runs < 1 or (args.cpu is not None and args.cpu < 0):
        parser.error('runs must be positive and cpu must be nonnegative')
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    # Each output directory is a distinct experiment; never overwrite old results.
    output.mkdir(parents=True, exist_ok=False)
    build.mkdir(parents=True, exist_ok=True)
    generated = build / 'generated'
    generated.mkdir(exist_ok=True)
    for language in ('c', 'cpp'):
        for schema in ('workload_shared.brd', 'workload.brd'):
            run([args.compiler.resolve(), '--language', language, '--output-directory', generated,
                 SOURCE / 'benchmarks/schemas' / schema])
    wrapper_generated = build / 'wrapper_generated'
    for schema in ('workload_shared.brd', 'workload.brd'):
        run([args.compiler.resolve(), '--language', 'cpp', '--cpp-storage', 'c',
             '--output-directory', wrapper_generated, SOURCE / 'benchmarks/schemas' / schema])
    for relative in ('quarry/runtime_c/version.h', 'quarry/runtime/version.hpp'):
        destination = generated / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.version_include / relative, destination)
    images = run(['docker', 'image', 'ls', '--no-trunc', '--format',
                  '{{.Repository}}:{{.Tag}} {{.ID}}'], capture_output=True, text=True).stdout
    matches = [line.split()[1] for line in images.splitlines()
               if line.split()[0] == args.image or line.split()[1] == args.image]
    if len(set(matches)) != 1:
        raise RuntimeError(f'Expected one locally available image for {args.image}; build the Dockerfile first')
    image_id = matches[0]
    inputs = {'image': image_id}
    for directory in ('include', 'runtime_c', 'benchmarks/c', 'benchmarks/cpp',
                      'benchmarks/protobuf', 'benchmarks/schemas', 'benchmarks/raspberry_pi'):
        for path in (SOURCE / directory).rglob('*'):
            if path.is_file() and path.suffix not in ('.py', '.md', '.pyc'):
                inputs[str(path.relative_to(SOURCE))] = sha(path)
    for directory in (generated, wrapper_generated):
        for path in directory.rglob('*'):
            if path.is_file():
                inputs[directory.name + '/' + str(path.relative_to(directory))] = sha(path)
    stamp = build / 'build-inputs.json'
    cached = json.loads(stamp.read_text()) if stamp.exists() else {}
    binaries = list(BACKENDS.values()) + ['pin_cpu']
    current_binaries = {name: sha(build / 'bin' / name) for name in binaries
                        if (build / 'bin' / name).exists()}
    if cached.get('inputs') != inputs or cached.get('binaries') != current_binaries or len(current_binaries) != len(binaries):
        run(['docker', 'run', '--rm', '--mount', f'type=bind,src={SOURCE},dst=/src,readonly',
             '--mount', f'type=bind,src={build},dst=/out', image_id,
             'sh', '/src/benchmarks/raspberry_pi/build_armhf.sh'])
        stamp.write_text(json.dumps({'inputs': inputs, 'binaries': {
            name: sha(build / 'bin' / name) for name in binaries}}, indent=2) + '\n')
    else:
        print('Reusing verified binaries: build inputs and binary hashes match', flush=True)
    datasets = build / 'datasets'
    datasets.mkdir(exist_ok=True)
    for case in CASES:
        command = [sys.executable, SOURCE / 'benchmarks/scripts/generate_dataset.py',
                   '--case', case, '--output', datasets / f'{case}.dataset']
        if args.smoke:
            command += ['--count', '8']
        run(command)
    manifest = {
        'started_utc': datetime.now(timezone.utc).isoformat(), 'ssh_destination': args.host,
        'quarry_commit': run(['git', '-C', SOURCE, 'rev-parse', 'HEAD'], capture_output=True,
                             text=True).stdout.strip(),
        'tracked_changes': run(['git', '-C', SOURCE, 'diff', 'HEAD', '--stat'],
                               capture_output=True, text=True).stdout,
        'schema_compiler_sha256': sha(args.compiler.resolve()),
        'compiler_version': run([args.compiler.resolve(), '--version'], capture_output=True,
                                text=True).stdout.strip(),
        'image_id': image_id,
        'toolchain': (build / 'toolchain.txt').read_text(), 'smoke': args.smoke,
        'cpu_affinity': args.cpu, 'runs': args.runs,
        'binary_bytes': {k: (build / 'bin' / v).stat().st_size for k, v in BACKENDS.items()},
        'generation_options': '--language c / --language cpp / --language cpp --cpp-storage c; default presence',
        'sha256': {str(p.relative_to(build)): sha(p) for directory in
                   ('bin', 'lib', 'generated', 'wrapper_generated', 'datasets', 'protobuf')
                   for p in (build / directory).rglob('*') if p.is_file()},
        'source_sha256': {str(p.relative_to(SOURCE)): sha(p) for directory in
                          ('include', 'runtime_c', 'compiler/backend_c', 'tools/schema_compiler',
                           'benchmarks/c', 'benchmarks/cpp', 'benchmarks/protobuf',
                           'benchmarks/schemas', 'benchmarks/raspberry_pi')
                          for p in (SOURCE / directory).rglob('*') if p.is_file()},
    }
    remote_dir = '/tmp/quarry-perf-' + uuid.uuid4().hex
    manifest['remote_dir'] = remote_dir
    archive = build / 'deploy.tar.gz'
    with tarfile.open(archive, 'w:gz') as tar:
        for directory in ('bin', 'lib', 'datasets'):
            tar.add(build / directory, arcname=directory)
    remote(args.host, ['mkdir', '-p', remote_dir])
    with archive.open('rb') as payload:
        remote(args.host, ['tar', '-xzf', '-', '-C', remote_dir], stdin=payload)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    inspect = '''uname -a; cat /proc/cpuinfo; cat /proc/meminfo; df -k /tmp;
for f in /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq /sys/class/thermal/thermal_zone0/temp; do echo "$f"; cat "$f"; done
if command -v vcgencmd >/dev/null; then vcgencmd get_throttled; vcgencmd measure_temp; fi
'''
    for repeat in range(args.runs):
        result_dir = output / f'run-{repeat + 1:03d}'
        result_dir.mkdir()
        for phase in ('before', 'after'):
            if phase == 'before':
                (result_dir / 'system-before.txt').write_bytes(
                    remote(args.host, ['sh', '-c', inspect], capture_output=True).stdout)
                paths = []
                for case in CASES:
                    for operation in OPERATIONS:
                        group = []
                        for backend, binary in BACKENDS.items():
                            name = f'{case}-{backend}-{operation}.json'
                            command = ['env', f'LD_LIBRARY_PATH={remote_dir}/lib']
                            if args.cpu is not None:
                                command += [f'{remote_dir}/bin/pin_cpu', str(args.cpu)]
                            command += [f'{remote_dir}/bin/{binary}', '--dataset',
                                        f'{remote_dir}/datasets/{case}.dataset', '--case', case,
                                        '--operation', operation, '--output', f'{remote_dir}/{name}',
                                        '--warmup', '0' if args.smoke else '2', '--iterations',
                                        '1' if args.smoke else '10', '--samples', '1' if args.smoke else '5']
                            remote(args.host, command)
                            path = result_dir / name
                            path.write_bytes(remote(args.host, ['cat', f'{remote_dir}/{name}'],
                                                    capture_output=True).stdout)
                            value = json.loads(path.read_text())
                            value.update({'wire_format': 'protobuf' if backend.startswith('protobuf')
                                          else 'brf-v2', 'git_commit': manifest['quarry_commit'],
                                          'cpu_model': 'Raspberry Pi 3 (see system-before.txt)',
                                          'architecture': 'armv7l', 'build_mode': 'Release'})
                            suffixes = ('.h', '.c') if backend == 'c' else ('.hpp',)
                            metrics_root = wrapper_generated if backend == 'cpp-c-storage' else generated
                            generated_files = [p for p in metrics_root.rglob('*')
                                               if p.is_file() and p.suffix in suffixes
                                               and 'benchmark' in p.relative_to(metrics_root).parts]
                            if backend.startswith('protobuf'):
                                generated_files = list((build / 'protobuf').glob('workload.pb.*'))
                            value['resources'].update({
                                'generated_source_bytes': sum(p.stat().st_size for p in generated_files),
                                'generated_files': len(generated_files),
                                'binary_size': (build / 'bin' / binary).stat().st_size,
                                'runtime_size': None,
                            })
                            path.write_text(json.dumps(value, indent=2) + '\n')
                            group.append(path)
                            paths.append(path)
                        run([sys.executable, SOURCE / 'benchmarks/scripts/validate_results.py', *group])
            else:
                (result_dir / 'system-after.txt').write_bytes(
                    remote(args.host, ['sh', '-c', inspect], capture_output=True).stdout)
        print(f'Completed {result_dir}: {len(paths)} validated measurements', flush=True)
    manifest['completed_utc'] = datetime.now(timezone.utc).isoformat()
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Results: {output}; deployed files retained at {args.host}:{remote_dir}', flush=True)


if __name__ == '__main__':
    main()
