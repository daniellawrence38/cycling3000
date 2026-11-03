"""Python side of the vectors: the reference itself must keep passing its anchors."""
import hashlib
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'reference'))
import ref  # noqa: E402


def test_crc32_anchor():
    assert ref.crc32(b'123456789') == 0xCBF43926


def test_sha256_anchor():
    assert hashlib.sha256(b'abc').hexdigest()[:16] == 'ba7816bf8f01cfea'


def test_floor_division_ramp():
    cd = dict(p0=550, p1=400, secs=600, ramp=True)
    assert [ref.ramp_p10(cd, e) for e in (150, 300, 450, 599)] == [513, 475, 438, 400]
