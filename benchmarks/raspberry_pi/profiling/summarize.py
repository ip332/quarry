#!/usr/bin/env python3
"""Attribute perf IP counts using an addr2line matching the target ELF."""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('profile', type=Path)
parser.add_argument('executable', type=Path)
parser.add_argument('--addr2line', default='arm-linux-gnueabihf-addr2line')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
lines = args.profile.read_text().splitlines()
rows = [line.split() for line in lines if line and not line.startswith('#')]
symbols = subprocess.run([args.addr2line, '-f', '-C', '-e', str(args.executable)],
                         input='\n'.join(row[0] for row in rows)+'\n',
                         text=True, capture_output=True, check=True).stdout.splitlines()
if len(symbols) != 2*len(rows):
    raise SystemExit('Unexpected addr2line output')
functions = Counter()
locations = Counter()
for i, (_, count) in enumerate(rows):
    functions[symbols[2*i]] += int(count)
    locations[(symbols[2*i], symbols[2*i+1])] += int(count)
total = sum(functions.values())
if not total:
    raise SystemExit('No samples collected')
result = {'collection': lines[0], 'total_samples': total,
          'functions': [{'function': f, 'samples': n, 'percent': 100*n/total}
                        for f,n in functions.most_common()],
          'locations': [{'function': f, 'location': loc, 'samples': n, 'percent': 100*n/total}
                        for (f,loc),n in locations.most_common()]}
args.output.write_text(json.dumps(result, indent=2)+'\n')
