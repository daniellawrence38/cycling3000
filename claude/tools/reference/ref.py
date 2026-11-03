#!/usr/bin/env python3
"""Reference implementation for CC700 workout, protocol and algorithm test vectors.

This file is the executable source of the numbers in claude/cc700-test-vectors.md.
Everything uses integer arithmetic with explicit round-half-up so a C or Kotlin port can match bit for bit.
"""
import hashlib, json, math, re, struct, zlib

# ----------------------------------------------------------------------------- validation
class VError(Exception):
    def __init__(self, code, path):
        super().__init__(f'{code} at {path}')
        self.code, self.path = code, path


STEP_KEYS = {'label', 'secs', 'ftp_pct', 'ramp', 'rpm'}
GROUP_KEYS = {'reps', 'steps'}
LABEL_RE = re.compile(r'^[A-Za-z0-9 ]{1,16}$')


def is_int(x):
    return isinstance(x, int) and not isinstance(x, bool)


def tenths(x, path):
    """Percent value -> integer tenths of a percent. 0.1 resolution, 1.0 to 300.0."""
    if isinstance(x, bool) or not isinstance(x, (int, float)):
        raise VError('E_PCT', path)
    t = round(x * 10)
    if abs(x * 10 - t) > 1e-6 or not (10 <= t <= 3000):
        raise VError('E_PCT', path)
    return t


def validate_step(s, path):
    if not isinstance(s, dict):
        raise VError('E_STEP_TYPE', path)
    for k in s:
        if k not in STEP_KEYS:
            raise VError('E_UNKNOWN_FIELD', f'{path}/{k}')
    for k in ('label', 'secs', 'ftp_pct'):
        if k not in s:
            raise VError('E_FIELD_MISSING', f'{path}/{k}')
    lab = s['label']
    if not isinstance(lab, str) or not LABEL_RE.match(lab) or not lab.strip():
        raise VError('E_LABEL', f'{path}/label')
    if not is_int(s['secs']) or not (5 <= s['secs'] <= 43200):
        raise VError('E_SECS', f'{path}/secs')
    p = s['ftp_pct']
    ramp = s.get('ramp', False)
    if not isinstance(ramp, bool):
        raise VError('E_RAMP', f'{path}/ramp')
    if isinstance(p, list):
        if len(p) != 2:
            raise VError('E_PCT', f'{path}/ftp_pct')
        a, b = tenths(p[0], f'{path}/ftp_pct/0'), tenths(p[1], f'{path}/ftp_pct/1')
        if not ramp and not a < b:
            raise VError('E_PCT', f'{path}/ftp_pct')
        if ramp and a == b:
            raise VError('E_RAMP', f'{path}/ramp')
    else:
        tenths(p, f'{path}/ftp_pct')
        if ramp:
            raise VError('E_RAMP', f'{path}/ramp')
    if 'rpm' in s and (not is_int(s['rpm']) or not (40 <= s['rpm'] <= 130)):
        raise VError('E_RPM', f'{path}/rpm')


def validate(doc):
    """Raises VError for the first problem in document order. Returns the document when valid."""
    if not isinstance(doc, dict):
        raise VError('E_JSON', '')
    for k in doc:
        if k not in ('v', 'name', 'steps'):
            raise VError('E_UNKNOWN_FIELD', f'/{k}')
    if doc.get('v') != 1 or not is_int(doc.get('v')):
        raise VError('E_VERSION', '/v')
    name = doc.get('name')
    if not isinstance(name, str) or not (1 <= len(name) <= 40) or not all(0x20 <= ord(c) <= 0x7E for c in name):
        raise VError('E_NAME', '/name')
    steps = doc.get('steps')
    if not isinstance(steps, list) or not steps:
        raise VError('E_STEPS_EMPTY', '/steps')
    count = 0
    total = 0
    for i, item in enumerate(steps):
        path = f'/steps/{i}'
        if isinstance(item, dict) and ('reps' in item or 'steps' in item):
            for k in item:
                if k not in GROUP_KEYS:
                    raise VError('E_UNKNOWN_FIELD', f'{path}/{k}')
            if not is_int(item.get('reps')) or not (2 <= item['reps'] <= 30):
                raise VError('E_REPS', f'{path}/reps')
            inner = item.get('steps')
            if not isinstance(inner, list) or not inner:
                raise VError('E_STEPS_EMPTY', f'{path}/steps')
            for j, s in enumerate(inner):
                if isinstance(s, dict) and ('reps' in s or 'steps' in s):
                    raise VError('E_NESTED', f'{path}/steps/{j}')
                validate_step(s, f'{path}/steps/{j}')
            count += item['reps'] * len(inner)
            total += item['reps'] * sum(s['secs'] for s in inner)
        else:
            validate_step(item, path)
            count += 1
            total += item['secs']
        if count > 100:
            raise VError('E_TOO_MANY_STEPS', '/steps')
    if total > 86400:
        raise VError('E_TOO_LONG', '/steps')
    return doc


# ----------------------------------------------------------------------------- conversion (phone side)
def to_percent_form(doc):
    steps = []
    for item in doc['steps']:
        group = [(s, r, item['reps']) for r in range(1, item['reps'] + 1) for s in item['steps']] if 'reps' in item else [(item, 0, 0)]
        for s, ri, rt in group:
            p = s['ftp_pct']
            ramp = bool(s.get('ramp', False))
            if isinstance(p, list):
                p0, p1 = round(p[0] * 10), round(p[1] * 10)
            else:
                n = round(p * 10)
                p0, p1 = (n * 95 + 50) // 100, (n * 105 + 50) // 100
            steps.append(dict(label=s['label'], secs=s['secs'], p0=p0, p1=p1, ramp=ramp, rpm=s.get('rpm', 0), rep_idx=ri, rep_total=rt))
    return steps


def encode_object(name, steps):
    """CW1 binary object. Returns (bytes, id8)."""
    total = sum(s['secs'] for s in steps)
    b = b'CW' + bytes([1, 0]) + b'\x00' * 8 + struct.pack('<IBB', total, len(steps), len(name)) + name.encode('ascii')
    for s in steps:
        lab = s['label'].encode('ascii')
        b += struct.pack('<HHHBBBBB', s['secs'], s['p0'], s['p1'], 1 if s['ramp'] else 0, s['rpm'], s['rep_idx'], s['rep_total'], len(lab)) + lab
    oid = hashlib.sha256(b).digest()[:8]
    return b[:4] + oid + b[12:], oid


def decode_object(b):
    assert b[:2] == b'CW' and b[2] == 1
    oid = b[4:12]
    total, n, nl = struct.unpack_from('<IBB', b, 12)
    name = b[18:18 + nl].decode('ascii')
    off = 18 + nl
    steps = []
    for _ in range(n):
        secs, p0, p1, fl, rpm, ri, rt, ll = struct.unpack_from('<HHHBBBBB', b, off)
        off += 11
        steps.append(dict(label=b[off:off + ll].decode('ascii'), secs=secs, p0=p0, p1=p1, ramp=bool(fl & 1), rpm=rpm, rep_idx=ri, rep_total=rt))
        off += ll
    assert off == len(b)
    return name, total, oid, steps


# ----------------------------------------------------------------------------- unit side maths
def watts(ftp, p10):
    return (ftp * p10 + 500) // 1000


def ramp_p10(s, e):
    """Instantaneous target (tenths of percent) at elapsed seconds e (0..secs). Floor division, half up.

    `//` is floor division. For a descending ramp the numerator is negative, so a C or Kotlin port must use
    floor division (cc_floordiv / Math.floorDiv), not `/`, which truncates toward zero and gives a different answer.
    """
    return s['p0'] + ((s['p1'] - s['p0']) * e * 2 + s['secs']) // (2 * s['secs'])


def resolve(ftp, s, e=0):
    """Returns (low_w, high_w). Ramp steps are resolved at elapsed e: target +/- 5 %."""
    if not s['ramp']:
        return watts(ftp, s['p0']), watts(ftp, s['p1'])
    p = ramp_p10(s, e)
    return watts(ftp, (p * 95 + 50) // 100), watts(ftp, (p * 105 + 50) // 100)


def band(r):
    return 1 if r <= 55 else 2 if r <= 75 else 3 if r <= 90 else 4 if r <= 105 else 5 if r <= 120 else 6 if r <= 150 else 7


def zone_of_power(w, ftp):
    return band((w * 200 + ftp) // (2 * ftp))


def target_zone(s, e=0):
    p = ramp_p10(s, e) if s['ramp'] else (s['p0'] + s['p1']) // 2
    return band((p + 5) // 10)


def pill(range_w, samples, dwell=2):
    """Status pill state machine. samples: 3 s average power once per second. Returns list of (state, delta)."""
    # Leaving IN needs `dwell` consecutive seconds outside on the same side. Once out, crossing to the
    # other side switches at once. Returning inside the range is immediate.
    lo, hi = range_w
    state, out, k, side = 'IN', [], 0, 0
    for p in samples:
        if lo <= p <= hi:
            state, k, side = 'IN', 0, 0
        else:
            s = 1 if p > hi else -1
            if state != 'IN':
                state = 'HIGH' if s > 0 else 'LOW'
            else:
                k = k + 1 if s == side else 1
                side = s
                if k >= dwell:
                    state = 'HIGH' if s > 0 else 'LOW'
        delta = (p - hi) if state == 'HIGH' else (lo - p) if state == 'LOW' else 0
        out.append((state, delta))
    return out


def pill_step(range_w, samples, step_secs, settle=5, min_secs=60, dwell=2):
    """Pill over one step. samples[e] is the 3 s power at elapsed second e of the step.
    Returns 'HIDDEN' where the pill shows no state: the whole step if it is shorter than min_secs,
    otherwise the first `settle` seconds. The state machine starts fresh at elapsed `settle`."""
    if step_secs < min_secs:
        return ['HIDDEN'] * len(samples)
    return ['HIDDEN'] * min(settle, len(samples)) + pill(range_w, samples[settle:], dwell)


# ----------------------------------------------------------------------------- sensors
def parse_hr(b):
    return b[1] if not (b[0] & 1) else b[1] | (b[2] << 8)


def parse_cps(b):
    flags, power = struct.unpack_from('<Hh', b, 0)
    off = 4
    res = dict(power=power, crank=None)
    if flags & 0x0001: off += 1          # pedal power balance
    if flags & 0x0004: off += 2          # accumulated torque
    if flags & 0x0010: off += 6          # wheel revolution data
    if flags & 0x0020:
        res['crank'] = struct.unpack_from('<HH', b, off)
    return res


def bin_mean(samples):
    """One-second power or heart-rate bin: half-up integer mean of the samples received in that second.
    Returns None for an empty second (the caller then applies the 2 s stale rule)."""
    if not samples:
        return None
    n = len(samples)
    return (sum(samples) * 2 + n) // (2 * n)


def cadence(prev, cur):
    """prev, cur = (crank_revs u16, event_time u16 in 1/1024 s). Returns rpm (int) or None when no new event."""
    dr = (cur[0] - prev[0]) & 0xFFFF
    dt = (cur[1] - prev[1]) & 0xFFFF
    if dr == 0 or dt == 0:
        return None
    return (dr * 60 * 1024 * 2 + dt) // (2 * dt)


# ----------------------------------------------------------------------------- protocol framing
def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def fragment(msg, mtu_payload):
    """Split msg into packets. Packet = HDR + data. HDR bit7 START, bit6 END, bits0-5 counter."""
    cap = mtu_payload - 1
    parts = [msg[i:i + cap] for i in range(0, len(msg), cap)] or [b'']
    out = []
    for i, d in enumerate(parts):
        h = (0x80 if i == 0 else 0) | (0x40 if i == len(parts) - 1 else 0) | (i & 0x3F)
        out.append(bytes([h]) + d)
    return out


def message(opcode, req_id, payload=b''):
    return bytes([opcode, req_id]) + payload


# ----------------------------------------------------------------------------- climb detection (phone side)
def detect_climbs(e, spacing=10, H=5.0, merge_drop=8.0, merge_gap=150, min_len=300, min_ascent=15.0, min_grade=3.0):
    n = len(e)
    raw, v, m = [], 0, 0
    for i in range(1, n):
        if e[m] - e[i] >= H:
            raw.append((v, m))
            v = min(range(m, i + 1), key=lambda k: e[k])
            m = v
        elif e[i] <= e[v]:      # <= so a flat base slides the start to its last sample
            v = m = i
        elif e[i] > e[m]:
            m = i
    raw.append((v, m))
    merged = []
    for seg in raw:
        if seg[1] == seg[0]:
            continue
        if merged:
            pv, pm = merged[-1]
            if e[pm] - e[seg[0]] <= merge_drop and (seg[0] - pm) * spacing <= merge_gap:
                merged[-1] = (pv, seg[1] if e[seg[1]] >= e[pm] else pm)
                continue
        merged.append(seg)
    out = []
    for v, m in merged:
        length = (m - v) * spacing
        asc = e[m] - e[v]
        if length < min_len or asc < min_ascent or asc / length * 100 < min_grade:
            continue
        w = max(1, 50 // spacing)
        mg = max((e[i + w] - e[i]) / (w * spacing) * 100 for i in range(v, m - w + 1)) if m - v >= w else asc / length * 100
        out.append(dict(start_m=v * spacing, length_m=length, ascent_m=int((asc * 2 + 1) // 2),
                        avg_x10=int((asc / length * 1000 * 2 + 1) // 2), max_x10=int((mg * 10 * 2 + 1) // 2)))
    return out


# ----------------------------------------------------------------------------- route object (CR1)
def zz(n):
    return (n << 1) ^ (n >> 63) if n >= 0 else ((-n) << 1) - 1


def varint(u):
    out = bytearray()
    while True:
        b = u & 0x7F
        u >>= 7
        if u:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def haversine_m(a, b):
    R = 6371000.0
    la1, lo1, la2, lo2 = [math.radians(x / 1e7) for x in (a[0], a[1], b[0], b[1])]
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * R * math.asin(math.sqrt(h))


def encode_route(name, pts, prof_step, prof, turns, climbs):
    dist = int(sum(haversine_m(pts[i], pts[i + 1]) for i in range(len(pts) - 1)) + 0.5)
    ascent = sum(max(0, prof[i + 1] - prof[i]) for i in range(len(prof) - 1)) // 10
    nm = name.encode('ascii')
    b = b'CR' + bytes([1, 0]) + b'\x00' * 8 + struct.pack('<IHHHBHHB', dist, ascent, len(pts), len(turns), len(climbs), len(prof), prof_step, len(nm)) + nm
    b += struct.pack('<ii', *pts[0])
    for p, q in zip(pts, pts[1:]):
        b += varint(zz(q[0] - p[0])) + varint(zz(q[1] - p[1]))
    for a in prof:
        b += struct.pack('<h', a)
    for t in turns:
        st = t['street'].encode('ascii')
        b += struct.pack('<IBBB', t['dist_m'], t['type'], t['exit'], len(st)) + st
    for c in climbs:
        b += struct.pack('<IHHhh', c['start_m'], c['length_m'], c['ascent_m'], c['avg_x10'], c['max_x10'])
    oid = hashlib.sha256(b).digest()[:8]
    return b[:4] + oid + b[12:], oid, dist, ascent
