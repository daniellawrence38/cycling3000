"""tools/fw/backup_report.py on a synthetic image laid out like the stock flash."""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fw"))
import backup_report as br  # noqa: E402


def image():
    img = bytearray(b"\xff" * (16 * 1024 * 1024))
    img[0:4] = bytes([0xE9, 3, 2, 0x4F])
    img[0x100:0x110] = b"v5.1.2-dirty\0\0\0\0"
    parts = [("nvs", 1, 2, 0x9000, 16384), ("otadata", 1, 0, 0xD000, 8192), ("ota_0", 0, 0x10, 0x20000, 8192000),
             ("ota_1", 0, 0x11, 0x7F0000, 8192000)]
    for i, (n, t, s, o, z) in enumerate(parts):
        e = b"\xaa\x50" + struct.pack("<BBII", t, s, o, z) + n.encode().ljust(16, b"\0") + b"\0" * 4
        img[0x8000 + 32 * i:0x8000 + 32 * i + 32] = e
    a = 0x20000
    img[a] = 0xE9
    d = a + 32
    struct.pack_into("<I", img, d, br.APP_DESC_MAGIC)
    img[d + 16:d + 48] = b"CC700Pro_V1.710".ljust(32, b"\0")
    img[d + 112:d + 144] = b"423e6536".ljust(32, b"\0")
    img[0x30000:0x30020] = b"lcd panel st7789 init done\0\0\0\0\0\0"
    img[0x40000:0x40010] = b"wifi pass=secret"
    return bytes(img)


def test_report_contents():
    r = br.report(image())
    assert "DOES NOT MATCH" in r
    assert "flash_mode: DIO" in r
    assert "v5.1.2-dirty" in r
    assert "ota_1" in r and "completely erased (all 0xFF): True" in r
    assert "version: CC700Pro_V1.710" in r and "idf: 423e6536" in r
    assert "ST7789" in r and "st7789 init done" in r
    assert "secret" not in r
