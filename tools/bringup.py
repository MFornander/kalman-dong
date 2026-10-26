#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""Guided bring-up of a Dong (or the bare board) over USB.

    uv run tools/bringup.py check [--env xiao_screen] [--no-flash]
        Flash, then check the IMU, frame and IMU rates, and what each
        orientation filter costs per IMU sample.
    uv run tools/bringup.py mount
        Run Mounting Calibration (follow the prompts on the Dong), then
        verify it: right edge / tip up, then lying flat.
    uv run tools/bringup.py record NAME [--seconds 30]
        Count down, then record raw IMU into test/data/NAME.csv.
    uv run tools/bringup.py compare
        Replay every recording in test/data through both filters.
    uv run tools/bringup.py all
        Everything above in order, pausing for Enter between steps.

Each step prints PASS/FAIL lines and exits non-zero if anything failed.
"""

import argparse
import pathlib
import re
import subprocess
import sys
import time

import serial

import capture_imu

ROOT = pathlib.Path(__file__).resolve().parent.parent
failures: list[str] = []


def verdict(ok: bool, what: str, detail: str = "") -> bool:
    print(f"  {'PASS' if ok else 'FAIL'}  {what}{'  (' + detail + ')' if detail else ''}")
    if not ok:
        failures.append(what)
    return ok


def open_port(path: str, wait: float = 15.0) -> serial.Serial:
    """Opens the Dong's port, waiting out USB re-enumeration after a reset."""
    deadline = time.monotonic() + wait
    while True:
        try:
            return serial.Serial(path, 921600, timeout=0.1)
        except (serial.SerialException, OSError):
            if time.monotonic() > deadline:
                raise
            time.sleep(0.3)


def query(port: serial.Serial, cmd: str, wait: float = 0.7) -> list[str]:
    port.reset_input_buffer()
    port.write(cmd.encode())
    end, buf = time.monotonic() + wait, b""
    while time.monotonic() < end:
        buf += port.read(4096)
    return buf.decode("ascii", errors="replace").splitlines()


def number(lines: list[str], pattern: str) -> float | None:
    for line in lines:
        m = re.search(pattern, line)
        if m:
            return float(m.group(1))
    return None


def text(lines: list[str], pattern: str) -> str | None:
    for line in lines:
        m = re.search(pattern, line)
        if m:
            return m.group(1)
    return None


def flash(env: str, port_path: str) -> None:
    print(f"Flashing {env} ...")
    for attempt in range(3):
        r = subprocess.run(["pio", "run", "-e", env, "-t", "upload", "--upload-port", port_path],
                           cwd=ROOT, capture_output=True, text=True)
        if r.returncode == 0:
            verdict(True, f"flash {env}")
            time.sleep(2.0)  # let it boot and finish Ignition
            return
        time.sleep(1.0)
    verdict(False, f"flash {env}", r.stdout.strip().splitlines()[-1] if r.stdout else "pio failed")
    sys.exit(1)


def check(port_path: str, env: str, do_flash: bool) -> None:
    print("\n== Check")
    if do_flash:
        flash(env, port_path)
    with open_port(port_path) as port:
        time.sleep(3.0)  # rates are averaged over the first seconds
        info = query(port, "i")
        chip = text(info, r"chip=(\S+)")
        verdict(chip not in (None, "none"), "IMU found", chip or "no reply")
        hz = number(info, r"imu=(\d+)Hz")
        verdict(hz is not None and hz >= 780, "IMU rate >= 780 Hz", f"{hz} Hz")
        fps = number(info, r"fps=(\d+)")
        verdict(fps is not None and fps >= 200, "frame rate >= 200 fps", f"{fps} fps")
        render = number(info, r"render (\d+)")
        total = number(info, r"total (\d+)")
        print(f"        frame: render {render} us, total {total} us")

        costs = {}
        for _ in range(2):  # measure both filters, ending on the one we started with
            name = text(info, r"filter=(\w+)")
            read = number(info, r"imu us: read (\d+)")
            track = number(info, r"track (\d+)")
            costs[name] = (read, track, number(info, r"imu=(\d+)Hz"))
            query(port, "f")
            time.sleep(3.0)
            info = query(port, "i")
        for name, (read, track, rate) in costs.items():
            per_sample = (read or 0) + (track or 0)
            budget = 1e6 / rate if rate else 0
            verdict(rate is not None and rate >= 780 and per_sample < 0.9 * budget, f"{name}: IMU keeps up",
                    f"read {read} + track {track} = {per_sample:.0f} of {budget:.0f} us per sample")
        bench = query(port, "b", 3.0)
        line = text(bench, r"# (noise3.*)")
        if line:
            print(f"        benchmark: {line}")


def mount(port_path: str) -> None:
    print("\n== Mounting Calibration")
    print("  Pretend the board is in a finished Dong. Its Blade sticks straight out of the")
    print("  screen's right edge (as drawn on screen), and Line 0 is the top bar.")
    print("    1) Point the Blade at the ceiling; hold until green fills.")
    print("    2) Hold the Blade level with Line 0 on top; hold until green fills.")
    with open_port(port_path) as port:
        port.reset_input_buffer()
        port.write(b"c")
        deadline, result = time.monotonic() + 180, None
        while time.monotonic() < deadline and result is None:
            line = port.readline().decode("ascii", errors="replace").strip()
            if line.startswith("# banner:"):
                print("   ", line[len("# banner:"):].strip())
            elif line.startswith("# Mounting saved"):
                result = (True, line[len("# Mounting saved:"):].strip())
            elif line.startswith("# Mounting Calibration failed"):
                result = (False, line.split(":", 1)[1].strip())
        if not verdict(result is not None and result[0], "Mounting Calibration",
                       result[1] if result else "timed out"):
            return

        print("  Verify: point the Blade at the ceiling and hold ...")
        query(port, "a", 0.1)  # start the axis printout
        try:
            verdict(wait_for_pitch(port, lambda p: p > 75, 40), "Blade pitch ~ +90 with the tip up")
            print("  Now hold the Blade level (pointing at the horizon) ...")
            verdict(wait_for_pitch(port, lambda p: abs(p) < 15, 40), "Blade pitch ~ 0 lying flat")
        finally:
            query(port, "a", 0.3)  # stop it


def wait_for_pitch(port: serial.Serial, ok, timeout: float) -> bool:
    """True once the reported Blade pitch satisfies `ok` for ~1 s."""
    deadline, streak = time.monotonic() + timeout, 0
    while time.monotonic() < deadline:
        line = port.readline().decode("ascii", errors="replace")
        m = re.search(r"Blade pitch\s+(-?[\d.]+)", line)
        if not m:
            continue
        pitch = float(m.group(1))
        streak = streak + 1 if ok(pitch) else 0
        print(f"\r    pitch {pitch:6.1f}", end="", flush=True)
        if streak >= 5:  # 5 Hz printout
            print()
            return True
    print()
    return False


def record(port_path: str, name: str, seconds: float, what: str = "") -> None:
    print(f"\n== Record {name} ({seconds:.0f} s){': ' + what if what else ''}")
    for n in (3, 2, 1):
        print(f"  starting in {n} ...")
        time.sleep(1)
    print("  GO")
    out = capture_imu.DATA_DIR / f"{name}.csv"
    rows = capture_imu.record(port_path, seconds, out)
    verdict(rows / seconds > 700, f"recorded {out.name}", f"{rows} samples")


def compare() -> None:
    print("\n== Filter comparison on recordings")
    r = subprocess.run(["pio", "test", "-e", "native", "-f", "test_replay", "-v"], cwd=ROOT,
                       capture_output=True, text=True)
    for line in r.stdout.splitlines():
        if "MESSAGE:" in line:
            print("  ", line.split("MESSAGE:", 1)[1].strip())
    verdict(r.returncode == 0, "replay tests")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("step", choices=["check", "mount", "record", "compare", "all"])
    ap.add_argument("name", nargs="?", help="recording name (record)")
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--env", default="xiao_screen")
    ap.add_argument("--no-flash", action="store_true")
    ap.add_argument("--port")
    args = ap.parse_args()

    port = args.port or (capture_imu.find_port() if args.step != "compare" else "")
    if args.step == "check":
        check(port, args.env, not args.no_flash)
    elif args.step == "mount":
        mount(port)
    elif args.step == "record":
        if not args.name:
            sys.exit("record needs a NAME")
        record(port, args.name, args.seconds)
    elif args.step == "compare":
        compare()
    else:
        stamp = time.strftime("%m%d-%H%M")
        check(port, args.env, not args.no_flash)
        input("\nPress Enter to start Mounting Calibration ...")
        mount(port)
        for name, what in [("still", "lay it down and don't touch it"),
                           ("sweeps", "slow sweeps and tilts, like exploring the clouds"),
                           ("swings", "fast swings and twirls, the real thing")]:
            input(f"\nPress Enter to record '{name}' ({what}) ...")
            record(port, f"bringup-{stamp}-{name}", args.seconds, what)
        compare()

    print(f"\n{'All passed.' if not failures else 'FAILED: ' + ', '.join(failures)}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
