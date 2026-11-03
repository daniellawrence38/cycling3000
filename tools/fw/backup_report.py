#!/usr/bin/env python3
"""Offline report on the 16 MB flash backup for BRINGUP.md step 3. Reads the file only; never talks to a
device. Prints facts that are safe to share (versions, partition table, whether ota_1 is empty, driver
and chip names) and nothing else: no NVS contents, no free text that could hold Wi-Fi credentials.

  python3 tools/fw/backup_report.py original-flash-16MB.bin > backup-report.txt
"""
import hashlib
import re
import struct
import sys

BACKUP_SHA256 = "8ec2dfd530d6dbf2f078d23114c589d88ac0fbd728b926da70c0526e5c7b3ec7"
APP_DESC_MAGIC = 0xABCD5432
# Names of parts and drivers that would identify hardware, matched case-insensitively.
TOKENS = [
    "ST7789", "ST7796", "ILI9341", "ILI9342", "GC9A01", "NV3030", "CST816", "CST820", "CST328", "FT6336", "FT6206",
    "FT5X06", "GT911", "CHSC6", "AXS15231", "BMP280", "BMP388", "BMP390", "BME280", "SPL06", "SPL07", "DPS310",
    "LPS22", "MS5611", "ICP-10", "QMP6988", "HP203", "UBLOX", "UBX-", "ATGM", "AT6558", "CASIC", "$PCAS",
    "ALLYSTAR", "$PMTK", "QUECTEL", "L76", "LC76", "AG3335", "UM620", "BQ24", "BQ25", "BQ27", "MAX1704",
    "CW2015", "CW2017", "SY6974", "IP5306", "TP4056", "AXP", "LTR-", "LTR3", "OPT3001", "BH1750", "VEML",
    "SDMMC", "SDSPI", "SD CARD", "EMMC", "TINYUSB", "MSC", "NRF52", "NRF5", "ANT+", "ANTPLUS", "LVGL",
]
SAFE = re.compile(rb"[A-Za-z0-9_ .:/()+\-%,=\[\]]{4,80}")


def cstr(b):
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def partitions(img):
    out = []
    for off in range(0x8000, 0x8C00, 32):
        e = img[off:off + 32]
        if e[:2] != b"\xaa\x50":
            break
        typ, sub, poff, size = struct.unpack_from("<BBII", e, 2)
        out.append((cstr(e[12:28]), typ, sub, poff, size))
    return out


def app_desc(img, off):
    if img[off] != 0xE9:
        return None
    # image header (24 bytes) + first segment header (8 bytes), then esp_app_desc_t
    d = off + 32
    if struct.unpack_from("<I", img, d)[0] != APP_DESC_MAGIC:
        return None
    return {"version": cstr(img[d + 16:d + 48]), "project": cstr(img[d + 48:d + 80]), "time": cstr(img[d + 80:d + 96]),
            "date": cstr(img[d + 96:d + 112]), "idf": cstr(img[d + 112:d + 144])}


def bootloader(img):
    info = {"magic_ok": img[0] == 0xE9}
    if info["magic_ok"]:
        info["segments"] = img[1]
        info["flash_mode"] = {0: "QIO", 1: "QOUT", 2: "DIO", 3: "DOUT"}.get(img[2], img[2])
        info["flash_size_freq"] = "0x%02x" % img[3]
        info["chip_id"] = struct.unpack_from("<H", img, 12)[0]
        info["min_chip_rev"] = img[14]
    # esp_bootloader_desc_t (IDF 5.2 and later): magic 0x50 then version and idf_ver
    region = img[0:0x8000]
    m = re.search(rb"\x50..(....)(v\d+\.\d+[^\0]{0,28})\0", region, re.S)
    info["bootloader_desc_idf"] = m.group(2).decode("ascii", "replace") if m else None
    info["version_strings"] = sorted({s.decode() for s in re.findall(rb"v\d+\.\d+(?:\.\d+)?(?:-[A-Za-z0-9.\-]+)?", region)})[:10]
    return info


def report(img):
    lines = []
    digest = hashlib.sha256(img).hexdigest()
    lines.append("size %d bytes, SHA-256 %s (%s)" % (len(img), digest, "matches the handover" if digest == BACKUP_SHA256 else "DOES NOT MATCH"))
    lines.append("\n[bootloader at 0x0]")
    for k, v in bootloader(img).items():
        lines.append("  %s: %s" % (k, v))
    lines.append("\n[partition table at 0x8000]")
    parts = partitions(img)
    for name, typ, sub, off, size in parts:
        lines.append("  %-10s type %d subtype 0x%02x offset 0x%06x size %d" % (name, typ, sub, off, size))
    for name, typ, sub, off, size in parts:
        if typ == 0:
            d = app_desc(img, off)
            region = img[off:off + size]
            blank = region.count(0xFF) == len(region)
            lines.append("\n[%s]" % name)
            lines.append("  completely erased (all 0xFF): %s" % blank)
            if d:
                for k, v in d.items():
                    lines.append("  %s: %s" % (k, v))
        if name == "otadata":
            od = img[off:off + size]
            for i in range(2):
                seq, state, c = struct.unpack_from("<I", od, i * 0x1000)[0], struct.unpack_from("<I", od, i * 0x1000 + 24)[0], \
                    struct.unpack_from("<I", od, i * 0x1000 + 28)[0]
                lines.append("  otadata copy %d: seq %#010x state %#010x crc %#010x" % (i, seq, state, c))
    lines.append("\n[hardware names in the firmware: token, count, first contexts]")
    up = img.upper()
    for t in TOKENS:
        tb = t.encode()
        n = up.count(tb)
        if not n:
            continue
        ctx = []
        for m in re.finditer(re.escape(tb), up):
            s = max(0, m.start() - 40)
            for frag in SAFE.findall(img[s:m.end() + 40]):
                if tb in frag.upper():
                    ctx.append(frag.decode("ascii").strip())
                    break
            if len(ctx) >= 3:
                break
        lines.append("  %-10s %4d  %s" % (t, n, " | ".join(ctx)))
    return "\n".join(lines)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    print(report(open(sys.argv[1], "rb").read()))
