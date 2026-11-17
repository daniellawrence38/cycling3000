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

Nothing here writes to the port. If the port disappears (the unit powers off or resets), the
capture marks it in the file, waits for the port to come back and carries on, so a wake and the boot
after it are caught in the same file.

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

    deadline = time.time() + a.seconds
    t0 = time.time()
    p = open_port(a.port, a.baud)
    if p is None:
        print(f"{a.port} is not there yet: waiting for it (plug the unit in or switch it on)")
        p = wait_for_port(a.port, a.baud, deadline)
        if p is None:
            sys.exit(f"could not open {a.port} within {a.seconds:g} s.\n"
                     "If a monitor is still running on that port, close it first.")

    print(f"capturing {a.port} at {a.baud} for {a.seconds:g} s into {a.out}")
    print("If the port disappears (power-off, reset), the capture waits for it and carries on.")
    print("Ctrl+C stops early and still leaves a complete file.\n")

    total = 0
    try:
        with open(a.out, "wb") as f:
            while time.time() < deadline:
                try:
                    data = p.read(4096)
                except (serial.SerialException, OSError) as e:
                    # The unit switched off or reset and Windows removed the port. Note it in the file
                    # and wait for the port to come back, so the next boot is caught as well.
                    try:
                        p.close()
                    except Exception:
                        pass
                    mark(f, a.quiet, f"port lost at {time.time() - t0:.1f} s ({e.__class__.__name__}); waiting for it")
                    p = wait_for_port(a.port, a.baud, deadline)
                    if p is None:
                        mark(f, a.quiet, "port did not come back before the end of the capture")
                        break
                    mark(f, a.quiet, f"port back at {time.time() - t0:.1f} s")
                    continue
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
        if p is not None:
            try:
                p.close()
            except Exception:
                pass

    print(f"\n{total} bytes written to {a.out}")
    return 0


def open_port(port, baud):
    """Opens the port with DTR and RTS deasserted before opening (see the note at the top: on this
    board that is the difference between a capture and a brick scare). None if it cannot be opened."""
    p = serial.Serial()
    p.port = port
    p.baudrate = baud
    p.timeout = 0.5
    p.dtr = False
    p.rts = False
    try:
        p.open()
    except (serial.SerialException, OSError):
        return None
    return p


def wait_for_port(port, baud, deadline):
    while time.time() < deadline:
        p = open_port(port, baud)
        if p is not None:
            return p
        time.sleep(0.2)
    return None


def mark(f, quiet, text):
    line = f"\n--- capture_serial: {text} ---\n"
    f.write(line.encode())
    f.flush()
    if not quiet:
        sys.stdout.write(line)
        sys.stdout.flush()


if __name__ == "__main__":
    raise SystemExit(main())
