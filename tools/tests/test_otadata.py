"""tools/fw/otadata.py: the otadata the bring-up writes is computed as ESP-IDF's otatool does."""
import binascii
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fw"))
import otadata  # noqa: E402


def blank():
    return bytes([0xFF]) * otadata.OTADATA_SIZE


def entry(b, i, seq, state=0xFFFFFFFF, good_crc=True):
    b = bytearray(b)
    c = otadata.crc(seq) if good_crc else 0
    struct.pack_into("<I20sII", b, i * 0x1000, seq, b"\xff" * 20, state, c)
    return bytes(b)


def test_crc_is_crc32_of_seq_with_initial_ones():
    # esp_rom_crc32_le(UINT32_MAX, &seq, 4) == binascii.crc32(seq, 0xFFFFFFFF), as otatool uses
    assert otadata.crc(1) == binascii.crc32(struct.pack("<I", 1), 0xFFFFFFFF)


def test_erased_boots_ota0_and_switches_to_ota1():
    assert otadata.active_slot(blank()) is None
    new = otadata.switch(blank(), 1)
    assert otadata.active_slot(new) == 1
    assert otadata.entries(new)[0]["seq"] == 2


def test_from_ota0_running():
    stock = entry(blank(), 0, 1)                  # seq 1: ota_0
    assert otadata.active_slot(stock) == 0
    new = otadata.switch(stock, 1)
    e = otadata.entries(new)
    assert e[0]["seq"] == 1 and e[1]["seq"] == 2  # written in the other copy, stock copy kept
    assert otadata.active_slot(new) == 1
    back = otadata.switch(new, 0)
    assert otadata.active_slot(back) == 0 and otadata.entries(back)[0]["seq"] == 3


def test_higher_sequences_and_invalid_copies():
    s = entry(entry(blank(), 0, 7), 1, 8)         # 8 -> ota_1
    assert otadata.active_slot(s) == 1
    assert otadata.active_slot(otadata.switch(s, 0)) == 0
    bad = entry(entry(blank(), 0, 5), 1, 9, good_crc=False)
    assert otadata.active_slot(bad) == 0          # (5 - 1) % 2
    aborted = entry(entry(blank(), 0, 3), 1, 3, state=4)
    new = otadata.switch(aborted, 1)
    assert otadata.active_slot(new) == 1


def test_switch_command_refuses_wrong_size(tmp_path):
    f = tmp_path / "x.bin"
    f.write_bytes(b"\xff" * 10)
    try:
        otadata.main(["switch", str(f), "--to", "1", "--out", str(tmp_path / "y.bin")])
    except SystemExit as e:
        assert "expected" in str(e)
    else:
        raise AssertionError("accepted a short file")
