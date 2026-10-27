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
- Optional: an **M5Stack 8-Encoder unit** (U153) on the Grove port gives every
  setting its own knob (see [Knobs](#knobs)).

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

A, B and C are wired to the carrier's USR1-USR3 buttons, and which is which
depends on the build. On the LED builds (`xiao`, `xiao_2m`,
`xiao_lightswarm`) C is USR3. On the bare board (`xiao_screen`) A and C swap
so the Palette lands on USR3, the easiest button to press, and C is USR1.
Each Dong Profile picks its button pins (`BUTTON_*_PIN` in
[src/profiles.h](src/profiles.h)). Type `?` on the [serial console](#serial-console) to see the
pads of the build that's running.

### Knobs

With an [M5Stack 8-Encoder unit](https://shop.m5stack.com/products/8-encoder-unit-stm32f030)
plugged into the Grove port (before power-on), each setting has its own knob,
counted 1-8 as printed on the unit. One click of a knob moves its setting one
step; settings stop at their ends, and the Palette wraps around. The buttons
keep working alongside.

| Knob | Turn         | Push: Quick | Long (2 s)      | Held (5 s)           |
|------|--------------|-------------|-----------------|----------------------|
| 1    | Palette      | Status page | back to default |                      |
| 2    | brightness   | Status page | back to default |                      |
| 3    | Clear Sky    | Status page | back to default |                      |
| 4    | cloud size   | Status page | back to default |                      |
| 5    | cloud motion | Status page | back to default |                      |
| 6    | Girth        | Status page | back to default |                      |
| 7    | quiet glow: 5, 15, 30, 50, 75% | Status page | back to default |  |
| 8    | spare        | Status page | Re-level        | Mounting Calibration |

Knob 8's push works like button C. A quick push never changes a setting, so
bumping a knob while turning it is harmless. **Quiet glow** (knob 7) is how
bright the Blade stays in silence while sound drives brightness. The
**toggle switch** turns sound on and off: it decides at power-on and
whenever it's flipped, and serial `m` still works in between. The unit's
LEDs stay off. It runs from the Grove port's 3.3 V as it is.

Power on and hold the Dong still for a second. **Ignition** extends the
Blade like a lightsaber while it learns which way is down, then it fades into
the clouds. Re-level (hold C) points the clouds' reference heading where you
are pointing. After a minute at Rest the Blade dims and the screen turns off;
picking the Dong up wakes them. Picking up means turning it about 20° or
carrying it for 2 s, so a bump of the table doesn't.

Sound drives brightness: in the room's usual quiet the Blade glows at 15%
(the *quiet glow*; knob 7 on an 8-Encoder unit changes it), and it brightens
to full with the loudest sounds it has heard lately. A steady hum (a fan, a
crowd) fades back to quiet within about 15 s, so beats and shouts over it
still show. Serial `m` or the knob unit's switch turns this off and on.

### Voice (prototype, `xiao_screen` only)

Hold **USR3** and say a two-word command; let go when you're done. The
screen shows *Listening* while you hold it. A command heard replaces the
button's own press; if nothing is heard, the press counts as usual (a quick
press of A on this build still steps the Palette). A tap under 0.4 s skips
the recognizer and acts at once. After a longer press the recognizer needs
about half a second of silence to close the last word, so results come
0.6-0.9 s after letting go; pausing a beat before letting go lets it finish
while you still hold the button.

| Say | Does |
|-----|------|
| *next palette*, *next colors* / *previous palette*, *previous colors* | step the Palette |
| *lights up*, *more light* / *lights down*, *less light* | step brightness |
| *go faster*, *speed up* / *go slower*, *slow down* | step cloud motion |
| *freeze clouds*, *stop clouds* | freeze the clouds |
| *bigger clouds* / *smaller clouds* | step cloud size |
| *more clouds* / *fewer clouds*, *more sky* | step Clear Sky |
| *sound on* / *sound off* | sound drives brightness, or not |
| *reset heading* | Re-level |
| *show status* | open or close the Status page |

It runs Espressif's MultiNet7 English recognizer (ESP-SR, part of the
Arduino core) on core 0, below the IMU and mic tasks, so the IMU keeps its
rate and the LED core its frames. Listening takes about 27 ms of every
32 ms of audio, so the Status display redraws slowly while you hold the
button. The model (3.3 MB) lives in its own flash partition; write it once
per board:

```bash
pio run -e xiao_screen -t upload_voice_model
```

Serial `t` listens for 3 s as if USR3 were held, `g` steps the gain the mic
gets before the recognizer, `u` steps that closing silence (600 ms; 450,
300 and 200 to experiment), `V` lists every phrase, and `i` shows the
recognizer's timing. Each attempt logs what was heard, how much silence it
took and how loud you were.

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

All logic lives in [lib/core](lib/core/src/kd) as plain C++17 and is tested on
the PC; [src/](src) is thin hardware glue.

### Serial console

The firmware talks over the board's USB-C port. Open a console on it with
PlatformIO's monitor, naming the env you flashed (it sets the 921600 baud):

```bash
pio device monitor -e xiao_screen --port /dev/ttyACM0
```

`--port` is only needed when more than one serial device is plugged in; the
board shows up as `/dev/ttyACM0` on Linux (`/dev/cu.usbmodem*` on a Mac,
`COMn` on Windows; `pio device list` shows them).

- **Type `?` first.** It prints the firmware version, which USR pad each
  button is on for this build, and every command.
- **One key per command, no Enter.** The monitor sends each key as you type
  it and doesn't echo it.
- **Ctrl+C quits.**
- **Quit before flashing or running a tool.** `pio run -t upload`,
  `tools/capture_imu.py` and `tools/bringup.py` need the port to themselves:
  with the monitor open, flashing fails or the monitor swallows what the tool
  was waiting for.
- **Start it again after a reset or replug.** The board's USB port goes away
  while it restarts, and the monitor doesn't reconnect by itself.

An unknown key says to type `?`. The board prints the same list, plus its
version and which pads A, B and C are on, when you type `?` (or `h`):

| Key | Does |
|-----|------|
| **Status** | |
| `?` | this help, starting with the buttons and their pads (also `h`) |
| `i` | status: profile, IMU, filter, rates, per-stage frame and display timings, mic, knobs |
| `p` | raw button levels (0 = pressed) and how GPIO43 is wired |
| **Controls** | |
| `w` | as a quick press of C: open or close the Status page |
| `v` | Blade view on or off |
| `c` | start Mounting Calibration (as holding C for 5 s) |
| `k` | log the 8-Encoder unit's counts, pushes and switch as they change (again: stop) |
| **Sound** | |
| `m` | sound drives brightness, or not (as the 8-Encoder unit's switch) |
| `l` | log the mic's level and Loudness 10 times a second (again: stop) |
| **Voice** (`xiao_screen`) | |
| `t` | listen for 3 s, as if USR3 were held |
| `g` | next voice gain (1x to 32x) |
| `u` | next silence wait after letting go (600, 450, 300, 200 ms) |
| `V` | list every spoken command |
| **Motion** | |
| `f` | next orientation filter (Mahony, Kalman) |
| `a` | axis check, 5 times a second: which sensor axis sees gravity, where the Blade points (again: stop) |
| `R` / `S` | start / stop streaming raw IMU samples as CSV (`tools/capture_imu.py`) |
| **Performance** | |
| `b` | Cloud Field micro-benchmark |
| `z` | log a line every frame for 3 s; then `i` shows the worst loop, which should stay a few ms even with the port closed |

### Versioning

The firmware's version is in [VERSION](VERSION), as major.minor. **Every pull
request raises it**, usually the minor number (0.1 to 0.2); the GitHub check
*Version* fails a PR whose VERSION isn't above the base branch's. Two PRs open
at once both start from the same number, so whichever merges second raises
it again when it is rebased.

The build compiles in the version and the git commit (`git describe`, with
`-dirty` for uncommitted changes) through
[tools/pio_version.py](tools/pio_version.py). The serial console shows them
at boot and at the top of `?` and `i`, and IMU recordings carry `fw=` in their
header:

```
# kalman-dong 0.1 (28604ac) ready; ? for help
```

### Recording swings

With the Dong plugged in over USB:

```bash
uv run tools/capture_imu.py swing-figure8 --seconds 60
```

Recordings land in `test/data/` and are replayed through the motion tracker by
`pio test -e native -f test_replay`.

## Orientation filters

The Cloud Field is anchored by the Dong's orientation
([ADR 0002](docs/adr/0002-orientation-only-anchoring.md)), so the orientation
filter is what makes the clouds stay put in the air. It fuses the IMU's two
sensors, read about 817 times a second:

- the **gyro** is smooth and quick but drifts, and
- the **accelerometer** knows which way is down, but only when the Dong
  isn't being swung.

A filter only does that fusion. [MotionTracker](lib/core/src/kd/motion_tracker.h)
does the rest around it: it learns the gyro's bias whenever the Dong is at
Rest and hands the filter bias-corrected rates, and it runs Ignition,
Re-level, Travel and the look-ahead to when the Pixels light. With six axes
nothing can correct **heading** (the turn around vertical): it drifts
slowly, and shows as the clouds turning and in the ROT readout's turn
number. A magnetometer would fix that
([docs/research/imu-options.md](docs/research/imu-options.md)).

| Filter | How it works | Cost per sample on the board |
|---|---|---|
| **Mahony** (default) | Integrates the gyro and nudges tilt toward the measured gravity: 2 gains (`kp` 1/s, `ki` 0.02/s²). Trusts gravity fully within 0.5 m/s² of 1 g and not at all beyond 2.5 m/s², so swing forces don't tip it. | ~100 µs |
| **Kalman** | Error-state Kalman filter over orientation and the gyro bias MotionTracker hasn't caught. Trusts the accelerometer less the further it strays from 1 g, and skips readings too unlikely for its own uncertainty (innovation gate), which catches swings that pass through 1 g pointing nowhere near down. | ~200 µs |

Both are in [kd/orientation_filter.h](lib/core/src/kd/orientation_filter.h).
Each IMU sample has 1224 µs, of which the I²C read takes about 550.

### Why Mahony

On real hand-held recordings ([test/data/](test/data/README.md)) the two tie
on tilt, but Mahony holds heading far better at rest:

| Recording | Measure | Mahony | Kalman |
|---|---|---|---|
| still, 30 s | heading drift | **0.07°** | 1.0° |
| slow sweeps | tilt error at quiet moments, mean | 4.6° | 4.5° |
| fast swings | tilt error at quiet moments, mean | 1.1° | 0.95° |

Differences under about half a degree of tilt are noise in this yardstick.
Heading drift is the error a wielder actually sees (the clouds slowly turn),
and nothing corrects it, so it decides. Mahony also costs half the CPU and
has four settings to tune instead of nine.

In simulation (`pio test -e native -f test_motion -v` prints the report) the
Kalman filter holds tilt 13× better when the gyro bias shifts mid-swing, and
somewhat better through big swings, but slightly worse under hand jitter.
That bias shift is what a warming gyro does, so it is the case to revisit
with a better IMU or a magnetometer.

### Trying and adding filters

- **Switch live:** serial `f` cycles the filters; `i` names the one running
  and its cost (`imu us: ... track`).
- **Change a build's default:** add `-DKD_FILTER=Kalman` to its
  `build_flags` in [platformio.ini](platformio.ini).

To add a filter:

1. **Implement** `OrientationFilter`
   ([kd/orientation_filter.h](lib/core/src/kd/orientation_filter.h)):
   `name()`, `reset(orientation)`, `update(gyro, accel, dt)` and
   `orientation()`. It gets body-frame rates in rad/s with MotionTracker's
   bias already removed, and specific force in m/s² that reads +g upward at
   rest; it returns body to world, Z up. Leave bias at Rest, Re-level and
   heading reference to MotionTracker.
2. **Register** it in [kd/filter_bank.h](lib/core/src/kd/filter_bank.h): a
   member, an entry in the constructor's list, and one more in `kCount`.
   It then shows up in `f`, in `-DKD_FILTER=`, and in the replay of every
   recording.
3. **Test** it: add its type to `FILTERS` in
   [test/test_motion/test_main.cpp](test/test_motion/test_main.cpp), so every
   motion test runs against it, and to `compare()` there, so the simulated
   report shows it.
4. **Compare** it:
   - in simulation: `pio test -e native -f test_motion -v`;
   - on real recordings: `uv run tools/bringup.py compare` (or
     `pio test -e native -f test_replay -v`);
   - on the board: `f` until it runs, then `i` for its cost, which has to
     stay well inside the 1224 µs per sample.
5. **To make it the default,** change `kDefaultFilter` in
   [kd/filter_bank.h](lib/core/src/kd/filter_bank.h) and the test "Mahony is
   the default filter" in
   [test/test_filter_bank](test/test_filter_bank/test_main.cpp), and update
   *Why Mahony* above.

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
of lying still with no button pressed, and comes back when the board is
picked up (turned about 20° or moved for 2 s) or a button is pressed.

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

Then open the [serial console](#serial-console) and type `?`.

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
8. **Rest.** Lay the board down: after 60 s the bars dim to 20%; knock on
   the table and they stay dim; pick it up and they brighten instantly. Leave
   it a few minutes and watch for heading drift (the clouds slowly turning).
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

IMU cost per sample on the board (budget 1224 us at 817 Hz): I2C read ~550
us, tracking ~100 us with Mahony and ~200 us with Kalman (see
[Orientation filters](#orientation-filters)).

Still to check, on a real strip: LED colors, signal quality at 4 MHz
without a level shifter (the 1 m prototype), and current draw.
