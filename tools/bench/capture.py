"""Talks to the bench firmware (pio run -e bench -t upload) over USB serial.

Sends one command, prints what the panel says, and saves the frame it drew as
a PNG, so the image can be checked without looking at the panel.

  python tools/bench/capture.py COM8 "date 2026-11-11" out.png
  python tools/bench/capture.py COM8 "name Carmalumis barneus" out.png
  python tools/bench/capture.py COM8 sync
"""
import sys
import time

import serial
from PIL import Image


def main():
    port, command = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else None
    timeout_s = float(sys.argv[4]) if len(sys.argv) > 4 else 300

    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 1
    s.dtr = False  # do not reset the board when the port opens
    s.rts = False
    s.open()
    with s:
        # If the board is still starting up, let it finish its banner first.
        quiet_since = time.time()
        start = time.time()
        while time.time() - quiet_since < 1.5 and time.time() - start < 20:
            line = s.readline().decode("ascii", "replace").strip()
            if line:
                print(f"[  boot ] {line}")
                quiet_since = time.time()
                if line == "READY":
                    break
        s.reset_input_buffer()
        s.write((command + "\n").encode())
        t0 = time.time()
        rows = None
        size = None
        while time.time() - t0 < timeout_s:
            line = s.readline().decode("ascii", "replace").strip()
            if not line:
                continue
            if line.startswith("FRAME "):
                _, w, h = line.split()
                size = (int(w), int(h))
                rows = []
                continue
            if rows is not None and line == "END":
                # A row damaged in transit is shown as a white row rather than
                # failing the capture.
                width_hex = size[0] // 8 * 2
                good = []
                bad = 0
                for r in rows[: size[1]]:
                    try:
                        if len(r) != width_hex:
                            raise ValueError
                        bytes.fromhex(r)
                        good.append(r)
                    except ValueError:
                        bad += 1
                        good.append("f" * width_hex)
                good += ["f" * width_hex] * (size[1] - len(good))
                if bad or len(rows) != size[1]:
                    print(f"warning: {bad} damaged rows, {len(rows)} of {size[1]} received")
                data = bytes.fromhex("".join(good))
                if out:
                    Image.frombytes("1", size, data).save(out)
                    print(f"[{time.time() - t0:6.1f}s] saved {out}")
                rows = None
                continue
            if rows is not None:
                rows.append(line)
                continue
            print(f"[{time.time() - t0:6.1f}s] {line}")
            if line == "READY":
                return 0
    print("timed out")
    return 1


if __name__ == "__main__":
    sys.exit(main())
