# kalman-dong
Hippie Lightsaber

A fur-covered LED staff that uses its motion sensor to make you feel like you
are slicing through slowly drifting rainbow clouds hanging in the air around
you. The vocabulary (Dong, Blade, Line, Cloud Field, Girth, Push, ...) is
defined in [CONTEXT.md](CONTEXT.md); key decisions are in [docs/adr/](docs/adr/).

## Hardware

- Seeed **XIAO ESP32-S3 Plus with the 1.14" display** carrier: onboard
  LSM6DS3-family IMU (I2C, INT1 on GPIO41), ST7789 display, buttons A/B/C.
- **SK9822/APA102** 4-pin Pixels in 4 Lines along a 1-2 m Blade. Data and
  clock go on the free pads GPIO38/39 (a second Strand uses GPIO40/42),
  through a 74AHCT125 level shifter for 5 V Pixels.
- One battery pack feeds the Pixels and the board's 5 V input (via a diode).
  Pack voltage can be sensed through a divider on a free pad.

Each physical build is a **Dong Profile** in [src/profiles.h](src/profiles.h):
Blade length, Pixels per Line, how Strands fold into Lines, pins, IMU axis
mapping, Power Budget, battery. The IMU must be mapped so body +X points from
the Hilt to the tip.

| env               | profile                                                  |
|-------------------|----------------------------------------------------------|
| `xiao`            | 1 m, 4×60 Pixels, one folded Strand, 3 A budget          |
| `xiao_2m`         | 2 m, 4×120 Pixels, one folded Strand, 6 A, pack sensing  |
| `xiao_lightswarm` | LightSwarm Dong: 2 Strands × 150, software SPI, 1.6 A    |

## Using it

| Button | Short press      | Long press      |
|--------|------------------|-----------------|
| A      | next Palette     | step Clear Sky  |
| B      | step brightness  | step Girth      |
| C      | wake the display | Re-level        |

Power on and hold the Dong still for a second. **Ignition** extends the
Blade like a lightsaber while it learns which way is down, then it fades into
the clouds. Re-level (hold C) points the clouds' reference heading where you
are pointing. After a minute at Rest the Blade dims; any motion wakes it.

## Building and testing

Toolchain: PlatformIO + pioarduino (Arduino core 3.3.x) — see
[ADR 0004](docs/adr/0004-pioarduino-platform.md).

```bash
uv tool install platformio==6.2.0
```

```bash
pio test -e native
```

```bash
pio run -e xiao -t upload
```

```bash
pio device monitor -e xiao
```

All logic lives in [lib/core](lib/core/src/kd) as plain C++17 and is tested on
the PC; [src/](src) is thin hardware glue. In the serial monitor, `i` prints
status.

### Recording swings

With the Dong plugged in over USB:

```bash
uv run tools/capture_imu.py swing-figure8 --seconds 60
```

Recordings land in `test/data/` and are replayed through the motion tracker by
`pio test -e native -f test_replay`.

## Status

Built and host-tested only; **not yet run on hardware**. First things to
check on a real Dong:

1. IMU axis directions vs. the Blade (set `imuAxes` in the profile).
2. FastLED on SPI3 alongside the display on SPI2 (no flicker, display works).
3. Frame rate and IMU rate on the display (targets: ~240 fps, 833 Hz).
4. Real current draw vs. the Power Budget.
5. The two-Strand profile's software SPI.
