# simunit

The simulated unit (brief 2, WP3): the core library with a directory-backed `hal_fs` (`dirfs.c`, which refuses a rename onto an existing file as FAT does), a sensor and GNSS script player (`player.c`, the `tools/gen_ride` CSV format), and the protocol server on a localhost TCP socket.

```
build/host/simunit/simunit --root DIR [--port 7700] [--script FILE.csv] [--speed 1] [--start-at 0]
                           [--finish] [--exit] [--mtu 247] [--utc UNIX|none] [--tz 480] [--battery 80] [--quiet]
```

- `--root` is the simulated `CC700/` folder on the separate storage; simunit creates `objects/`, `rides/` and `config/` in it and nothing else.
- `--script` plays a CSV, one row per second, and presses START at `--start-at`. `--finish` then pauses and saves through the real button path (hold right, left, power, right). `--speed 0` runs as fast as possible; `--exit` quits after the script.
- Without `--script` it idles on Ready and serves the link until it is killed.
- `--utc none` starts with the clock unknown (rides get provisional IDs until TIME_SET).

TCP framing: every BLE packet (header byte plus data, protocol 2.5) travels as a u16 little-endian length followed by the packet, in both directions. One client at a time; connecting counts as the bonded phone. `--mtu` sets the negotiated ATT MTU, so 23 exercises the 19-byte packets and 512-byte chunks.

A Python client for tests is in `tools/linkclient/cc700link.py`.

Example, a headless 30-minute ride checked with the independent decoder:

```
python3 tools/gen_ride/gen_ride.py
build/host/simunit/simunit --root /tmp/cc700 --port 0 --script core/tests/fixtures/ride30.csv --speed 0 --finish --exit
python3 tools/fitcheck/fitcheck.py /tmp/cc700/rides/*.fit
```
