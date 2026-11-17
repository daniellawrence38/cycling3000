# What the vendor's phone app reveals

A first look, 17 November 2026. Read from a file only: the OnelapFit Android package, opened as a zip
and searched as text. **Nothing was installed or run, no account was used, no vendor server was
contacted, and the unit was not involved.** The tools are in `tools/re/`.

This is triage, not a decode. Everything below is what the package *contains*; none of it has been
seen working, and anything about the unit's behaviour stays Unverified (START-HERE rule 3).

## The file

| | |
| --- | --- |
| App | OnelapFit 1.9.4, package `com.onelap.fitness` |
| Size, SHA-256 | 125,281,026 bytes, `ee599f84f4a7dd0500bea18121aa55517edd17775f8e892aae3ccf31eb9909b6` |
| Source | APKPure mirror, downloaded 17 November 2026 |
| Signature | Google Play app signing (`META-INF/GOOGPLAY.RSA`), certificate SHA-256 `8ec9d708…b0f6e8`; the v2 scheme's digest of the file matches. So the bytes are what Google Play distributes, not a repack. Read with REA's `inspect-apk-signature`. |

It is kept at `/opt/re/work/apk/` in WSL, outside the repository (`*.apk` is git-ignored).

## Findings

**1. The app and the stock firmware share a BLE service.** The app holds a family of 128-bit UUIDs
`8ce5cc01` … `8ce5cc07-0a4d-11e9-ab14-d663bd873d93`. The first of them is in the stock firmware
image three times in the byte order a BLE stack stores (`tools/re/find_uuid.py`, flash offsets
`0x4328f8`, `0x714958`, `0x714dde`). So that is the service the stock firmware offers the phone, and
the app's code for it describes the stock phone protocol. None of the app's other custom UUIDs
(Nordic UART, Nordic DFU, TI OAD and others, which belong to other products) is in the image.

**2. The CC700 Pro is a Magene C606-family computer.** The app's bundled device pages
(`assets/ble_web_pack.zip`) select behaviour by a numeric product id:
`{262:"c606", 310:"c606", 355:"c606V2", 401:"c606Pro", 405:"c606Pro", 50:"c706", 390:"cc700Pro"}`,
with 389 for the plain CC700. The CC700 Pro is handled by the same pages and the same
`C606SystemConfig` structures as Magene's C606. This fits the `MAGENE_ERROR` strings in the stock
firmware. Practical consequence: anything published about the Magene C606 family's hardware or
protocol is likely to apply to this unit.

**3. "SPU" means the product, not the co-processor.** The app's name for that numeric id is
`spu_id`, and it sits beside `spu_version`. So the stock firmware's `SPUID=%d Not Match DevType=%d`
and the factory screen's `SPUVer 1.711` are about the product model and its firmware version.
`docs/NRF-PROTOCOL.md`, Q36 and `hal/hal_nrf.h` called the nRF co-processor "the SPU" on the strength
of that one string; that was a wrong inference and is corrected there. Nothing in the code depended
on it.

**4. Firmware updates come from the vendor's server, not from the package.** The package contains no
firmware image. It contains the request paths `api/v1/app/device/firmware/checkVersion` and
`api/v1/app/device/firmware/upgrade` on `api-fitness.rfsvr.com`, a Nordic DFU library (2.9.0), and
update code that speaks of "nodes" with their own versions. Whether the CC700 Pro's nRF image is
delivered this way, separately from the ESP32 image, is not established. **Not done, deliberately:**
calling that server. It would mean sending requests to the vendor in this project's name, and very
likely an account token, which rule 2 puts out of bounds for me. See Q39.

**5. The app's own formats are in the package as text.** `route.proto`, `workout.proto` and
`wanlu_v3.proto` are plain protobuf definitions. `wanlu_v3.proto` is the app's own ride-recording
format (chunks for start, record, lap, pause, device info), not the unit's protocol. `route.proto`
and `workout.proto` are small and may describe what the app sends to a computer; not yet read
against the firmware.

## What this is good for, and what it is not

- It does **not** reveal the ESP32-to-nRF serial protocol (`docs/NRF-PROTOCOL.md`). The phone never
  sees that link. The route to the missing nRF answers (Q38) through the app is the nRF's firmware
  image, which the package does not hold.
- It **does** give a second, independent description of the stock phone protocol, which the original
  plan wanted investigated before replacing the firmware (development plan, step 3), and it gives the
  product family.

## Not done yet

The app's code has not been decompiled. It is three things at once: Kotlin and Java (13 dex files,
about 88 MB), a Flutter part (`libapp.so`), and the bundled web pages. The bike-computer protocol
classes are under `com.onelap.lib_ble` (about 4,000 references), with `C606SystemConfig`,
`BikeComputerProUartMerge` and the `command_factory` packages the obvious places to start.
