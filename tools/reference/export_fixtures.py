#!/usr/bin/env python3
"""Exports the inputs and expected results behind cc700-test-vectors.md as JSON for the Kotlin tests
(android/core/src/test/resources/vectors.json). It imports vectors.py, so the cases are the exact
objects the vectors file was generated from (vectors.py also rewrites its own copy of the .md)."""
import contextlib
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
with contextlib.redirect_stdout(io.StringIO()):
    import vectors as V  # noqa: E402
import ref  # noqa: E402

out = {
    'w1': {'doc': V.W1, 'hex': V.obj1.hex(), 'id': V.id1.hex(), 'crc': V.crc1},
    'w2': {'doc': V.W2, 'hex': V.obj2.hex(), 'id': V.id2.hex(), 'crc': ref.crc32(V.obj2)},
    'valid': [{'name': n, 'doc': d} for n, d in V.valid],
    'invalid': [{'name': n, 'doc': d, 'code': c, 'path': p} for n, d, c, p in V.cases],
    'messages': [{'name': n, 'hex': m.hex()} for n, m in V.msgs],
    'v8': {'profile': [float(x) for x in V.prof], 'spacing': 10, 'climbs': V.cl},
    'v9': {'points': V.pts, 'prof_step': V.step, 'profile_dm': V.prof_dm, 'turns': V.turns, 'climbs': V.climbs,
           'name': 'Test loop', 'hex': V.robj.hex(), 'id': V.rid.hex(), 'crc': ref.crc32(V.robj), 'dist': V.rdist,
           'ascent': V.rasc},
}
dst = os.path.join(HERE, '..', '..', 'android', 'core', 'src', 'test', 'resources', 'vectors.json')
os.makedirs(os.path.dirname(dst), exist_ok=True)
json.dump(out, open(dst, 'w'), indent=1, ensure_ascii=False)
print('wrote', os.path.normpath(dst), len(out['valid']), 'valid', len(out['invalid']), 'invalid')
