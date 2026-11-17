#!/usr/bin/env python3
"""esp_image_to_elf.py: turn an ESP32-S3 application image inside a flash backup into an ELF file.

Why. A disassembler or decompiler (Ghidra, objdump) wants an ELF: it gives every byte its virtual
address and tells the tool the machine is Xtensa. The flash backup holds the vendor's application in
Espressif's own image format instead, a list of (load address, length, bytes) segments. This script
copies those segments, unchanged, into ELF sections and program headers at their load addresses. It
adds nothing and interprets nothing; `--verify` re-reads the ELF and checks every byte against the
image.

It reads a FILE (outputs/esp32s3-backup/original-flash-16MB.bin). It never talks to the unit.

Optionally, `--rom-ld DIR` names ESP-IDF's components/esp_rom/esp32s3/ld directory; every
`name = 0x4...;` there becomes an absolute ELF symbol, so calls into the chip's mask ROM (memcpy,
ets_printf, ...) show up by name. The mask ROM is part of the chip, so those addresses hold for any
firmware.

Usage:
  esp_image_to_elf.py FLASH.bin --offset 0x20000 --out stock-ota0.elf [--rom-ld DIR] [--verify]
"""
import argparse
import glob
import hashlib
import os
import re
import struct
import sys

EM_XTENSA = 94
PF_X, PF_W, PF_R = 1, 2, 4
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 1, 2, 4

# ESP32-S3 address map (ESP-IDF soc/esp32s3 memory layout): name, low, high, program flags.
REGIONS = [
    ("drom", 0x3C000000, 0x3E000000, PF_R),                # flash, data (rodata)
    ("dram", 0x3FC88000, 0x3FD00000, PF_R | PF_W),         # internal data RAM
    ("iram", 0x40370000, 0x403E0000, PF_R | PF_W | PF_X),  # internal instruction RAM
    ("irom", 0x42000000, 0x44000000, PF_R | PF_X),         # flash, code
    ("rtc", 0x50000000, 0x50002000, PF_R | PF_W),
    ("rtc_fast", 0x600FE000, 0x60100000, PF_R | PF_W | PF_X),
]


def region_of(addr):
    for name, lo, hi, flags in REGIONS:
        if lo <= addr < hi:
            return name, flags
    return "unknown", PF_R


def parse_image(blob, offset):
    """Returns (entry, [(load_addr, data, file_offset_of_data)], header_info)."""
    if blob[offset] != 0xE9:
        raise SystemExit(f"no ESP image magic 0xE9 at 0x{offset:x} (found 0x{blob[offset]:02x})")
    magic, nseg, spi_mode, spi_cfg, entry = struct.unpack_from("<BBBBI", blob, offset)
    chip_id = struct.unpack_from("<H", blob, offset + 12)[0]
    hash_appended = blob[offset + 23]
    if not 1 <= nseg <= 16:
        raise SystemExit(f"implausible segment count {nseg}")
    pos = offset + 24
    segs = []
    checksum = 0xEF
    for _ in range(nseg):
        addr, size = struct.unpack_from("<II", blob, pos)
        pos += 8
        if size == 0 or size > 16 * 1024 * 1024 or pos + size > len(blob):
            raise SystemExit(f"implausible segment at 0x{pos - 8:x}: addr 0x{addr:08x} size {size}")
        data = blob[pos:pos + size]
        for b in data:
            checksum ^= b
        segs.append((addr, data, pos))
        pos += size
    # The checksum byte is the last byte of the 16-byte-aligned block after the segments.
    pos = (pos + 16) & ~15
    stored = blob[pos - 1]
    info = {
        "segments": nseg, "entry": entry, "chip_id": chip_id, "spi_mode": spi_mode,
        "checksum_ok": stored == checksum, "image_end": pos, "hash_appended": hash_appended,
    }
    if hash_appended:
        info["sha256_ok"] = hashlib.sha256(blob[offset:pos]).digest() == blob[pos:pos + 32]
        info["image_end"] = pos + 32
    return entry, segs, info


def rom_symbols(ld_dir):
    out = {}
    pat = re.compile(r"^\s*(?:PROVIDE\s*\(\s*)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9a-fA-F]+)\s*\)?\s*;")
    for path in sorted(glob.glob(os.path.join(ld_dir, "*.ld"))):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = pat.match(line)
                if m:
                    addr = int(m.group(2), 16)
                    if 0x3FF00000 <= addr < 0x40070000:  # the mask ROM's data and code
                        out.setdefault(m.group(1), addr)
    return out


def build_elf(entry, segs, symbols):
    names = {}
    sections = []  # (name, addr, data, shflags, pflags)
    for addr, data, _ in segs:
        region, pflags = region_of(addr)
        n = names.get(region, 0)
        names[region] = n + 1
        name = f".{region}{n}"
        sh = SHF_ALLOC | (SHF_WRITE if pflags & PF_W else 0) | (SHF_EXECINSTR if pflags & PF_X else 0)
        sections.append((name, addr, data, sh, pflags))

    ehsize, phentsize, shentsize = 52, 32, 40
    phoff = ehsize
    data_off = phoff + phentsize * len(sections)
    offsets = []
    pos = data_off
    for _, _, data, _, _ in sections:
        pos = (pos + 3) & ~3
        offsets.append(pos)
        pos += len(data)

    # String and symbol tables.
    strtab = bytearray(b"\0")
    symtab = bytearray(struct.pack("<IIIBBH", 0, 0, 0, 0, 0, 0))
    for name, addr in sorted(symbols.items(), key=lambda kv: kv[1]):
        st_name = len(strtab)
        strtab += name.encode() + b"\0"
        is_code = 0x40000000 <= addr < 0x40070000
        info = (1 << 4) | (2 if is_code else 1)  # GLOBAL, FUNC or OBJECT
        symtab += struct.pack("<IIIBBH", st_name, addr, 0, info, 0, 0xFFF1)  # SHN_ABS
    shstr = bytearray(b"\0")

    def sname(s):
        off = len(shstr)
        shstr.extend(s.encode() + b"\0")
        return off

    pos = (pos + 3) & ~3
    symtab_off = pos
    pos += len(symtab)
    strtab_off = pos
    pos += len(strtab)
    sh_rows = [struct.pack("<10I", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    for (name, addr, data, sh, _), off in zip(sections, offsets):
        sh_rows.append(struct.pack("<10I", sname(name), 1, sh, addr, off, len(data), 0, 0, 4, 0))
    strtab_idx = len(sh_rows) + 1
    sh_rows.append(struct.pack("<10I", sname(".symtab"), 2, 0, 0, symtab_off, len(symtab), strtab_idx, 1, 4, 16))
    sh_rows.append(struct.pack("<10I", sname(".strtab"), 3, 0, 0, strtab_off, len(strtab), 0, 0, 1, 0))
    shstr_idx = len(sh_rows)
    shstr_name = sname(".shstrtab")
    shstr_off = pos
    sh_rows.append(struct.pack("<10I", shstr_name, 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0))
    pos += len(shstr)
    shoff = (pos + 3) & ~3

    ident = b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\0" * 8  # 32-bit, little-endian
    eh = ident + struct.pack("<HHIIIIIHHHHHH", 2, EM_XTENSA, 1, entry, phoff, shoff, 0x300,
                             ehsize, phentsize, len(sections), shentsize, len(sh_rows), shstr_idx)
    out = bytearray(eh)
    for (_, addr, data, _, pflags), off in zip(sections, offsets):
        out += struct.pack("<8I", 1, off, addr, addr, len(data), len(data), pflags, 4)
    for (_, _, data, _, _), off in zip(sections, offsets):
        out += b"\0" * (off - len(out))
        out += data
    out += b"\0" * (symtab_off - len(out))
    out += symtab + strtab + shstr
    out += b"\0" * (shoff - len(out))
    for row in sh_rows:
        out += row
    return bytes(out), [(n, a, len(d), off) for (n, a, d, _, _), off in zip(sections, offsets)]


def verify(elf, segs):
    """Independent re-read of the ELF's program headers; every byte must equal the image's."""
    phoff, = struct.unpack_from("<I", elf, 28)
    phnum, = struct.unpack_from("<H", elf, 44)
    if phnum != len(segs):
        return False
    for i, (addr, data, _) in enumerate(segs):
        p_type, p_off, p_vaddr, _, p_filesz, p_memsz, _, _ = struct.unpack_from("<8I", elf, phoff + 32 * i)
        if (p_type, p_vaddr, p_filesz, p_memsz) != (1, addr, len(data), len(data)):
            return False
        if elf[p_off:p_off + p_filesz] != data:
            return False
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("flash")
    ap.add_argument("--offset", type=lambda s: int(s, 0), default=0x20000,
                    help="flash offset of the application image (ota_0 is 0x20000)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rom-ld", help="ESP-IDF components/esp_rom/esp32s3/ld, for mask ROM symbol names")
    ap.add_argument("--verify", action="store_true")
    a = ap.parse_args()

    with open(a.flash, "rb") as f:
        blob = f.read()
    entry, segs, info = parse_image(blob, a.offset)
    symbols = rom_symbols(a.rom_ld) if a.rom_ld else {}
    elf, layout = build_elf(entry, segs, symbols)
    with open(a.out, "wb") as f:
        f.write(elf)

    print(f"image at 0x{a.offset:x}: {info['segments']} segments, entry 0x{entry:08x}, chip_id {info['chip_id']}, "
          f"checksum {'ok' if info['checksum_ok'] else 'BAD'}"
          + (f", sha256 {'ok' if info.get('sha256_ok') else 'BAD'}" if info["hash_appended"] else "")
          + f", ends at 0x{info['image_end']:x}")
    for (name, addr, size, off), (_, _, src) in zip(layout, segs):
        print(f"  {name:<10} 0x{addr:08x}..0x{addr + size:08x}  {size:>8} bytes  flash 0x{src:06x}  elf 0x{off:06x}")
    print(f"{len(symbols)} ROM symbols; wrote {a.out} ({len(elf)} bytes), sha256 {hashlib.sha256(elf).hexdigest()}")
    if not info["checksum_ok"] or (info["hash_appended"] and not info.get("sha256_ok")):
        print("the image's own integrity check FAILED: is the offset right?", file=sys.stderr)
        return 1
    if a.verify and not verify(elf, segs):
        print("verify FAILED: the ELF does not carry the image's bytes", file=sys.stderr)
        return 1
    if a.verify:
        print("verify ok: every segment byte is in the ELF at its load address")
    return 0


if __name__ == "__main__":
    sys.exit(main())
