# kalman-dong
Hippie Lightsaber

[![The Dong swinging through an invisible rainbow fog, lighting up only the slice it passes through](docs/media/slicing-the-rainbow-fog.webp)](docs/media/slicing-the-rainbow-fog.mp4)

**[Watch with sound (MP4, 15 s)](docs/media/slicing-the-rainbow-fog.mp4)**: an
invisible rainbow fog hangs in the air, and the Dong lights up only the slice
it passes through, like a laser pointer in a smoky room. The fog and the
Pixels' colors come from the firmware's own Cloud Field.

A fur-covered LED staff that uses its motion sensor to make you feel like you
are slicing through slowly drifting rainbow clouds hanging in the air around
you. The vocabulary (Dong, Blade, Line, Cloud Field, Girth, Push, ...) is
defined in [CONTEXT.md](CONTEXT.md); key decisions are in [docs/adr/](docs/adr/).

## Hardware

- Seeed **XIAO ESP32-S3 Plus with the 1.14" display** carrier: onboard
  LSM6DS3-family IMU (I2C, INT1 on GPIO41), ST7789 display, buttons A/B/C
  and a PDM microphone (clock GPIO1, data GPIO2).
- **SK9822/APA102** 4-pin Pixels in 4 Lines along a 1-2 m Blade. Data and
  clock go on the pads D11/D12 (GPIO38/39; a second Strand uses D13/D15).
  The 1 m prototype drives them directly at 4 MHz; longer builds add a
  74AHCT125 level shifter.
- The prototype runs from a two-port USB power bank: one port to the board's
  USB-C, one to the Pixels' 5 V at the Hilt. Three external buttons wire to
  the U1-U3 pads.

Wiring, power, buttons and first light:
[docs/led-build.md](docs/led-build.md), with the
[exact pads on the carrier](docs/led-build.md#where-each-wire-goes-on-the-carrier).

Each physical build is a **Dong Profile** in [src/profiles.h](src/profiles.h):
Blade length, Pixels per Line, how Strands fold into Lines, pins, IMU axis
mapping, Power Budget, battery. The IMU must be mapped so body +X points from
the Hilt to the tip.

| env               | profile                                                  |
|-------------------|----------------------------------------------------------|
| `xiao_screen`     | bare board on the bench; Blade drawn on the display      |
| `xiao`            | 1 m, 4×60 Pixels, one folded Strand, 3 A budget          |
| `xiao_2m`         | 2 m, 4×120 Pixels, one folded Strand, 6 A, pack sensing  |
| `xiao_lightswarm` | LightSwarm Dong: 2 Strands × 150, software SPI, 1.6 A    |

## Using it

Every button has three presses: **Quick** (let go before 2 s), **Long** (let
go between 2 and 5 s) and **Held** (reaches 5 s; acts then). While a button is
down, the display's header shows a bar of half-second segments (teal for
Quick, amber for Long) and what letting go will do.

| Button | Quick            | Long            | Held                 | Bare board | Other builds |
|--------|------------------|-----------------|----------------------|------------|--------------|
| A      | next Palette     | cloud size: 0.3, 0.6, 1.2, 2.4 m | step Clear Sky | USR3 | USR1 |
| B      | cloud motion: frozen, 1x, 2x | step brightness | step Girth | USR2 | USR2 |
| C      | Status page (again to close; closes itself after 30 s) | Re-level | Mounting Calibration | USR1 | USR3 |

Each Dong Profile picks its button pins (`BUTTON_*_PIN` in
[src/profiles.h](src/profiles.h)).

Power on and hold the Dong still for a second. **Ignition** extends the
Blade like a lightsaber while it learns which way is down, then it fades into
the clouds. Re-level (hold C) points the clouds' reference heading where you
are pointing. After a minute at Rest the Blade dims; any motion wakes it.

Sound drives brightness: in the room's usual quiet the Blade glows at 15%,
and it brightens to full with the loudest sounds it has heard lately. A
steady hum (a fan, a crowd) fades back to quiet within about 15 s, so beats
and shouts over it still show. Serial `m` turns this off and on.

### Mounting Calibration (once per build)

The Dong has to know how its IMU sits relative to the Blade. Hold **C for 5
seconds** (on the bare board C is USR1), or send `c` over serial; calibration
starts as the press bar fills:

1. **Point the Blade at the ceiling** and hold. Every Line shows a bright
   tip; green fills from the Hilt as the hold registers (~2 s). Normal hand
   shake is fine.
2. **Hold the Blade level with Line 0 on top** and hold. Only Line 0 lights.

On the bare board, pretend it is mounted in a Dong: the Blade sticks straight
out of the screen's right edge (away from the USB cable, as the Blade view
draws it) and Line 0 faces out of the screen. So pose 1 is the right edge up
with the screen facing you, and pose 2 is lying flat, screen up. Check it with
TIP in the screen's header: about +90 in pose 1, 0 lying flat.

The result is saved in flash and Ignition runs again. A square-mounted board
snaps to the exact axes, so roughly held poses are fine; an angled mount is
kept as measured. Until calibrated, the profile's `imuAxes` is used.

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

## Bench testing with just the board

The `xiao_screen` build draws the four Lines on the display (landscape: Hilt
on the left, tip on the right, Line 0 on top) exactly as they would go to the
Pixels, after brightness and the Power Budget. The board stands in for the
Hilt; the right edge of the screen is the direction the Blade points.

At the tip end, a header reads upright with the Blade pointing up, as a grid
of x, y and z:

- **POS** is how far the Dong has moved through the clouds (Travel plus Push),
  in centimetres, wrapping every 10 m to -500..499.
- **ROT** is how it has turned since Ignition or the last Re-level, as roll,
  tip up and turn in percent of a half turn (90° = 50, 180° = 100). Positive
  is rolled clockwise looking out along the Blade, tip raised, and turned
  clockwise seen from above. The turn is where heading drift, and so the
  orientation filter, shows.

While a button is held the header shows what letting go will do instead, and
setting changes show there briefly. The Status page (C quick) is upright in
the same pose, with the same grid at the top. The screen turns off after 60 s
of lying still with no button pressed, and comes back when the board moves
or a button is pressed.

The quickest route is the guided bring-up script: it flashes, checks rates
and filter costs, runs Mounting Calibration and verifies it, records swings
and replays them through both filters, printing PASS/FAIL as it goes.

```bash
uv run tools/bringup.py all
```

Steps can also run one at a time (`check`, `mount`, `record NAME`,
`compare`); see `uv run tools/bringup.py --help`. Manually:

```bash
pio run -e xiao_screen -t upload
```

```bash
pio device monitor -e xiao_screen
```

Serial commands: `i` status plus per-stage frame, IMU and display timings, `c`
start Mounting Calibration, `f` switch orientation filter (Mahony / Kalman),
`a` toggle axis check (5 Hz), `b` Cloud Field micro-benchmark, `v` toggle the
Blade view, `R`/`S` start/stop raw IMU streaming, `z` log a line every frame
for 3 s (then `i` shows the worst loop, which should stay a few ms even with
the port closed), `m` sound drives brightness or not, `l` log the
microphone's level and Loudness 10 times a second.

1. **Boot and IMU.** The Blade view comes up right away (C shows the Status page). `i`
   should name the chip (LSM6DS3 / LSM6DS3TR-C / LSM6DSO) and report about
   833 Hz IMU and up to 240 fps.
2. **Mounting.** Run Mounting Calibration (hold C for 5 s, see above). The
   header's TIP should then read about +90 with the Blade at the ceiling and
   about 0 with it level.
3. **Ignition.** Reset with the board still: the bars extend from the left
   in about a second. Reset while shaking it: still done within two seconds.
4. **Slicing.** With the clouds frozen (the default; B quick cycles frozen,
   1x, 2x), sweep the board left and right and tilt it up and down: the
   colors change as you move through the clouds and return exactly when you
   come back to the same pointing direction.
5. **Girth.** Hold B for 5 s to step Girth; at 0 all four bars match. At
   0.4 m, roll the board 90 deg about the Blade: the colors move one bar over
   (what was on TOP shows on L after a clockwise roll seen from the Hilt).
6. **Buttons.** See the table above; while a button is down the header shows
   what letting go will do.
7. **Settings survive power cycles.** Change something, wait 3 s, unplug,
   plug back in.
8. **Rest.** Lay the board down: after 60 s the bars dim to 20%; pick it up
   and they brighten instantly. Leave it a few minutes and watch for heading
   drift (the clouds slowly turning).
9. **Drift data.** Record the board lying still and some waving for the
   replay tests: `uv run tools/capture_imu.py bench-still --seconds 120`.
10. **Sound.** `l` logs the microphone: a quiet room sits at Loudness 0 and
    the bars at 15% (the Status page's bottom row shows it too). Talk or clap
    near the board: the bars brighten at once and fade back within a second.
11. **LED outputs (optional).** A scope or logic analyzer on GPIO38 (data)
    and GPIO39 (clock) should show the SK9822 stream while the display keeps
    updating; a short strip on those pads works too (mind the 3.3 V logic).

Screen colors are gamma-encoded to approximate what the Pixels emit; they
will still look more saturated than LEDs under fur.

## Status

Running on the bare board: LSM6DS3TR-C at 817 Hz, Ignition learns the gyro
bias, Rest detection works. Frame rates measured on the board (LED output
running, no LEDs attached):

| env               | render | frame  | fps           |
|-------------------|--------|--------|---------------|
| `xiao_screen`     | 2.2 ms | 2.9 ms | 240 (the cap) |
| `xiao_lightswarm` | 2.9 ms | 3.7 ms | 240 (the cap) |
| `xiao_2m`         | 3.7 ms | 4.6 ms | 217           |

Hardware lessons so far:

- FastLED 3.10.8 crashed the S3 and 3.9.20 bit-bangs (3.5 ms per frame), so
  Pixels go out through our own SPI2 DMA driver
  ([ADR 0005](docs/adr/0005-own-led-driver.md)); the display owns SPI3.
- Seeed_GFX sends one pixel per SPI call; the Blade view bypasses it with
  one burst per bar (~21 ms per screen).
- The S3 has no double-precision FPU and no float divide: keep `double`,
  division, `std::floor` and function-local statics out of per-sample code
  (together they cost 2x in rendering).
- Only GPIO1-20 can read analog, and all of those are taken on this carrier
  except the board's own JST LiPo divider (GPIO10). Pack sensing for the 2 m
  build needs a 1S pack on the JST or an I2C fuel gauge.
- Real gyro noise is ~0.03 rad/s sample to sample; Rest detection works on
  smoothed readings (see `test/data/bench-desk-20s.csv`).

Two orientation filters sit behind one interface: Mahony (default) and an
error-state Kalman filter with innovation gating; `f` cycles them live, and
`-DKD_FILTER=Kalman` in a build's `build_flags` makes another the default.
[kd/filter_bank.h](lib/core/src/kd/filter_bank.h) lists them and says how to
add one. In
simulation the Kalman filter holds tilt 13x better when the gyro bias shifts
mid-swing and somewhat better through big swings, and is slightly worse
under hand jitter (`pio test -e native -f test_motion -v` prints the
comparison). On real hand-held recordings (`test/data/bench-*`) they tie on
tilt (about 1 deg in fast swings, 4.5 deg in slow sweeps, at quiet moments
mid-motion), while Mahony holds heading better at rest (0.07 vs 1.0 deg over
30 s) at half the CPU, so Mahony stays the default.

IMU cost per sample on the board (budget 1224 us at 817 Hz): I2C read ~550
us, tracking ~100 us with Mahony and ~200 us with Kalman.

Still to check, on a real strip: LED colors, signal quality at 4 MHz
without a level shifter (the 1 m prototype), and current draw.
