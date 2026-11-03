#!/usr/bin/env python3
"""Offline otadata tool for BRINGUP.md step 4. It never talks to a device: it reads and writes files.

  python3 tools/fw/otadata.py extract BACKUP.bin --out otadata-stock.bin
      Copies the 8 KB otadata region (0xd000) out of the 16 MB flash backup, after checking the backup's
      SHA-256 against the handover.
  python3 tools/fw/otadata.py inspect FILE
      Shows both entries and which OTA slot the bootloader would start.
  python3 tools/fw/otadata.py switch FILE --to 1 --out otadata-ota1.bin
      Makes the otadata that starts ota_1 (or 0), computed exactly as ESP-IDF v5.5.6
      components/app_update/otatool.py does: next sequence number in the other copy, CRC-32 of it.

Layout (ESP-IDF esp_ota_select_entry_t): two copies, at 0x0 and 0x1000, each
ota_seq u32, seq_label[20], ota_state u32, crc u32 = crc32(ota_seq, init 0xFFFFFFFF).
The bootloader takes the valid copy with the higher sequence and starts slot (seq - 1) % 2.
"""
import argparse
import binascii
import hashlib
import struct
import sys

OTADATA_OFFSET = 0xD000
OTADATA_SIZE = 0x2000
COPY = 0x1000
SLOTS = 2
BACKUP_SHA256 = "8ec2dfd530d6dbf2f078d23114c589d88ac0fbd728b926da70c0526e5c7b3ec7"
STATES = {0: "NEW", 1: "PENDING_VERIFY", 2: "VALID", 3: "INVALID", 4: "ABORTED", 0xFFFFFFFF: "UNDEFINED"}


def crc(seq):
    return binascii.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF


def entries(b):
    out = []
    for i in range(2):
        seq, label, state, c = struct.unpack_from("<I20sII", b, i * COPY)
        valid = seq != 0xFFFFFFFF and state not in (3, 4) and c == crc(seq)
        out.append({"seq": seq, "state": state, "crc": c, "valid": valid})
    return out


def active_slot(b):
    e = entries(b)
    valid = [x for x in e if x["valid"]]
    if not valid:
        return None  # the bootloader starts the factory app, or ota_0 if there is none
    return (max(x["seq"] for x in valid) - 1) % SLOTS


def switch(b, to):
    """Returns new otadata that starts slot `to`, the way otatool.switch_ota_partition does."""
    b = bytearray(b)
    e = entries(b)
    if e[0]["valid"] and e[1]["valid"]:
        base = 0 if e[0]["seq"] >= e[1]["seq"] else 1
    elif e[0]["valid"]:
        base = 0
    elif e[1]["valid"]:
        base = 1
    else:
        base = -1
    target = to + 1
    if base >= 0:
        i = 0
        while e[base]["seq"] > target % SLOTS + i * SLOTS:
            i += 1
        nxt = target % SLOTS + i * SLOTS
    else:
        nxt = target
    start = (1 if base == 0 else 0) * COPY
    struct.pack_into("<I", b, start, nxt)
    struct.pack_into("<I", b, start + 28, crc(nxt))
    st = struct.unpack_from("<I", b, start + 24)[0]
    if st in (3, 4):  # INVALID or ABORTED would make the bootloader ignore this copy
        struct.pack_into("<I", b, start + 24, 0xFFFFFFFF)
    return bytes(b)


def describe(b):
    lines = []
    for i, x in enumerate(entries(b)):
        lines.append("copy %d at +0x%04x: seq %#010x state %s crc %#010x %s" % (
            i, i * COPY, x["seq"], STATES.get(x["state"], hex(x["state"])), x["crc"], "valid" if x["valid"] else "not valid"))
    s = active_slot(b)
    lines.append("the bootloader would start: %s" % ("ota_%d" % s if s is not None else "ota_0 (no valid entry)"))
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    x = sub.add_parser("extract"); x.add_argument("backup"); x.add_argument("--out", required=True)
    i = sub.add_parser("inspect"); i.add_argument("file")
    s = sub.add_parser("switch"); s.add_argument("file"); s.add_argument("--to", type=int, choices=[0, 1], required=True)
    s.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "extract":
        data = open(a.backup, "rb").read()
        digest = hashlib.sha256(data).hexdigest()
        if digest != BACKUP_SHA256:
            sys.exit("backup SHA-256 %s does not match the handover's %s; stop" % (digest, BACKUP_SHA256))
        part = data[OTADATA_OFFSET:OTADATA_OFFSET + OTADATA_SIZE]
        open(a.out, "wb").write(part)
        print("wrote %s (%d bytes from 0x%x)\n%s" % (a.out, len(part), OTADATA_OFFSET, describe(part)))
    elif a.cmd == "inspect":
        print(describe(open(a.file, "rb").read()))
    else:
        old = open(a.file, "rb").read()
        if len(old) != OTADATA_SIZE:
            sys.exit("expected %d bytes" % OTADATA_SIZE)
        new = switch(old, a.to)
        open(a.out, "wb").write(new)
        print("before:\n%s\nafter:\n%s" % (describe(old), describe(new)))
        if active_slot(new) != a.to:
            sys.exit("internal error: the result does not start ota_%d" % a.to)


if __name__ == "__main__":
    main()
