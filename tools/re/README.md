# tools/re: reading the stock firmware and the vendor app, offline

Everything here works on **files**: the flash backup in `outputs/esp32s3-backup/` and a copy of the
vendor's Android app. Nothing here connects to the unit, a serial port, USB or a phone (START-HERE
rule 1), and nothing here needs or stores a key, token or password (rule 2).

The hardware facts in `docs/BRINGUP-LOG.md` and `docs/NRF-PROTOCOL.md` were first found by hand:
find a string, find the `l32r` that loads it, read the code around it. These tools do the same thing
reproducibly, so a finding can be re-checked by running a command instead of trusting the prose.

## What is here

| File | What it does |
| --- | --- |
| `setup_wsl.sh` | Installs the tools under `/opt/re` in WSL, each pinned and checksum-verified: Node 24.22.3, OpenJDK 25, Ghidra 12.2, `rea-agents` 6.9.0. |
| `esp_image_to_elf.py` | Copies the stock application's segments out of the flash backup into an Xtensa ELF at their load addresses, checks the image's own checksum and SHA-256, and (`--verify`) re-reads the ELF against the image byte for byte. Adds the chip's mask ROM symbol names from ESP-IDF. |
| `gh.sh` | Runs one Ghidra script against the analysed firmware, read-only, and prints only the script's result. |
| `ghidra/DecompileAt.java` | Decompiles the function at an address (or by name) and lists what calls it and what it calls. |
| `ghidra/StringXrefs.java` | Finds every use of a string by its text, following both hops (instruction to literal pool, literal to string). This is the old hand method. |
| `rd.py` | Prints the bytes at a virtual address in the ELF as hex, uint32 and float: for reading the pointers and constants a decompile refers to. Its segment table is the one `esp_image_to_elf.py` prints for this backup. |
| `find_uuid.py` | Looks for 128-bit Bluetooth UUIDs in a binary, to tie the phone app to a firmware image. |
| `rea.sh` | Runs REA with a fixed offline configuration and refuses its live-target commands. |

## One-time setup (WSL, as root)

```
bash tools/re/setup_wsl.sh
python3 tools/re/esp_image_to_elf.py outputs/esp32s3-backup/original-flash-16MB.bin \
    --offset 0x20000 --out /opt/re/work/stock-ota0.elf \
    --rom-ld /opt/esp/esp-idf/components/esp_rom/esp32s3/ld --verify
JAVA_HOME=/usr/lib/jvm/java-25-openjdk-amd64 GHIDRA_HEADLESS_MAXMEM=3G \
    /opt/re/ghidra/support/analyzeHeadless /opt/re/work/ghidra-proj cc700 \
    -import /opt/re/work/stock-ota0.elf
```

The analysis is slow and wants memory. Run it on its own: on 17 November 2026 Windows ran out of
virtual memory and took the whole WSL VM down while this, a firmware build and the test suite were
running together (drive C: had 2.7 GB free, which limits the page file).

`/opt/re/work/` holds the ELF, the Ghidra project and the vendor APK. None of it is in the
repository: the ELF and the APK are other people's code, and all of it can be rebuilt from the
backup with the commands above.

## Everyday use

```
bash tools/re/gh.sh DecompileAt.java 0x4222137c          # the link's CRC routine
bash tools/re/gh.sh StringXrefs.java "InstrLen Error"    # who logs this, and from where
bash tools/re/rea.sh inspect-artifact outputs/esp32s3-backup/original-flash-16MB.bin \
    --detail summary --evidence_output /opt/re/work/flash-evidence
```

## What REA can and cannot do here (tried 17 November 2026, REA 6.9.0)

REA ("Reverse Engineer Anything", github.com/morluto/rea) was the reason these tools were set up. It
was given a pass-or-fail trial against facts already confirmed on the unit.

| Job | Result |
| --- | --- |
| Read the 16 MB flash backup as an ESP flash image | **Works, and agrees with our own findings independently**: ESP32-S3, bootloader built from IDF `423e6536`, partition table MD5 valid with six partitions, `ota_0` a valid app (`CC700Pro_V1.710-2-g44e3c9e-dirt`, checksum and SHA-256 valid), `ota_1` erased, `otadata` record 0 selecting slot 0. |
| Decompile the stock firmware | **Refused**: `unsupported ELF architecture: e_machine 94` (Xtensa). Its guidance says the same of ESP images: "Segments are not disassembled, and RISC-V or Xtensa segments cannot be opened as raw-binary." |
| Read the vendor APK's signature | **Works**: Google Play signing, digest matches. |
| Decompile the vendor APK | Not tried: it needs a separately supplied `jadx-headless-mcp` JAR that REA does not install. |
| A future nRF (ARM Cortex-M) image | Its guidance lists "Thumb-only, and Cortex-M variants" as unsupported for headerless images. |

So for this project REA is a container and package inspector, not the decompiler. The decompiling
is done by Ghidra directly, which supports Xtensa, through `gh.sh` and the two scripts above. That
combination passed the trial: it reproduces the link's CRC routine (`0x4222137c`, polynomial at
`0x4220e4a8`) and frame builder (`0x4205062c`) exactly as confirmed on the wire, and went on to give
the key, clock, battery and light messages and the exact power commands
(`docs/NRF-PROTOCOL.md`, "What the vendor's handlers say").

`rea setup` has **not** been run: it registers REA's MCP server in the Claude configuration, which
buys nothing while REA cannot open the firmware.

## Reading a decompile honestly

The decompiler's C is a reconstruction. Names are Ghidra's (`FUN_…`, `DAT_…`), types are guesses, and
on this firmware most calls go through pointers in literal pools, so a call target is sometimes a
pointer that has to be read out of the image (`tools/re/rd.py <address>` does that). Treat a decompile as
strong evidence about what the code does with bytes, and as no evidence at all about what a board
has fitted. A layout is "confirmed" in the docs only when a capture agrees with it.
