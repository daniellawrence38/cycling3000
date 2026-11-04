#!/usr/bin/env python3
"""Capture a fixed-length serial log to a file, then exit by itself.

Why this exists rather than `python -m serial.tools.miniterm ... | Tee-Object`:

  * miniterm never ends on its own, so a capture had to be stopped by hand -- and when its output is
    piped to a file, the pipe can swallow the Ctrl+] that would have stopped it cleanly. Killing the
    window instead can lose the unflushed tail of the file, which is the part you wanted.
  * This runs for a stated number of seconds, flushes after every read, and stops. Nothing to
    interrupt, nothing to lose.

It also takes the same care miniterm's `--dtr 0 --rts 0` does, and for a specific reason on this
board: **DTR drives GPIO 0, which is both a UART RX line and the ESP32's boot strapping pin.** Letting
the host assert it on open can drop the unit into the ROM download loader -- dark screen, no firmware,
"it will not turn on". So DTR and RTS are both deasserted before the port is opened, not after.

Nothing here writes to the port.

Usage:
    python tools/capture_serial.py COM3 --seconds 300 --out nrf-capture.txt
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is not installed in this interpreter: python -m pip install pyserial")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. COM3 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=300.0,
                    help="how long to capture; the firmware's own capture is about 200 s")
    ap.add_argument("--out", default="nrf-capture.txt")
    ap.add_argument("--quiet", action="store_true", help="do not echo to the console")
    a = ap.parse_args()

    # Configure before opening, so the host never asserts the reset or strapping lines. See the note
    # at the top of this file: on this board that is the difference between a capture and a brick
    # scare.
    p = serial.Serial()
    p.port = a.port
    p.baudrate = a.baud
    p.timeout = 0.5
    p.dtr = False
    p.rts = False
    try:
        p.open()
    except serial.SerialException as e:
        sys.exit(f"could not open {a.port}: {e}\n"
                 "If a monitor is still running on that port, close it first.")

    print(f"capturing {a.port} at {a.baud} for {a.seconds:g} s into {a.out}")
    print("Power-cycle the unit now; the boot log will be caught from the start.")
    print("Ctrl+C stops early and still leaves a complete file.\n")

    deadline = time.time() + a.seconds
    total = 0
    try:
        with open(a.out, "wb") as f:
            while time.time() < deadline:
                data = p.read(4096)
                if not data:
                    continue
                f.write(data)
                f.flush()  # so an early exit still leaves everything read so far
                total += len(data)
                if not a.quiet:
                    sys.stdout.write(data.decode("utf-8", "replace"))
                    sys.stdout.flush()
    except KeyboardInterrupt:
        print("\nstopped early", file=sys.stderr)
    finally:
        p.close()

    print(f"\n{total} bytes written to {a.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
