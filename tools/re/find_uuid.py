#!/usr/bin/env python3
"""find_uuid.py: look for 128-bit Bluetooth UUIDs in a binary, in both byte orders.

A BLE stack stores a 128-bit UUID as sixteen raw bytes, usually least significant byte first, so a
UUID seen in a phone app can be tied to a firmware image by finding those bytes in it.

Usage: find_uuid.py FILE UUID [UUID ...]      (files only; nothing is sent anywhere)
"""
import sys


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        blob = f.read()
    for u in sys.argv[2:]:
        raw = bytes.fromhex(u.replace("-", ""))
        if len(raw) != 16:
            sys.exit(f"not a 128-bit UUID: {u}")
        for name, pat in (("little-endian", raw[::-1]), ("big-endian", raw), ("text", u.encode())):
            at, hits = 0, []
            while (at := blob.find(pat, at)) != -1:
                hits.append(at)
                at += 1
            for h in hits:
                print(f"{u}  {name:<13} file offset 0x{h:06x}")
            if name == "text":
                up = u.upper().encode()
                at = 0
                while (at := blob.find(up, at)) != -1:
                    print(f"{u}  text (upper)  file offset 0x{at:06x}")
                    at += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
