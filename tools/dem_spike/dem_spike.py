#!/usr/bin/env python3
"""Elevation spike (brief 7.4, WP5): asks the Geoscience Australia 5 m LiDAR DEM and Open Topo Data
(SRTM 30 m) for the same Perth points, and reports coverage, values and the request rate that works.

Run on a PC with internet access (the cloud build environment could not reach either host):
    python3 tools/dem_spike/dem_spike.py > dem-spike.md
then paste the output into docs/DEM-FINDINGS.md. Standard library only. Sends no key or personal data.
"""
import json
import statistics
import sys
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

GA = 'https://services.ga.gov.au/gis/rest/services/DEM_LiDAR_5m_2025/MapServer'
OT = 'https://api.opentopodata.org/v1/srtm30m'

# (name, lat, lon): Kings Park (summit and the escarpment), Swan River foreshore, Perth Hills
POINTS = [
    ('Kings Park, Fraser Ave', -31.9606, 115.8410),
    ('Kings Park, DNA Tower', -31.9604, 115.8331),
    ('Kings Park escarpment, Mounts Bay Rd', -31.9620, 115.8460),
    ('Swan River foreshore, Elizabeth Quay', -31.9590, 115.8580),
    ('Swan River foreshore, South Perth', -31.9700, 115.8520),
    ('Perth Hills, Kalamunda', -31.9740, 116.0570),
    ('Perth Hills, Lesmurdie Falls', -31.9930, 116.0490),
    ('Perth Hills, Mundaring Weir', -31.9550, 116.1630),
]


def get(url, timeout=20):
    t = time.time()
    req = urllib.request.Request(url, headers={'User-Agent': 'cc700-dem-spike'})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read()
    return json.loads(body), (time.time() - t) * 1000


def ga_identify(lat, lon):
    q = urllib.parse.urlencode({
        'geometry': f'{lon},{lat}', 'geometryType': 'esriGeometryPoint', 'sr': '4326', 'tolerance': '0',
        'mapExtent': f'{lon - 0.001},{lat - 0.001},{lon + 0.001},{lat + 0.001}', 'imageDisplay': '100,100,96',
        'returnGeometry': 'false', 'f': 'json'})
    j, ms = get(f'{GA}/identify?{q}')
    for r in j.get('results', []):
        v = r.get('attributes', {}).get('Pixel Value')
        try:
            return float(v), ms, None
        except (TypeError, ValueError):
            return None, ms, f'pixel value {v!r}'
    return None, ms, json.dumps(j)[:200]


def main():
    out = ['# DEM spike results', '', time.strftime('Run %Y-%m-%d %H:%M %Z'), '']
    try:
        info, ms = get(f'{GA}?f=json')
        out += [f'GA service: reachable ({ms:.0f} ms). Description: {info.get("serviceDescription", "")[:300]!r}', '']
    except Exception as e:  # noqa: BLE001
        out += [f'GA service: NOT reachable: {e}', '']
    out += ['| point | GA 5 m (m) | GA ms | Open Topo 30 m (m) |', '|---|---|---|---|']
    ot = {}
    try:
        locs = '|'.join(f'{la},{lo}' for _, la, lo in POINTS)
        j, _ = get(f'{OT}?locations={urllib.parse.quote(locs)}')
        ot = {i: r.get('elevation') for i, r in enumerate(j.get('results', []))}
    except Exception as e:  # noqa: BLE001
        out.insert(4, f'Open Topo Data: NOT reachable: {e}')
    for i, (name, la, lo) in enumerate(POINTS):
        try:
            v, ms, err = ga_identify(la, lo)
            ga = f'{v:.2f}' if v is not None else f'none ({err})'
        except Exception as e:  # noqa: BLE001
            ga, ms = f'error: {e}', 0
        out.append(f'| {name} | {ga} | {ms:.0f} | {ot.get(i, "-")} |')
    # Request rate: 40 points along Mounts Bay Rd at 4 concurrent requests (the app's limit)
    line = [(-31.9620 + k * 0.0002, 115.8460 - k * 0.0004) for k in range(40)]
    t = time.time()
    with ThreadPoolExecutor(4) as ex:
        res = list(ex.map(lambda p: ga_identify(*p), line))
    dt = time.time() - t
    ok = [r for r in res if r[0] is not None]
    lat = [r[1] for r in res]
    out += ['', f'Rate test: 40 GA requests, 4 at a time: {dt:.1f} s total, {len(ok)} with a value, '
            f'median {statistics.median(lat):.0f} ms, max {max(lat):.0f} ms, {40 / dt:.1f} requests per second.']
    print('\n'.join(out))


if __name__ == '__main__':
    sys.exit(main())
