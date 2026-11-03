"""WP3: simunit runs a scripted 30-minute ride headlessly and leaves a FIT file fitcheck accepts; the
protocol works end to end over simunit's TCP link (HELLO, FTP, workouts from the starter set, routes,
ride sync with CRC)."""
import json
import os
import socket
import subprocess
import sys
import time

import pytest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SIMUNIT = os.path.join(ROOT, 'build', 'host', 'simunit', 'simunit')
FIX = os.path.join(ROOT, 'core', 'tests', 'fixtures')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'linkclient'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'fitcheck'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'reference'))
import cc700link  # noqa: E402
import fitcheck  # noqa: E402
import ref  # noqa: E402

pytestmark = pytest.mark.skipif(not os.path.exists(SIMUNIT), reason='simunit not built')


def test_scripted_30_minute_ride(tmp_path):
    root = tmp_path / 'CC700'
    subprocess.run([SIMUNIT, '--root', str(root), '--port', '0', '--script', os.path.join(FIX, 'ride30.csv'),
                    '--speed', '0', '--finish', '--exit', '--utc', '1793000000', '--quiet'], check=True, timeout=60)
    fits = sorted(p for p in os.listdir(root / 'rides') if p.endswith('.fit'))
    assert fits == ['1793000000.fit']
    assert not [p for p in os.listdir(root / 'rides') if p.endswith('.part')]
    r = fitcheck.check(str(root / 'rides' / fits[0]))
    s = r['session']
    # 1800 s of script; the red light stops 60 s and auto-pause takes 5 s to engage and 2 s to resume
    assert 1735 <= s['total_timer_time'] <= 1760
    assert r['counts']['record'] == int(s['total_timer_time'])
    assert 13000 < s['total_distance'] < 15000
    assert s['avg_power'] > 200
    # The 8 s power-meter silence leaves records without power (omitted, not 0)
    assert sum(1 for x in r['records'] if x.get('power') is None) >= 5
    # 20 s without a fix leaves records without a position
    assert sum(1 for x in r['records'] if x.get('position_lat') is None) >= 15
    # only folders of our own were created
    assert sorted(os.listdir(root)) == ['config', 'objects', 'rides']


def _free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


@pytest.fixture
def unit(tmp_path):
    port = _free_port()
    proc = subprocess.Popen([SIMUNIT, '--root', str(tmp_path / 'CC700'), '--port', str(port), '--speed', '1',
                             '--utc', '1793000000', '--quiet'])
    for _ in range(100):
        try:
            c = cc700link.Client(port)
            break
        except OSError:
            time.sleep(0.05)
    else:
        proc.kill()
        raise RuntimeError('simunit did not start')
    yield c, tmp_path / 'CC700', port, proc
    c.close()
    proc.terminate()
    proc.wait(5)


def _workout(name):
    doc = json.load(open(os.path.join(ROOT, 'claude', 'workouts', name)))
    ref.validate(doc)
    obj, _ = ref.encode_object(doc['name'], ref.to_percent_form(doc))
    return doc, obj


def test_protocol_end_to_end(unit):
    c, root, port, proc = unit
    st, h = c.hello()
    assert st == cc700link.OK and h['proto'] == 1 and h['state'] == 0
    assert (h['major'], h['minor'], h['patch']) == (0, 1, 0)
    assert h['workout_free'] == 20 and h['route_free'] == 10
    assert c.ftp_get() == 200
    assert c.ftp_set(260) == cc700link.OK and c.ftp_get() == 260
    assert c.ftp_set(601) == cc700link.BAD_ARG
    assert c.time_set(1793000000, 480) == cc700link.OK
    # The six starter workouts
    names = sorted(f for f in os.listdir(os.path.join(ROOT, 'claude', 'workouts')) if f.endswith('.json'))
    for n in names:
        doc, obj = _workout(n)
        assert c.put(1, obj) == cc700link.OK, n
        assert c.put(1, obj) == cc700link.EXISTS
    lst = c.obj_list(1)
    assert sorted(x['name'] for x in lst) == sorted(_workout(n)[0]['name'] for n in names)
    assert {x['name']: x['nsteps'] for x in lst}['VO2max 30-15 3 x 13'] == 84
    # A route (V§9 style object from gen_ride)
    route = open(os.path.join(FIX, 'route.cr1'), 'rb').read()
    assert c.put(2, route) == cc700link.OK
    rl = c.obj_list(2)
    assert rl[0]['name'] == 'Kings Park test' and rl[0]['nclimbs'] == 1
    # Delete one workout
    assert c.delete(1, lst[0]['id']) == cc700link.OK
    assert len(c.obj_list(1)) == 5
    assert c.hello()[1]['workout_free'] == 15
    # No rides yet
    assert c.ride_list() == []


def test_ride_sync_after_scripted_ride(tmp_path):
    root = tmp_path / 'CC700'
    subprocess.run([SIMUNIT, '--root', str(root), '--port', '0', '--script', os.path.join(FIX, 'redlight.csv'),
                    '--speed', '0', '--finish', '--exit', '--utc', '1793000000', '--quiet'], check=True, timeout=60)
    port = _free_port()
    proc = subprocess.Popen([SIMUNIT, '--root', str(root), '--port', str(port), '--speed', '1', '--quiet',
                             '--mtu', '23'])
    try:
        for _ in range(100):
            try:
                c = cc700link.Client(port, mtu=23)
                break
            except OSError:
                time.sleep(0.05)
        rides = c.ride_list()
        assert len(rides) == 1 and rides[0]['ride_id'] == 1793000000 and rides[0]['flags'] == 0
        data = c.ride_fetch(1793000000)  # 512-byte chunks at MTU 23, CRC checked
        assert data == open(root / 'rides' / '1793000000.fit', 'rb').read()
        r = fitcheck.check(data)
        assert r['counts']['session'] == 1
        assert c.ride_mark(1793000000, True) == cc700link.OK
        assert c.ride_list()[0]['flags'] == 1
        c.close()
    finally:
        proc.terminate()
        proc.wait(5)
