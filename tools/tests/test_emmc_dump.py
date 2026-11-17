"""tools/emmc_dump.py: the PC end of the eMMC read-out.

The golden record here is the one core/tests/test_dump_fmt.c builds with the firmware's own code, so
the two ends are held to the same line. The rest plays a small FAT32 volume through the collector the
way the unit sends it, and reads the files back out of the backup."""
import base64
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import emmc_dump  # noqa: E402

SECTOR = 512


def record(lba, data):
    crc = emmc_dump.record_crc(lba, data)
    return b"#CC7 D %08x %02x %08x %s\n" % (lba, len(data) // SECTOR, crc, base64.b64encode(data))


def test_golden_record_matches_the_firmware():
    sec = bytes((i * 7 + 3) & 0xFF for i in range(512))
    # The CRC the C test asserts for the same sector at the same address.
    assert emmc_dump.record_crc(0x00012345, sec) == 0xA6154B91
    line = record(0x00012345, sec)
    assert line.startswith(b"#CC7 D 00012345 01 a6154b91 AwoRGB8mLTQ7QklQ")
    assert emmc_dump.parse_line(line) == ("D", 0x00012345, sec)
    # The console turns LF into CR LF, and a log line can run into the start of a record.
    assert emmc_dump.parse_line(line.replace(b"\n", b"\r\n")) == ("D", 0x00012345, sec)
    assert emmc_dump.parse_line(b"I (123) nrf: something" + line) == ("D", 0x00012345, sec)


def test_damaged_records_are_refused():
    sec = bytes(512)
    line = record(7, sec)
    assert emmc_dump.parse_line(line) is not None
    # Any change to the address, the count, the checksum or the data.
    assert emmc_dump.parse_line(line.replace(b"00000007", b"00000008")) is None
    assert emmc_dump.parse_line(line.replace(b" 01 ", b" 02 ")) is None
    assert emmc_dump.parse_line(line.replace(b"AAAA", b"AAAB", 1)) is None
    assert emmc_dump.parse_line(line[:-40] + b"\n") is None   # cut short
    assert emmc_dump.parse_line(line[:60] + b"I (5) x: y" + line[60:]) is None  # a log line inside it
    assert emmc_dump.parse_line(b"#CC7 D zz\n") is None
    assert emmc_dump.parse_line(b"hello\n") is None


def dirent(name83, attr, cluster, size):
    return struct.pack("<11sB8xHHHHI", name83, attr, cluster >> 16, 0, 0, cluster & 0xFFFF, size)


def lfn_entries(name, short):
    """The long-name entries for one file, in the order they are stored (last part first)."""
    chk = 0
    for c in short:
        chk = (((chk & 1) << 7) + (chk >> 1) + c) & 0xFF
    units = name.encode("utf-16-le") + b"\x00\x00"
    units += b"\xff" * (-len(units) % 26)
    parts = [units[i:i + 26] for i in range(0, len(units), 26)]
    out = []
    for n in range(len(parts), 0, -1):
        p = parts[n - 1]
        seq = n | (0x40 if n == len(parts) else 0)
        out.append(struct.pack("<B10sBBB12sH4s", seq, p[0:10], 0x0F, 0, chk, p[10:22], 0, p[22:26]))
    return b"".join(out)


class Volume:
    """A tiny FAT32 volume: 4 KB clusters, a root with one file and one folder holding one file."""
    RESERVED, FAT_SECTORS, SPC, CLUSTERS = 32, 4, 8, 300

    def __init__(self):
        self.data_first = self.RESERVED + 2 * self.FAT_SECTORS
        self.sectors = {}
        fat = [0] * (self.FAT_SECTORS * 128)
        fat[0], fat[1] = 0x0FFFFFF8, 0x0FFFFFFF
        self.ride = bytes((i * 13) & 0xFF for i in range(5000))  # two clusters
        self.note = b"hello\n"
        root = dirent(b"NOTE    TXT", 0x20, 6, len(self.note)) + dirent(b"FITS       ", 0x10, 3, 0)
        fits = (dirent(b".          ", 0x10, 3, 0) + dirent(b"..         ", 0x10, 0, 0) +
                lfn_entries("ride one é.fit", b"RIDEON~1FIT") + dirent(b"RIDEON~1FIT", 0x20, 4, len(self.ride)))
        for cl, nxt, body in ((2, 0x0FFFFFFF, root), (3, 0x0FFFFFFF, fits), (4, 5, self.ride[:4096]),
                              (5, 0x0FFFFFFF, self.ride[4096:]), (6, 0x0FFFFFFF, self.note)):
            fat[cl] = nxt
            self.put_cluster(cl, body)
        fatb = struct.pack(f"<{len(fat)}I", *fat)
        boot = bytearray(512)
        boot[0:3] = b"\xeb\x58\x90"
        struct.pack_into("<HBHB", boot, 11, 512, self.SPC, self.RESERVED, 2)
        boot[510:512] = b"\x55\xaa"
        self.put(0, bytes(boot))
        for s in range(1, self.RESERVED):
            self.put(s, bytes(512))
        for copy in range(2):
            for s in range(self.FAT_SECTORS):
                self.put(self.RESERVED + copy * self.FAT_SECTORS + s, fatb[s * 512:(s + 1) * 512])
        self.used = [cl for cl in range(2, self.CLUSTERS + 2) if fat[cl]]

    def put(self, lba, sec):
        assert len(sec) == 512
        self.sectors[lba] = sec

    def put_cluster(self, cl, body):
        body = body + bytes(4096 - len(body))
        first = self.data_first + (cl - 2) * self.SPC
        for i in range(self.SPC):
            self.put(first + i, body[i * 512:(i + 1) * 512])

    def stream(self, cycle):
        """The lines the unit sends for one pass."""
        out = [b"#CC7 H cycle=%d card=TEST card_sectors=20000 volume_sectors=2440 reserved=%d fats=2 "
               b"fat_sectors=%d spc=%d data_first=%d clusters=%d root=2\n"
               % (cycle, self.RESERVED, self.FAT_SECTORS, self.SPC, self.data_first, self.CLUSTERS)]
        recs = []
        for s in range(0, self.data_first, 8):
            n = min(8, self.data_first - s)
            recs.append((s, b"".join(self.sectors[s + i] for i in range(n))))
        for cl in self.used:
            first = self.data_first + (cl - 2) * self.SPC
            recs.append((first, b"".join(self.sectors[first + i] for i in range(8))))
        x = 0
        for lba, data in recs:
            out.append(record(lba, data))
            x ^= emmc_dump.record_crc(lba, data)
        out.append(b"#CC7 E cycle=%d records=%d sectors=%d used_clusters=%d xor=%08x read_errors=0 dropped_lines=0\n"
                   % (cycle, len(recs), sum(len(d) // 512 for _, d in recs), len(self.used), x))
        return out


def test_two_agreeing_passes_make_a_complete_backup(tmp_path):
    vol = Volume()
    col = emmc_dump.Collector(str(tmp_path / "b"))
    for line in vol.stream(1):
        col.feed(line)
    ok, why = col.complete()
    assert not ok and "fewer than 2" in why          # one pass is not enough
    for line in vol.stream(2):
        col.feed(line)
    assert col.complete() == (True, "complete")
    index = col.finish()
    assert index["complete"] and not index["mismatched_lbas"]

    b = emmc_dump.Backup(str(tmp_path / "b"))
    found = {p: (d, s) for p, d, s, _ in b.walk()}
    assert found == {"/NOTE.TXT": (False, 6), "/FITS": (True, 0), "/FITS/ride one é.fit": (False, 5000)}
    first = {p: c for p, _, _, c in b.walk()}
    assert b.read_chain(first["/FITS/ride one é.fit"], 5000) == vol.ride
    assert b.read_chain(first["/NOTE.TXT"], 6) == vol.note

    assert emmc_dump.main(["list", str(tmp_path / "b")]) == 0
    assert emmc_dump.main(["extract", str(tmp_path / "b"), "--to", str(tmp_path / "x")]) == 0
    assert (tmp_path / "x" / "FITS" / "ride one é.fit").read_bytes() == vol.ride
    assert emmc_dump.main(["image", str(tmp_path / "b"), "--to", str(tmp_path / "v.img")]) == 0
    img = (tmp_path / "v.img").read_bytes()
    assert len(img) == 20000 * 512
    for lba, sec in vol.sectors.items():
        assert img[lba * 512:(lba + 1) * 512] == sec
    # An existing backup is never overwritten.
    with pytest.raises(SystemExit):
        emmc_dump.Collector(str(tmp_path / "b"))


def test_a_lost_record_and_a_differing_record_are_both_caught(tmp_path):
    vol = Volume()
    # A record missing from the first pass is picked up later, and the backup waits for it twice.
    col = emmc_dump.Collector(str(tmp_path / "lost"))
    one = vol.stream(1)
    for line in one[:10] + one[11:]:
        col.feed(line)
    assert not col.complete()[0]
    for line in vol.stream(2):
        col.feed(line)
    ok, why = col.complete()
    assert not ok and "1 record(s) seen fewer than 2" in why
    for line in vol.stream(3):
        col.feed(line)
    assert col.complete()[0]
    col.finish()

    # The same address arriving with different, validly checksummed contents is never "complete".
    col = emmc_dump.Collector(str(tmp_path / "diff"))
    for line in vol.stream(1):
        col.feed(line)
    two = vol.stream(2)
    lba = vol.data_first + (4 - 2) * vol.SPC
    two = [record(lba, bytes(4096)) if l.startswith(b"#CC7 D %08x" % lba) else l for l in two]
    for line in two:
        col.feed(line)
    ok, why = col.complete()
    assert not ok
    index = col.finish()
    assert not index["complete"]

    # A pass in which the unit reported a read error is not accepted either.
    col = emmc_dump.Collector(str(tmp_path / "err"))
    for cycle in (1, 2):
        lines = vol.stream(cycle)
        lines[-1] = lines[-1].replace(b"read_errors=0", b"read_errors=1")
        for line in lines:
            col.feed(line)
    assert not col.complete()[0]
    col.finish()


def subset_stream(vol, cycle):
    """What a FS_EMMC_MODE 4 pass sends for the folder /FITS: the reserved sectors, the root directory,
    the folder and its file, and the first FAT's sector 0. NOTE.TXT and the second FAT are not sent."""
    out = [b"#CC7 H cycle=%d subset=CC700 card=TEST card_sectors=20000 volume_sectors=2440 reserved=%d "
           b"fats=2 fat_sectors=%d spc=%d data_first=%d clusters=%d root=2\n"
           % (cycle, vol.RESERVED, vol.FAT_SECTORS, vol.SPC, vol.data_first, vol.CLUSTERS)]
    recs = []
    for s in range(0, vol.RESERVED, 8):
        recs.append((s, b"".join(vol.sectors[s + i] for i in range(8))))
    for cl in (2, 3, 4, 5):
        first = vol.data_first + (cl - 2) * vol.SPC
        recs.append((first, b"".join(vol.sectors[first + i] for i in range(8))))
    recs.append((vol.RESERVED, vol.sectors[vol.RESERVED]))
    x = 0
    for lba, data in recs:
        out.append(record(lba, data))
        x ^= emmc_dump.record_crc(lba, data)
    out.append(b"#CC7 E cycle=%d records=%d sectors=%d used_clusters=4 xor=%08x read_errors=0 dropped_lines=0\n"
               % (cycle, len(recs), sum(len(d) // 512 for _, d in recs), x))
    return out


def test_a_folder_only_read_out_is_listed_and_extracted_with_under(tmp_path):
    vol = Volume()
    col = emmc_dump.Collector(str(tmp_path / "b"))
    for cycle in (1, 2):
        for line in subset_stream(vol, cycle):
            col.feed(line)
    assert col.complete() == (True, "complete")
    col.finish()
    assert emmc_dump.main(["list", str(tmp_path / "b"), "--under", "/fits"]) == 0
    assert emmc_dump.main(["extract", str(tmp_path / "b"), "--under", "/FITS", "--to", str(tmp_path / "x")]) == 0
    assert (tmp_path / "x" / "FITS" / "ride one é.fit").read_bytes() == vol.ride
    assert not (tmp_path / "x" / "NOTE.TXT").exists()
    # The whole volume cannot be listed from it: NOTE.TXT's data was never sent.
    assert emmc_dump.main(["list", str(tmp_path / "b")]) == 1
    with pytest.raises(SystemExit):
        emmc_dump.main(["list", str(tmp_path / "b"), "--under", "/NOPE"])
