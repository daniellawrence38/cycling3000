#!/usr/bin/env python3
"""Copy the unit's eMMC to this PC, check the copy, and read files out of it.

The firmware built with FS_EMMC_MODE 3 (fw/main/emmc_dump_esp.c) sends every sector of the storage
that holds anything, as checksummed text records, again and again. This tool listens, keeps each
record whose checksum is right, and stops when every record has been received at least twice with
the same contents and the count and combined checksum match what the unit says it sent.

It only LISTENS on the serial port: nothing is written to it, and DTR and RTS are held off before the
port is opened so the unit is not reset (see tools/capture_serial.py for why that matters here).

    python tools/emmc_dump.py capture COM3 --out D:\\cc700-toolchain\\emmc-backup
    python tools/emmc_dump.py list    D:\\cc700-toolchain\\emmc-backup
    python tools/emmc_dump.py extract D:\\cc700-toolchain\\emmc-backup --to D:\\cc700-toolchain\\emmc-files
    python tools/emmc_dump.py image   D:\\cc700-toolchain\\emmc-backup --to D:\\emmc.img

A FS_EMMC_MODE 4 build (`make fw-export`) sends only the /CC700 folder; read that with
`list --under /CC700` and `extract --under /CC700`.

The backup is two files: `emmc-used.bin` (the sectors, in the order they first arrived) and
`emmc-index.json` (where each one belongs, its checksum, and the volume's geometry). `image` turns
them back into a full raw image of the storage, with zeros where the storage held no file data.
"""
import argparse
import base64
import hashlib
import json
import os
import struct
import sys
import time
import zlib

SECTOR = 512
PREFIX = b"#CC7 "


# ------------------------------------------------------------------ the record format
def record_crc(lba, data):
    """The CRC a record carries: zlib's CRC-32 over the LBA (4 bytes, little-endian) then the data."""
    return zlib.crc32(data, zlib.crc32(struct.pack("<I", lba))) & 0xFFFFFFFF


def parse_line(line):
    """One line from the unit. Returns ('D', lba, data), ('H', dict), ('E', dict), ('X', text),
    or None for anything that is not a record or fails its checksum."""
    line = line.strip(b"\r\n")
    at = line.find(PREFIX)
    if at < 0:
        return None
    parts = line[at + len(PREFIX):].split(b" ")
    kind = parts[0]
    try:
        if kind == b"D":
            if len(parts) != 5:
                return None
            lba, nsec, crc = int(parts[1], 16), int(parts[2], 16), int(parts[3], 16)
            data = base64.b64decode(parts[4], validate=True)
            if nsec == 0 or len(data) != nsec * SECTOR or record_crc(lba, data) != crc:
                return None
            return ("D", lba, data)
        if kind in (b"H", b"E"):
            fields = {}
            for p in parts[1:]:
                k, _, v = p.partition(b"=")
                fields[k.decode()] = v.decode()
            return (kind.decode(), fields)
        if kind == b"X":
            return ("X", b" ".join(parts[1:]).decode("ascii", "replace"))
    except (ValueError, UnicodeDecodeError):
        return None
    return None


# ------------------------------------------------------------------ collecting
class Collector:
    """Keeps the first good copy of each record and counts how often later copies agree."""

    def __init__(self, out_dir):
        os.makedirs(out_dir, exist_ok=True)
        self.out_dir = out_dir
        self.data_path = os.path.join(out_dir, "emmc-used.bin")
        self.index_path = os.path.join(out_dir, "emmc-index.json")
        if os.path.exists(self.data_path) or os.path.exists(self.index_path):
            raise SystemExit(f"{out_dir} already holds a backup; choose an empty folder so it is not overwritten")
        self.f = open(self.data_path, "wb")
        self.offset = 0
        self.rec = {}       # lba -> [nsec, crc, offset, times seen]
        self.mismatch = {}  # lba -> set of other crcs seen
        self.header = None
        self.notes = []
        self.bad_lines = 0
        self.cycles_done = []

    def feed(self, line):
        r = parse_line(line)
        if r is None:
            if PREFIX in line:
                self.bad_lines += 1
            return None
        if r[0] == "D":
            _, lba, data = r
            crc = record_crc(lba, data)
            have = self.rec.get(lba)
            if have is None:
                self.f.write(data)
                self.rec[lba] = [len(data) // SECTOR, crc, self.offset, 1]
                self.offset += len(data)
            elif have[1] == crc and have[0] == len(data) // SECTOR:
                have[3] += 1
            else:
                self.mismatch.setdefault(lba, set()).add(crc)
        elif r[0] == "H":
            if self.header is not None and self.geometry(r[1]) != self.geometry(self.header):
                self.notes.append("the volume's geometry changed between passes")
            self.header = r[1]
        elif r[0] == "X":
            self.notes.append(r[1])
        elif r[0] == "E":
            self.cycles_done.append(r[1])
        return r

    @staticmethod
    def geometry(h):
        return {k: v for k, v in h.items() if k != "cycle"}

    def complete(self, need=2):
        """True when the last finished pass is fully accounted for and every record was seen `need` times."""
        if not self.cycles_done or self.header is None:
            return False, "waiting for a whole pass"
        e = self.cycles_done[-1]
        if int(e.get("read_errors", "0")) != 0:
            return False, f"the unit reported {e['read_errors']} read error(s) in that pass"
        if len(self.rec) != int(e["records"]):
            return False, f"{len(self.rec)} of {e['records']} records so far"
        x = 0
        sectors = 0
        for nsec, crc, _, _ in self.rec.values():
            x ^= crc
            sectors += nsec
        if x != int(e["xor"], 16) or sectors != int(e["sectors"]):
            return False, "the records held do not add up to what the unit sent"
        if self.mismatch:
            return False, f"{len(self.mismatch)} record(s) differed between passes"
        short = sum(1 for r in self.rec.values() if r[3] < need)
        if short:
            return False, f"{short} record(s) seen fewer than {need} times"
        return True, "complete"

    def finish(self):
        self.f.flush()
        os.fsync(self.f.fileno())
        self.f.close()
        h = hashlib.sha256()
        with open(self.data_path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                h.update(block)
        ok, why = self.complete()
        index = {
            "format": 1,
            "complete": ok,
            "status": why,
            "geometry": self.geometry(self.header) if self.header else None,
            "last_pass": self.cycles_done[-1] if self.cycles_done else None,
            "data_file": "emmc-used.bin",
            "data_sha256": h.hexdigest(),
            "data_bytes": self.offset,
            "notes": sorted(set(self.notes)),
            "mismatched_lbas": sorted(self.mismatch),
            "records": sorted([lba, r[0], r[1], r[2], r[3]] for lba, r in self.rec.items()),
        }
        with open(self.index_path, "w") as f:
            json.dump(index, f)
        return index


def capture(args):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is not installed in this interpreter: python -m pip install pyserial")
    col = Collector(args.out)
    p = serial.Serial()
    p.port = args.port
    p.baudrate = 115200
    p.timeout = 0.5
    p.dtr = False  # before opening: never pull the unit's reset or boot lines
    p.rts = False
    p.open()
    print(f"listening on {args.port}; the backup goes to {args.out}")
    print("leave the unit plugged in and alone. This stops by itself when two passes agree.")
    pending = b""
    start = last_print = time.time()
    last_data = time.time()
    done = False
    try:
        while not done:
            chunk = p.read(65536)
            now = time.time()
            if chunk:
                last_data = now
                pending += chunk
                lines = pending.split(b"\n")
                pending = lines.pop()
                for line in lines:
                    r = col.feed(line)
                    if r and r[0] == "E":
                        ok, why = col.complete()
                        print(f"\npass {r[1].get('cycle')} finished: {why}")
                        done = ok
                    elif r and r[0] == "X":
                        print(f"\nunit says: {r[1]}")
            elif now - last_data > 30:
                print("\nnothing from the unit for 30 s. Is it running the read-out build, and is another "
                      "program holding the port?")
                last_data = now
            if now - last_print >= 2:
                last_print = now
                mb = col.offset / 1e6
                total = ""
                if col.cycles_done:
                    total = f" of {int(col.cycles_done[-1]['sectors']) * SECTOR / 1e6:.0f}"
                twice = sum(1 for r in col.rec.values() if r[3] >= 2)
                print(f"\r{mb:8.1f}{total} MB new, {len(col.rec)} records, {twice} confirmed twice, "
                      f"{col.bad_lines} bad lines, {mb / max(now - start, 1):.2f} MB/s   ", end="", flush=True)
            if args.max_minutes and now - start > args.max_minutes * 60:
                print(f"\nstopping after {args.max_minutes} minutes")
                break
    except KeyboardInterrupt:
        print("\nstopped by Ctrl+C")
    finally:
        p.close()
    index = col.finish()
    print(f"\n{index['data_bytes'] / 1e6:.1f} MB in {len(index['records'])} records")
    print(f"sha256 of emmc-used.bin: {index['data_sha256']}")
    if index["notes"]:
        print("notes from the unit:", "; ".join(index["notes"]))
    if index["complete"]:
        print("COMPLETE: every record arrived at least twice with identical contents, and the count and")
        print("combined checksum match what the unit sent.")
        return 0
    print(f"NOT COMPLETE: {index['status']}. Run it again into a new, empty folder.")
    return 1


# ------------------------------------------------------------------ reading the backup
class Backup:
    """A finished backup, read as a FAT32 volume."""

    def __init__(self, folder):
        with open(os.path.join(folder, "emmc-index.json")) as f:
            self.index = json.load(f)
        self.f = open(os.path.join(folder, self.index["data_file"]), "rb")
        self.where = {}  # sector -> offset in the data file
        for lba, nsec, _crc, off, _seen in self.index["records"]:
            for i in range(nsec):
                self.where[lba + i] = off + i * SECTOR
        g = self.index["geometry"]
        self.reserved = int(g["reserved"])
        self.spc = int(g["spc"])
        self.data_first = int(g["data_first"])
        self.clusters = int(g["clusters"])
        self.root = int(g["root"])
        self.volume_sectors = int(g["volume_sectors"])
        self.card_sectors = int(g["card_sectors"])

    def sector(self, lba):
        off = self.where.get(lba)
        if off is None:
            return None
        self.f.seek(off)
        return self.f.read(SECTOR)

    def fat(self, cluster):
        s = self.sector(self.reserved + cluster * 4 // SECTOR)
        if s is None:
            raise ValueError(f"the FAT sector for cluster {cluster} is not in the backup")
        return struct.unpack_from("<I", s, cluster * 4 % SECTOR)[0] & 0x0FFFFFFF

    def chain(self, cluster):
        seen = set()
        while 2 <= cluster <= self.clusters + 1 and cluster < 0x0FFFFFF7:
            if cluster in seen:
                raise ValueError(f"cluster chain loops at {cluster}")
            seen.add(cluster)
            yield cluster
            cluster = self.fat(cluster)

    def cluster_bytes(self, cluster):
        first = self.data_first + (cluster - 2) * self.spc
        out = bytearray()
        for i in range(self.spc):
            s = self.sector(first + i)
            if s is None:
                raise ValueError(f"cluster {cluster} (sector {first + i}) is not in the backup")
            out += s
        return bytes(out)

    def read_chain(self, cluster, size=None):
        out = bytearray()
        for c in self.chain(cluster):
            out += self.cluster_bytes(c)
            if size is not None and len(out) >= size:
                break
        return bytes(out if size is None else out[:size])

    def listdir(self, cluster):
        """Yields (name, is_dir, size, first cluster) for a directory, with long names decoded."""
        raw = self.read_chain(cluster)
        lfn = []
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0x00:
                break
            if e[0] == 0xE5:
                lfn = []
                continue
            attr = e[11]
            if attr & 0x0F == 0x0F:
                lfn.append(e)
                continue
            if attr & 0x08:
                lfn = []
                continue
            short = e[0:8].decode("latin-1").rstrip()
            ext = e[8:11].decode("latin-1").rstrip()
            name = short + ("." + ext if ext else "")
            if lfn:
                units = b""
                for part in reversed(lfn):
                    units += part[1:11] + part[14:26] + part[28:32]
                text = units.decode("utf-16-le", "replace")
                name = text.split("\x00")[0] or name
            lfn = []
            if name in (".", ".."):
                continue
            first = (struct.unpack_from("<H", e, 20)[0] << 16) | struct.unpack_from("<H", e, 26)[0]
            yield name, bool(attr & 0x10), struct.unpack_from("<I", e, 28)[0], first

    def find(self, path):
        """The first cluster of a folder given as /A/B, looked up from the root (case-insensitive)."""
        cluster = self.root
        for part in [p for p in path.split("/") if p]:
            for name, is_dir, _size, first in self.listdir(cluster):
                if is_dir and name.upper() == part.upper():
                    cluster = first
                    break
            else:
                raise SystemExit(f"{path}: no such folder in the backup")
        return cluster

    def walk(self, cluster=None, path=""):
        """Yields (path, is_dir, size, first cluster) for everything under a directory."""
        for name, is_dir, size, first in self.listdir(self.root if cluster is None else cluster):
            full = f"{path}/{name}"
            yield full, is_dir, size, first
            if is_dir and first >= 2:
                yield from self.walk(first, full)


def walk_from(b, under):
    if not under or under.strip("/") == "":
        return b.walk()
    return b.walk(b.find(under), "/" + under.strip("/"))


def cmd_list(args):
    b = Backup(args.folder)
    files = dirs = total = 0
    problems = []
    for path, is_dir, size, first in walk_from(b, args.under):
        if is_dir:
            dirs += 1
            print(f"{'<DIR>':>12}  {path}")
            continue
        files += 1
        total += size
        try:
            if size:
                have = sum(1 for _ in b.chain(first)) * b.spc * SECTOR
                if have < size:
                    problems.append(f"{path}: its cluster chain holds {have} bytes, the directory says {size}")
                b.read_chain(first, size)
        except ValueError as e:
            problems.append(f"{path}: {e}")
        print(f"{size:>12}  {path}")
    print(f"\n{files} files in {dirs} folders, {total / 1e6:.1f} MB of file data")
    print(f"backup marked complete: {b.index['complete']} ({b.index['status']})")
    if problems:
        print(f"{len(problems)} PROBLEM(S):")
        for p in problems:
            print("  " + p)
        return 1
    print("every file's data is present in the backup")
    return 0


def cmd_extract(args):
    b = Backup(args.folder)
    if os.path.exists(args.to) and os.listdir(args.to):
        sys.exit(f"{args.to} is not empty")
    n = 0
    for path, is_dir, size, first in walk_from(b, args.under):
        safe = [p for p in path.split("/") if p and p not in (".", "..")]
        safe = ["".join(c if c not in '<>:"\\|?*' and ord(c) >= 32 else "_" for c in p) for p in safe]
        target = os.path.join(args.to, *safe)
        if is_dir:
            os.makedirs(target, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as f:
            f.write(b.read_chain(first, size) if size else b"")
        n += 1
    print(f"{n} files written under {args.to}")
    return 0


def cmd_image(args):
    b = Backup(args.folder)
    if os.path.exists(args.to):
        sys.exit(f"{args.to} exists")
    size = b.card_sectors * SECTOR
    print(f"writing a {size / 1e9:.2f} GB raw image (zeros where the storage held no file data)")
    with open(args.to, "wb") as out:
        out.truncate(size)
        for lba in sorted(b.where):
            out.seek(lba * SECTOR)
            out.write(b.sector(lba))
    print("done")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture", help="listen to the unit and write a backup")
    c.add_argument("port")
    c.add_argument("--out", required=True, help="an empty folder for the backup")
    c.add_argument("--max-minutes", type=float, default=0, help="give up after this long (0: never)")
    c.set_defaults(fn=capture)
    for name, fn, need_to in (("list", cmd_list, False), ("extract", cmd_extract, True), ("image", cmd_image, True)):
        s = sub.add_parser(name)
        s.add_argument("folder")
        if need_to:
            s.add_argument("--to", required=True)
        if name != "image":
            s.add_argument("--under", default="",
                           help="only this folder, e.g. /CC700 (needed for a FS_EMMC_MODE 4 read-out)")
        s.set_defaults(fn=fn)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
