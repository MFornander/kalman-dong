# IMU recordings

Raw IMU samples from a real board, kept to compare orientation filters on real
motion. Every recording here is replayed through every filter in
[`FilterBank`](../../lib/core/src/kd/filter_bank.h) by:

```bash
pio test -e native -f test_replay -v
```

That prints, per recording and filter: when Ignition finished, the worst tilt
error at Rest, the heading change, the tilt error at quiet moments in
motion (slow rotation, |accel| near 1 g, where the accelerometer shows true
down), and how much Travel built up. None of these recordings moves the Hilt
deliberately, so any Travel is error. Today it stays at 0 m on all four. A new filter added to the bank shows up there with no other change.

## The recordings

All four: XIAO ESP32-S3 Plus display board (`xiao_screen` profile),
LSM6DS3TR-C at about 817 Hz, ±2000 dps and ±16 g, held by hand with the USB
cable attached. Columns are `t_us,gx,gy,gz,ax,ay,az` in the **sensor** frame
(rad/s, m/s²): no Mounting applied, so the same files work for any Mounting.

| File | When | What happened |
|---|---|---|
| `bench-desk-20s.csv` | 2026-10-23 | Lying still on a desk. Ignition finds Rest within 1 s. |
| `bench-still-30s.csv` | 2026-10-26 | Lying still on a desk, 30 s. Ignition times out (board just set down). |
| `bench-sweeps-30s.csv` | 2026-10-26 | Slow hand sweeps and tilts, like exploring the clouds. Peak 6.4 rad/s. |
| `bench-swings-30s.csv` | 2026-10-26 | Fast hand swings and twists, ending at rest on the desk. Peak 17 rad/s. |

## Baseline to beat

From the replay report when the recordings were made:

| Recording | Measure | Mahony | Kalman |
|---|---|---|---|
| still 30 s | heading drift | **0.07°** | 1.0° |
| sweeps | tilt error at quiet moments, mean | 4.6° | **4.5°** |
| swings | tilt error at quiet moments, mean | 1.1° | **0.95°** |

The quiet-moment yardstick includes some hand acceleration, so differences
under about half a degree are noise. Per-sample cost on the board, for
reference: Mahony ~100 µs, Kalman ~200 µs, of a 1224 µs budget.

## Adding recordings

With the board on USB:

```bash
uv run tools/bringup.py record NAME --seconds 30
```

Name new files for what they show (`dong-2m-overhead-spins.csv`, not
`test3.csv`) and add a row to the table above. Recordings whose name starts
with `bench-desk` must ignite by learning the gyro bias; the replay test
checks that.
