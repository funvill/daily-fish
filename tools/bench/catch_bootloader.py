"""Gets a deep-sleeping XIAO ESP32-C3 back into its bootloader without touching
the buttons.

A sleeping board has no USB port. It reappears for a fraction of a second at
each timer wake. This hammers the port, and the moment it opens sends the
USB-serial-JTAG "reset into download mode" line sequence, after which the
board stays put and `pio run -t upload` works.

  python tools/bench/catch_bootloader.py COM8 [seconds]
"""
import sys
import time

import serial

port = sys.argv[1]
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 600
t0 = time.time()
attempts = 0
while time.time() - t0 < seconds:
    try:
        s = serial.Serial()
        s.port = port
        s.baudrate = 115200
        s.dtr = False
        s.rts = False
        s.open()
    except Exception:
        time.sleep(0.01)
        continue
    attempts += 1
    try:
        # Same sequence esptool uses for USB-JTAG-serial chips, with short waits.
        s.rts = False
        s.dtr = False
        time.sleep(0.02)
        s.dtr = True
        s.rts = False
        time.sleep(0.02)
        s.rts = True
        s.dtr = False
        s.rts = True
        time.sleep(0.02)
        s.dtr = False
        s.rts = False
        s.close()
    except Exception:
        pass
    # In the bootloader the port stays; if it vanishes again, we missed.
    time.sleep(1.5)
    stayed = True
    for _ in range(40):
        try:
            c = serial.Serial()
            c.port = port
            c.dtr = False
            c.rts = False
            c.open()
            c.close()
        except Exception:
            stayed = False
            break
        time.sleep(1.0)
    if stayed:
        print(f"in bootloader after {time.time() - t0:.0f}s ({attempts} opens)")
        sys.exit(0)
    print(f"[{time.time() - t0:.0f}s] opened but the board slept again; still trying", flush=True)
print("gave up")
sys.exit(1)
