#!/usr/bin/env python3
"""Validates FIT files with an independent decoder (fitdecode, MIT).

Usage: fitcheck.py FILE [FILE ...]
Prints message counts and the session totals; exits 1 if any file fails to decode or fails its CRC.
"""
import collections
import io
import sys

import fitdecode


def check(data):
    """data: bytes or a path. Returns {'counts': Counter, 'session': dict, 'records': [dict]}.
    Raises on any decode or CRC error."""
    src = io.BytesIO(data) if isinstance(data, (bytes, bytearray)) else data
    counts = collections.Counter()
    session = None
    records = []
    with fitdecode.FitReader(src, check_crc=fitdecode.CrcCheck.RAISE,
                             error_handling=fitdecode.ErrorHandling.RAISE) as fr:
        for frame in fr:
            if isinstance(frame, fitdecode.FitDataMessage):
                counts[frame.name] += 1
                if frame.name == 'session':
                    session = {f.name: f.value for f in frame.fields}
                elif frame.name == 'record':
                    records.append({f.name: f.value for f in frame.fields})
    return {'counts': counts, 'session': session, 'records': records}


def main(argv):
    bad = 0
    for path in argv[1:]:
        try:
            r = check(path)
        except Exception as e:  # noqa: BLE001 - report every failure
            print(f'{path}: FAIL {e}')
            bad += 1
            continue
        s = r['session'] or {}
        print(f"{path}: OK {dict(r['counts'])} distance={s.get('total_distance')} "
              f"timer={s.get('total_timer_time')} avg_power={s.get('avg_power')}")
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
