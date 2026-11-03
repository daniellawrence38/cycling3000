#!/usr/bin/env python3
"""Encodes the starter workouts (claude/workouts/*.workout.json) and W1, W2 to CW1 objects for the
C host tests, using the reference implementation. Writes core/tests/fixtures/<name>.cw1 and an
index file `workouts.txt` with: file, size, nsteps, total_secs, id hex, crc32 hex."""
import glob, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import ref

out_dir = os.path.join(ROOT, 'core', 'tests', 'fixtures')
os.makedirs(out_dir, exist_ok=True)
lines = []
for path in sorted(glob.glob(os.path.join(ROOT, 'claude', 'workouts', '*.workout.json'))):
    doc = json.load(open(path))
    ref.validate(doc)
    steps = ref.to_percent_form(doc)
    obj, oid = ref.encode_object(doc['name'], steps)
    name = os.path.basename(path).replace('.workout.json', '')
    open(os.path.join(out_dir, name + '.cw1'), 'wb').write(obj)
    lines.append(f"{name}.cw1 {len(obj)} {len(steps)} {sum(s['secs'] for s in steps)} {oid.hex()} {ref.crc32(obj):08x}")
open(os.path.join(out_dir, 'workouts.txt'), 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
