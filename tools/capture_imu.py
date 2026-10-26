#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""Record raw IMU samples from a Dong over USB into test/data/.

The Dong streams CSV while recording ('R' starts, 'S' stops). Recordings are
replayed by the host tests (pio test -e native -f test_replay).

    uv run tools/capture_imu.py swing-figure8 --seconds 60
    uv run tools/capture_imu.py still-on-table --port /dev/ttyACM0
"""

import argparse
import pathlib
import sys
import time

import serial
from serial.tools import list_ports

ESPRESSIF_USB_VID = 0x303A
DATA_DIR = pathlib.Path(__file__).resolve().parent.parent / "test" / "data"


def find_port() -> str:
    for p in list_ports.comports():
        if p.vid == ESPRESSIF_USB_VID:
            return p.device
    sys.exit("No Dong found on USB; pass --port")


def record(port_path: str, seconds: float, out: pathlib.Path) -> int:
    """Records `seconds` of samples to `out`; returns the sample count."""
    if out.exists():
        sys.exit(f"{out} already exists")
    out.parent.mkdir(parents=True, exist_ok=True)

    with serial.Serial(port_path, 921600, timeout=0.5) as port:
        port.reset_input_buffer()
        port.write(b"R")
        lines, header_seen = [], False
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            raw = port.readline().decode("ascii", errors="replace").strip()
            if not raw:
                continue
            if raw.startswith("# kalman-dong imu"):
                header_seen = True
            if header_seen:
                lines.append(raw)
        port.write(b"S")

    rows = sum(1 for l in lines if l and l[0].isdigit())
    if not header_seen or rows == 0:
        sys.exit("No samples received (is the firmware running and the IMU found?)")
    out.write_text("\n".join(lines) + "\n")
    return rows


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", help="recording name, e.g. swing-figure8")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--port")
    args = ap.parse_args()

    out = DATA_DIR / f"{args.name}.csv"
    print(f"Recording {args.seconds:.0f} s -- swing away.")
    rows = record(args.port or find_port(), args.seconds, out)
    print(f"Wrote {rows} samples ({rows / args.seconds:.0f} Hz) to {out}")


if __name__ == "__main__":
    main()
