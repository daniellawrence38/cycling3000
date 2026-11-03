"""Independent checks of the C FIT writer and recovery with fitdecode (brief 5.6, WP2).

The C test test_fit writes the synthetic 30-minute ride (finished and as a .part) to a folder; this
test decodes the finished file and recovers the .part at cut points through libcc_testshim.so.
By default every 101st cut plus every cut in the first and last 1 KB is checked; set CC_FIT_FULL=1 to
check every byte offset (about 45,000 cuts, several minutes on 4 cores)."""
import ctypes
import os
import subprocess
from concurrent.futures import ProcessPoolExecutor

import pytest

import fitcheck

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BUILD = os.path.join(ROOT, 'build', 'host', 'core')
SHIM = os.path.join(BUILD, 'libcc_testshim.so')
T0 = 1793000000


def _fit_files(tmp):
    exe = os.path.join(BUILD, 'test_fit')
    if not os.path.exists(exe):
        pytest.skip('build/host not built; run make test-c-plain first')
    env = dict(os.environ, CC_FIT_OUT=str(tmp), CC_FIT_QUICK='1')
    subprocess.run([exe], cwd=os.path.join(ROOT, 'core', 'tests'), env=env, check=True,
                   stdout=subprocess.DEVNULL)
    return {n: open(os.path.join(tmp, n), 'rb').read() for n in os.listdir(tmp)}


@pytest.fixture(scope='module')
def files(tmp_path_factory):
    return _fit_files(tmp_path_factory.mktemp('fit'))


def test_finished_file_totals(files):
    r = fitcheck.check(files['synthetic.fit'])
    c, s = r['counts'], r['session']
    assert c['record'] == 1800 and c['lap'] == 2 and c['session'] == 1 and c['activity'] == 1
    assert s['total_timer_time'] == 1800.0
    assert s['total_elapsed_time'] == 1860.0
    assert s['total_distance'] == 14400.0          # 1800 records at 8 m/s
    assert s['sport'] == 'cycling'
    powers = [x['power'] for x in r['records'] if x.get('power') is not None]
    assert len(powers) == 1790                        # 10 s of power loss omitted, not zero
    assert s['avg_power'] == (sum(powers) * 2 + len(powers)) // (2 * len(powers))
    assert all(x.get('heart_rate') is None for i, x in enumerate(r['records']) if i in (0, 97 * 2))


def test_unknown_time_file(files):
    r = fitcheck.check(files['unknown-time.fit'])
    first = r['records'][0]['timestamp']
    assert int(first.timestamp()) == T0
    secs = [int(x['timestamp'].timestamp()) for x in r['records']]
    expect = [T0 + s for s in range(1860) if not 600 <= s < 660]
    assert secs == expect


_lib = None
_part = None


def _init(part):
    global _lib, _part
    _lib = ctypes.CDLL(SHIM)
    _part = part


def _check_cut(cut):
    buf = ctypes.create_string_buffer(len(_part) + 4096)
    n = _lib.shim_fit_recover(_part, cut, buf, len(buf), 0)
    expect = _lib.shim_fit_records(_part, cut)
    if n == 0:
        return (cut, expect == 0, 'deleted')
    if n < 0:
        return (cut, False, 'error')
    try:
        r = fitcheck.check(buf.raw[:n])
    except Exception as e:  # noqa: BLE001
        return (cut, False, str(e))
    return (cut, r['counts']['record'] == expect and r['counts']['session'] == 1, r['counts']['record'])


def test_recovery_cuts_decode(files):
    if not os.path.exists(SHIM):
        pytest.skip('libcc_testshim.so not built')
    part = files['synthetic.fit.part']
    first_def_end = 14 + 6 + 3 * 5
    if os.environ.get('CC_FIT_FULL'):
        cuts = list(range(first_def_end, len(part) + 1))
    else:
        cuts = sorted(set(range(first_def_end, min(len(part), 1024) + 1))
                      | set(range(max(first_def_end, len(part) - 1024), len(part) + 1))
                      | set(range(first_def_end, len(part) + 1, 101)))
    with ProcessPoolExecutor(max_workers=os.cpu_count(), initializer=_init, initargs=(part,)) as ex:
        bad = [r for r in ex.map(_check_cut, cuts, chunksize=64) if not r[1]]
    assert not bad, bad[:5]
    print(f'checked {len(cuts)} cuts')
