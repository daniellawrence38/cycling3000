#!/usr/bin/env python3
"""Synthetic rides, sensor scripts and routes for the core tests (brief 2, WP2).

Writes into core/tests/fixtures/:
  indoor.csv     20 min on a trainer: GNSS speed jitters 0 to 0.5 m/s, cadence 85, power 200 W
  redlight.csv   60 s at 8 m/s, 60 s stopped (no pedalling), 60 s at 8 m/s
  climb.csv      60 s flat, 120 s up a 5 % road, 60 s flat, all at 5 m/s
  route.cr1      a 2.4 km route near Kings Park with a left turn, a 5 % climb and a right turn
  route.gpx      the same route as GPX (for the app tests later)
  route_ride.csv a replay along route.cr1: on route, off route and back, then a detour on a parallel street

CSV columns, one row per second:
  t, fix (1/0), lat_e7, lon_e7, speed_mms, pa (-1 none), power (-1 no sample), cad (rpm, -1 no crank data),
  hr (-1 no sample), alt_m (GNSS altitude)

The scenario timings are documented next to each generator so the C tests can state their expected
values by hand. Deterministic (fixed random seed)."""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'reference'))
import ref  # noqa: E402

OUT = os.path.join(ROOT, 'core', 'tests', 'fixtures')
LAT0, LON0 = -319640000, 1158300000           # near Kings Park, Perth
M_PER_E7_LAT = 1e-7 * 111320.0
M_PER_E7_LON = 1e-7 * 111320.0 * math.cos(math.radians(-31.964))
SEA_PA = 101325.0


def pa_at(h):
    """Inverse of brief 5.3: h = 44330 * (1 - (p / 101325) ^ 0.190263)."""
    return SEA_PA * (1 - h / 44330.0) ** (1 / 0.190263)


def at(x_east_m, y_north_m):
    return (LAT0 + round(y_north_m / M_PER_E7_LAT), LON0 + round(x_east_m / M_PER_E7_LON))


def write_csv(name, rows):
    with open(os.path.join(OUT, name), 'w') as f:
        f.write('t,fix,lat_e7,lon_e7,speed_mms,pa,power,cad,hr,alt_m\n')
        for r in rows:
            f.write(','.join(str(v) for v in r) + '\n')


def row(t, x, y, speed, alt, power=200, cad=85, hr=140, fix=1, baro=True):
    lat, lon = at(x, y)
    pa = round(pa_at(alt)) if baro else -1
    return (t, fix, lat, lon, round(speed * 1000), pa, power, cad, hr, round(alt, 2))


def indoor():
    """1200 s: speed uniform 0..0.5 m/s, cadence 85, power 200. Never auto-pauses (pedalling)."""
    rnd = random.Random(7)
    return [row(t, 0, 0, rnd.uniform(0, 0.5), 20.0) for t in range(1200)]


def redlight():
    """t 0..59 riding 8 m/s; t 60..119 stopped: speed 0, cadence 0 (no new crank events), power 0;
    t 120..179 riding 8 m/s. Auto-pause after 5 s below 0.7 m/s without pedalling; resume after 2 s at
    1.4 m/s or more (the 3 s speed mean)."""
    rows, x = [], 0.0
    for t in range(180):
        stopped = 60 <= t < 120
        v = 0.0 if stopped else 8.0
        x += v
        rows.append(row(t, x, 0, v, 20.0, power=0 if stopped else 220, cad=0 if stopped else 90))
    return rows


def climb():
    """5 m/s throughout. t 0..59 flat at 20 m; t 60..179 the road rises 5 % (0.25 m per second, 30 m);
    t 180..239 flat at 50 m."""
    rows, x = [], 0.0
    for t in range(240):
        x += 5.0
        alt = 20.0 + 0.25 * min(max(t - 59, 0), 120)
        rows.append(row(t, x, 0, 5.0, alt))
    return rows


# ------------------------------------------------------------------ route
# Leg A: east 1000 m. Left turn (north) at 1000 m. Leg B: north 600 m, rising 5 % (30 m).
# Right turn (east) at 1600 m. Leg C: east 800 m. Arrive at 2400 m. Points every 50 m.
def route_points():
    pts = [(x, 0.0) for x in range(0, 1001, 50)]
    pts += [(1000.0, y) for y in range(50, 601, 50)]
    pts += [(x, 600.0) for x in range(1050, 1801, 50)]
    return [at(x, y) for x, y in pts]


def route_alt(d):
    return 20.0 + 0.05 * min(max(d - 1000, 0), 600)


def build_route():
    pts = route_points()
    step = 25
    dist = int(sum(ref.haversine_m(pts[i], pts[i + 1]) for i in range(len(pts) - 1)) + 0.5)
    prof = [round(route_alt(i * step) * 10) for i in range(dist // step + 1)]
    turns = [dict(dist_m=0, type=10, exit=0, street=''),
             dict(dist_m=1000, type=2, exit=0, street='Fraser Ave'),
             dict(dist_m=1600, type=5, exit=0, street=''),
             dict(dist_m=2400, type=9, exit=0, street='')]
    climbs = [dict(start_m=1000, length_m=600, ascent_m=30, avg_x10=50, max_x10=50)]
    obj, oid, d, asc = ref.encode_route('Kings Park test', pts, step, prof, turns, climbs)
    return obj, pts, d


def route_ride():
    """5 m/s. Positions along the route as (x, y) in metres:
    t 0..99    on route, distance 5*t (0..495 m along leg A)
    t 100..119 off route: moving south from (500, 0) at 5 m/s, 7.5..102.5 m from the route
    t 120..139 coming back north at 5 m/s: 97.5..2.5 m from the route
               (the 2.5 m offset keeps every sample clear of the 40 m and 25 m thresholds)
    t 140..239 on route, 500..995 m
    t 240..359 leg B north (the climb), 1000..1595 m along the route
    t 360..419 leg C east, 1600..1895 m
    t 420..479 detour: a parallel street 60 m north of leg C, east from x=1300 to x=1595
               (alongside route distance 1900..2195 m)
    t 480..519 back on leg C from x=1600 (route distance 2200 m) to x=1795 (2395 m)
    Off route needs 8 s beyond 40 m at 3 km/h or more; it ends after 3 s within 25 m."""
    rows = []
    for t in range(520):
        if t < 100:
            x, y = 5.0 * t, 0.0
        elif t < 120:
            x, y = 500.0, -5.0 * (t - 99) - 2.5
        elif t < 140:
            x, y = 500.0, -102.5 + 5.0 * (t - 119)
        elif t < 240:
            x, y = 5.0 * (t - 40), 0.0
        elif t < 360:
            x, y = 1000.0, 5.0 * (t - 240)
        elif t < 420:
            x, y = 1000.0 + 5.0 * (t - 360), 600.0
        elif t < 480:
            x, y = 1300.0 + 5.0 * (t - 420), 660.0
        else:
            x, y = 1600.0 + 5.0 * (t - 480), 600.0
        d_along = 1000 + 5.0 * (t - 240) if 240 <= t < 360 else 0
        alt = route_alt(d_along) if 240 <= t < 360 else (50.0 if t >= 360 else 20.0)
        rows.append(row(t, x, y, 5.0, alt))
    return rows


def ride30():
    """A 30-minute outdoor ride for simunit (WP3): 1800 rows. Laps around a 2 km square near Kings Park at
    7 to 9 m/s with power 150 to 330 W, heart rate 120 to 165, cadence 80 to 95; a 60 s stop at a red
    light from 900 s (auto-pause); 20 s without a GNSS fix from 1300 s; the power meter silent for 8 s
    from 1500 s. The barometer follows a gentle 10 m rise and fall per lap."""
    rnd = random.Random(30)
    rows, d = [], 0.0
    for t in range(1800):
        stopped = 900 <= t < 960
        v = 0.0 if stopped else 7.0 + 2.0 * (0.5 + 0.5 * math.sin(t / 50.0))
        d += v
        s = d % 2000.0
        side, u = int(s // 500), s % 500
        x, y = [(u, 0), (500, u), (500 - u, 500), (0, 500 - u)][side]
        alt = 20.0 + 5.0 * (1 - math.cos(2 * math.pi * (d % 2000.0) / 2000.0))
        power = -1 if 1500 <= t < 1508 else (0 if stopped else int(240 + 90 * math.sin(t / 37.0) + rnd.randint(-10, 10)))
        cad = 0 if stopped else 80 + (t // 60) % 16
        hr = int(140 + 25 * math.sin(t / 200.0))
        r = list(row(t, x, y, v, alt, power=power, cad=cad, hr=hr))
        if 1300 <= t < 1320:
            r[1] = 0  # no fix
        rows.append(tuple(r))
    return rows


def gpx(pts):
    s = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<gpx version="1.1" creator="cc700 gen_ride" xmlns="http://www.topografix.com/GPX/1/1">',
         '<trk><name>Kings Park test</name><trkseg>']
    for lat, lon in pts:
        s.append(f'<trkpt lat="{lat / 1e7:.7f}" lon="{lon / 1e7:.7f}"></trkpt>')
    s += ['</trkseg></trk>', '</gpx>']
    return '\n'.join(s) + '\n'


def main():
    os.makedirs(OUT, exist_ok=True)
    write_csv('indoor.csv', indoor())
    write_csv('redlight.csv', redlight())
    write_csv('climb.csv', climb())
    obj, pts, dist = build_route()
    open(os.path.join(OUT, 'route.cr1'), 'wb').write(obj)
    open(os.path.join(OUT, 'route.gpx'), 'w').write(gpx(pts))
    write_csv('route_ride.csv', route_ride())
    write_csv('ride30.csv', ride30())
    print('fixtures written; route', len(obj), 'bytes,', dist, 'm,', len(pts), 'points')


if __name__ == '__main__':
    main()
