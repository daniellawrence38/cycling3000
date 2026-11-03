"""A small Python client for the CC700 link protocol over simunit's TCP framing (u16 LE length plus one
BLE packet). Used by the end-to-end tests; the Android app has its own Kotlin client."""
import socket
import struct
import zlib

OK, BAD_CRC, FULL, BUSY, BAD_OBJECT, BAD_OPCODE, BAD_ARG, NOT_FOUND, TOO_BIG, EXISTS, NO_SESSION, OFFSET, IO_ERROR = range(13)


class LinkError(Exception):
    pass


class Client:
    def __init__(self, port, mtu=247, timeout=3.0):
        self.s = socket.create_connection(('127.0.0.1', port), timeout=timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.cap = mtu - 4
        self.req = 0
        self.events = []
        self.buf = b''

    def close(self):
        self.s.close()

    # ------------------------------------------------------------ framing
    def _send_msg(self, msg):
        parts = [msg[i:i + self.cap] for i in range(0, len(msg), self.cap)] or [b'']
        out = b''
        for i, d in enumerate(parts):
            h = (0x80 if i == 0 else 0) | (0x40 if i == len(parts) - 1 else 0) | (i & 0x3F)
            pkt = bytes([h]) + d
            out += struct.pack('<H', len(pkt)) + pkt
        self.s.sendall(out)

    def _recv_pkt(self):
        while True:
            if len(self.buf) >= 2:
                n = struct.unpack_from('<H', self.buf)[0]
                if len(self.buf) >= 2 + n:
                    pkt, self.buf = self.buf[2:2 + n], self.buf[2 + n:]
                    return pkt
            chunk = self.s.recv(65536)
            if not chunk:
                raise LinkError('connection closed')
            self.buf += chunk

    def _recv_msg(self):
        msg, ctr = b'', 0
        while True:
            p = self._recv_pkt()
            h = p[0]
            if h & 0x80:
                msg, ctr = b'', 0
            if (h & 0x3F) != (ctr & 0x3F):
                raise LinkError('counter mismatch')
            msg += p[1:]
            ctr += 1
            if h & 0x40:
                return msg

    def request(self, op, payload=b''):
        self.req = self.req % 255 + 1
        self._send_msg(bytes([op, self.req]) + payload)
        while True:
            m = self._recv_msg()
            # Events and responses share opcodes 0xA0 to 0xA4 (RIDE_LIST response is 0xA0, EVENT_STATE is
            # 0xA0): an event is told apart by req_id 0, which the phone never uses (QUESTIONS E7).
            if m[0] >= 0xA0 and m[1] == 0:
                self.events.append(m)
                continue
            if m[0] != (op | 0x80) or m[1] != self.req:
                raise LinkError(f'unexpected response {m[:2].hex()}')
            return m[2], m[3:]

    # ------------------------------------------------------------ requests
    def hello(self):
        st, p = self.request(0x01, b'\x01')
        return st, dict(zip(('proto', 'major', 'minor', 'patch', 'battery', 'state', 'workout_free', 'route_free'), p))

    def ftp_get(self):
        st, p = self.request(0x02)
        return struct.unpack('<H', p)[0]

    def ftp_set(self, w):
        return self.request(0x03, struct.pack('<H', w))[0]

    def time_set(self, unix, tz_min):
        return self.request(0x04, struct.pack('<Ih', unix, tz_min))[0]

    def obj_list(self, kind):
        st, p = self.request(0x10, bytes([kind]))
        out, n, o = [], p[0], 1
        for _ in range(n):
            oid, size, nl = p[o:o + 8], *struct.unpack_from('<IB', p, o + 8)
            name = p[o + 13:o + 13 + nl].decode()
            o += 13 + nl
            if kind == 1:
                total, nsteps = struct.unpack_from('<IB', p, o); o += 5
                out.append(dict(id=oid, size=size, name=name, total_secs=total, nsteps=nsteps))
            else:
                dist, asc, nc = struct.unpack_from('<IHB', p, o); o += 7
                out.append(dict(id=oid, size=size, name=name, dist_m=dist, ascent_m=asc, nclimbs=nc))
        return out

    def put(self, kind, obj, chunk=None):
        chunk = chunk or (4000 if self.cap + 4 >= 185 else 512)
        st, _ = self.request(0x11, bytes([kind]) + obj[4:12] + struct.pack('<II', len(obj), zlib.crc32(obj)))
        if st != OK:
            return st
        off = 0
        while off < len(obj):
            st, p = self.request(0x12, struct.pack('<I', off) + obj[off:off + chunk])
            if st != OK:
                return st
            off = struct.unpack('<I', p)[0]
        return self.request(0x13)[0]

    def delete(self, kind, oid):
        return self.request(0x14, bytes([kind]) + oid)[0]

    def ride_list(self):
        st, p = self.request(0x20)
        n = p[0]
        return [dict(zip(('ride_id', 'size', 'flags', 'duration_s', 'distance_m'),
                         struct.unpack_from('<IIBII', p, 1 + 17 * i))) for i in range(n)]

    def ride_fetch(self, ride_id, chunk=None):
        chunk = chunk or (4000 if self.cap + 4 >= 185 else 512)
        st, p = self.request(0x21, struct.pack('<I', ride_id))
        if st != OK:
            raise LinkError(f'RIDE_OPEN status {st}')
        size, crc = struct.unpack('<II', p)
        data = b''
        while len(data) < size:
            st, p = self.request(0x22, struct.pack('<IIH', ride_id, len(data), chunk))
            if st != OK or not p:
                raise LinkError(f'RIDE_READ status {st}')
            data += p
        if zlib.crc32(data) != crc:
            raise LinkError('ride CRC mismatch')
        return data

    def ride_mark(self, ride_id, uploaded):
        return self.request(0x23, struct.pack('<IB', ride_id, 1 if uploaded else 0))[0]
