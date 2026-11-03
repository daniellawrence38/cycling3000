#!/usr/bin/env python3
"""A mock Intervals.icu for trying the app's upload without a real account (brief WP6).

Answers GET /api/v1/athlete/0/activities with the activities uploaded so far (so a second upload of the
same ride is seen as a duplicate) and POST /api/v1/athlete/0/activities by saving the FIT file. Any
Authorization header is accepted and never printed.

  python3 tools/android/mock_intervals.py [--port 8800] [--dir /tmp/mock-intervals] [--fail 401|429|503|422]

In the app (Settings, Connection, Simulated unit): Intervals.icu server http://10.0.2.2:8800 from the
emulator, or http://<PC address>:8800 from a phone on the same network. Uploaded files are checked with
tools/fitcheck/fitcheck.py.
"""
import argparse
import datetime
import email.parser
import email.policy
import json
import os
import struct
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FIT_EPOCH = 631065600


def fit_start(b):
    """file_id time_created, found by a minimal walk (the unit writes plain headers only)."""
    hs = b[0]
    end = hs + struct.unpack_from("<I", b, 4)[0]
    defs = {}
    off = hs
    while off < end:
        h = b[off]
        if h & 0x80:
            return None
        local = h & 0x0F
        if h & 0x40:
            glob = struct.unpack_from("<H", b, off + 3)[0]
            nf = b[off + 5]
            fields = [(b[off + 6 + 3 * i], b[off + 7 + 3 * i]) for i in range(nf)]
            off += 6 + 3 * nf
            if h & 0x20:
                nd = b[off]
                off += 1 + 3 * nd
            defs[local] = (glob, fields)
            continue
        glob, fields = defs[local]
        o = off + 1
        for num, size in fields:
            if glob == 0 and num == 4 and size == 4:
                return struct.unpack_from("<I", b, o)[0] + FIT_EPOCH
            o += size
        off = o
    return None


class Handler(BaseHTTPRequestHandler):
    store = None
    fail = None
    activities = []

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.command, self.path.split("?")[0]))

    def reply(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if not self.path.startswith("/api/v1/athlete/0/activities"):
            return self.reply(404, {"error": "not found"})
        if self.fail == "401":
            return self.reply(401, {"error": "unauthorized"})
        self.reply(200, self.activities)

    def do_POST(self):
        if self.path != "/api/v1/athlete/0/activities":
            return self.reply(404, {"error": "not found"})
        if self.fail:
            return self.reply(int(self.fail), {"error": "mock failure %s" % self.fail})
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n)
        msg = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(
            b"Content-Type: " + self.headers["Content-Type"].encode() + b"\r\n\r\n" + raw)
        part = next((p for p in msg.iter_parts() if p.get_filename()), None)
        if part is None:
            return self.reply(422, {"error": "no file part"})
        data = part.get_payload(decode=True)
        start = fit_start(data)
        aid = "i%d" % (len(self.activities) + 1)
        path = os.path.join(self.store, "%s-%s" % (aid, part.get_filename()))
        with open(path, "wb") as f:
            f.write(data)
        when = datetime.datetime.fromtimestamp(start or 0, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        self.activities.append({"id": aid, "start_date": when, "name": part.get_filename()})
        sys.stderr.write("saved %s (%d bytes, starts %s)\n" % (path, len(data), when))
        self.reply(201, {"id": aid})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8800)
    ap.add_argument("--dir", default="/tmp/mock-intervals")
    ap.add_argument("--fail", choices=["401", "429", "503", "422"])
    a = ap.parse_args()
    os.makedirs(a.dir, exist_ok=True)
    Handler.store, Handler.fail = a.dir, a.fail
    srv = ThreadingHTTPServer(("0.0.0.0", a.port), Handler)
    sys.stderr.write("mock Intervals.icu on port %d, files in %s\n" % (srv.server_port, a.dir))
    srv.serve_forever()


if __name__ == "__main__":
    main()
