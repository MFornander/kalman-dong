# IMU options for the Dong

Research note, 2026-10-26. Question: which IMU (or IMU + controller) best
serves the Dong, either on the current board's Grove port or on a different
ESP32 board. Every number links to its source. Anything I worked out myself
rather than read is marked *(inferred)*. Spec values are datasheet
*typicals* unless marked max.

## Summary

- **Overall pick: keep the current board and add an ST LSM6DSV16X on the Grove
  port.** Use the SparkFun Qwiic breakout ([SEN-21325], $24.54) with a
  Grove↔Qwiic cable, and leave the onboard LSM6DS3TR-C powered down. Compared
  with today's chip it has:
  - about 8× less gyro bias drift with temperature: ±0.006 vs ±0.05 dps/°C
    ([dsv16x], [ds3trc]);
  - twice the gyro range: ±4000 vs ±2000 dps;
  - lower gyro noise: 2.8 vs 5 mdps/√Hz;
  - a better accelerometer: 60 vs 90 µg/√Hz noise, and offset drift of
    ±0.07 vs ±0.5 mg/°C;
  - SlimeVR's "Superior" tier, with 45–50 min before yaw needs a reset
    ([slime]).
- **ICM-45686 is the equal or slightly better chip.** It has ±0.005 dps/°C
  and ±0.3 dps initial offset ([icm45686]), and SlimeVR ranks it first. But
  there is no retail Grove/Qwiic module: the only breakout, SlimeVR's Mumo, is
  castellated and sold out ([mumo]). Choose it if a custom PCB is ever made.
- **Hold off on a magnetometer until a 6-axis upgrade has been measured.** Our
  measured heading drift at rest is already small: 0.07° over 30 s
  ([test/data/README.md](../../test/data/README.md)). The paper specs point
  to bias change with temperature and range clipping as the likely cause of
  drift in motion *(inferred)*, and a better gyro fixes both.
  - If drift remains, a magnetometer is a cheap add-on: MMC5603 at $5.95
    ([ada-mmc5603]), or BMM350 on a Grove unit at $6.50 ([m5-bmm350]).
  - It only helps if the LED supply and return run as a tightly paired wire
    and the sensor sits ≥ 10 cm from them; see
    [Magnetometer next to 3 A LED wiring](#magnetometer-next-to-3-a-led-wiring).
- **Architecture (b), a different controller, does not buy a better IMU.** No
  off-the-shelf ESP32 board found carries a top-tier IMU.
  - The best onboard option is the BMI270 on M5Stack boards. SlimeVR rates it
    "Poor", and its temperature drift is 2.5× the LSM6DSV16X's.
  - The AtomS3R is the only interesting board: it has a BMI270 + BMM150 on a
    private bus, which leaves Grove free for a better IMU. Moving to it would
    lose the 1.14" display, two of the three buttons, and battery charging.
- **Avoid BNO085/BNO086 on this ESP32-S3:**
  - gyro fixed at ±2000 dps;
  - fused output capped at 400 Hz;
  - needs I²C clock stretching, which Adafruit says does not work well on
    the ESP32-S3 ([ceva], [ada-stretch]);
  - SlimeVR lists it as discontinued and "Poor".

## Would a better IMU handle translation better?

Context: Travel is being developed on branch `dev-mf/travel` and is not
merged yet; its ADR there is `docs/adr/0002-orientation-only-anchoring.md`.
- Each deliberate stroke of the Hilt without rotation is integrated on its
  own.
- Strokes are gated off while turning faster than 1.5 rad/s.
- Linear acceleration under 0.4 m/s² is ignored.
- 60% of each stroke is kept, and only its level part.

The answer is: a little, but not much. Travel is limited mostly by how it is
computed, not by the sensor.

- **Tilt error leaking gravity into the strokes (biggest).**
  - Linear acceleration is the accelerometer reading minus gravity, rotated
    by the estimated orientation. Each 1° of tilt error leaves about
    0.17 m/s² of false acceleration.
  - The quiet-moment tilt error on the hand-sweep recording averages up to
    4.5° ([test/data/README.md](../../test/data/README.md)). That is about
    0.8 m/s², above the 0.4 m/s² stroke deadband. It is an overestimate,
    since that yardstick includes some hand acceleration.
  - A gyro with less bias drift and noise holds tilt better while the
    accelerometer is busy with the move. So the LSM6DSV16X should help here:
    real but modest *(inferred)*.
- **Accelerometer noise and offset drift barely matter.**
  - A stroke lasts well under 2 s and is measured on its own, so errors don't
    accumulate.
  - The LSM6DSV16X's accelerometer is better: offset drift of ±0.07 vs
    ±0.5 mg/°C ([dsv16x], [ds3trc]).
  - Even today's chip moves by only about 0.025 m/s² (2.5 mg) for a 5 °C
    change, far under the deadband.
- **The algorithm's choices dominate how it feels, on any IMU:**
  - the 0.4 m/s² deadband trims slow starts and stops;
  - only 60% of each stroke is kept;
  - strokes made while turning are discarded;
  - vertical moves spring back.
- **What would really change it:**
  - *Translation through swings:* most of what the Hilt's IMU feels during a
    swing is it circling the wrist. With the wrist-to-IMU lever arm known,
    the firmware could subtract ω×(ω×r) + α×r and keep measuring while
    turning. That works with any IMU; a quieter gyro makes α cleaner.
  - *Room-scale position* needs an outside reference (UWB anchors, a camera).
    Any IMU alone gives short strokes only, because position error grows with
    time squared.
- **Next step before buying hardware:** record 20 s of deliberate moves on the
  current board and replay it. That shows whether strokes are lost to tilt
  leakage (a better IMU helps) or to the gate and deadband (tuning).

  ```bash
  uv run tools/bringup.py record dong-strokes-20s --seconds 20
  ```

## What actually limits heading today

A 6-axis IMU cannot observe heading, so yaw is integrated gyro. The plausible
error sources, in the order I expect them to matter:

1. **Bias change with temperature between Rests** *(inferred)*.
   - LSM6DS3TR-C zero-rate offset drifts ±0.05 dps/°C typ ([ds3trc]).
   - A 5 °C warm-up after the last Rest (ESP32, regulator, LED driver heat)
     shifts the bias by 0.25 °/s, or 15° per minute of heading.
   - Rest re-learns bias (`MotionTracker`, τ = 2 s), so the error builds up
     only between Rests.
   - The LSM6DSV16X / ICM-45686 class are 8–10× better (±0.006 / ±0.005
     dps/°C).
   - The current driver does not read the chip's temperature, so this is
     untested.
2. **Gyro saturation in hard swings.**
   - Hand swings of the bare board reached 17 rad/s, about 975 dps
     ([test/data/README.md](../../test/data/README.md)).
   - Any sample past ±2000 dps (34.9 rad/s) is clipped, and the lost angle
     becomes permanent heading error. A swung 2 m Blade could plausibly get
     there *(inferred)*.
   - ±4000 dps (LSM6DSV16X, ISM330DHCX, ICM-45686) doubles the headroom.
3. **Scale-factor and cross-axis error during large rotations.**
   - These turn big back-and-forth swings into net heading change *(inferred)*.
   - The Kalman filter models 1% scale error
     ([orientation_filter.h](../../lib/core/src/kd/orientation_filter.h)).
   - Initial sensitivity tolerances were not gathered for this note. Change
     with temperature is similar across ST/TDK parts, about ±0.005–0.01 %/°C
     ([ds3trc], [dsv16x], [icm45686]).
4. **Noise (angle random walk) is negligible.**
   - 5 mdps/√Hz = 0.3°/√h, about 0.04° per minute *(inferred from [ds3trc])*.
   - Lower noise matters for Rest detection and bias learning, not for drift
     directly.

**Cheapest first check *(inferred)*:** add the chip temperature to the
recording format (LSM6DS3TR-C has an on-chip temperature sensor, [ds3trc]).
Record a 5-minute swing session with Re-level at the start and a return to
the same pose at the end, and see whether heading error tracks temperature.
That tells us whether the gyro upgrade alone will fix it.

## Comparison: 6-axis and fused IMUs

Abbreviations:
- ZRO = zero-rate offset, the gyro bias; "ZRO/°C" is its change with
  temperature.
- XL = accelerometer.
- "SlimeVR" = the tier on SlimeVR's IMU comparison ([slime], page last
  changed 2026-09-06 per [slime-src]), with its "time until yaw reset".

SlimeVR runs every IMU as 6-axis with its own host-side fusion (VQF), which is
the same situation as the Dong ([slime-drivers]). That makes its rankings
unusually relevant here.

| Chip | Gyro max | Max ODR | Gyro noise (mdps/√Hz) | ZRO / ZRO per °C (dps) | XL noise (µg/√Hz) / offset per °C | On-chip fusion | FIFO / timestamp | Interfaces, I²C addr | SlimeVR | Module, price | Source |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **LSM6DS3TR-C** (today) | ±2000 dps | 6.66 kHz | 5 | ±3 / **±0.05** | 90 (±4 g) / ±0.5 mg | none | 4 kB, 25 µs | I²C 400 kHz, SPI 10 MHz; 0x6A/6B | not listed | onboard; Adafruit 5543 (+LIS3MDL) $19.95 [ada-5543] | [ds3trc] |
| LSM6DSO | ±2000 | 6.66 kHz | 3.8 | ±1 / ±0.010 | 70–110 / ±0.1 mg | none (FSM, MLC) | 9 kB, 25 µs | I²C 400 kHz, I3C, SPI 10 MHz; 0x6A/6B | Legacy, 25–35 min | Adafruit 4517 (LSM6DSOX+LIS3MDL) $19.95 [ada-4517] | [dso] |
| **LSM6DSV16X** / LSM6DSV | **±4000** | 7.68 kHz | **2.8** | ±1 / **±0.006** | **60** / ±0.07 mg | **SFLP**: game rotation vector, gravity, gyro bias, 15–480 Hz | 4.5 kB, 21.75 µs | I²C Fm+ 1 MHz, I3C, SPI 10 MHz; 0x6A/6B | **Superior**, 45–50 min | **SparkFun Qwiic SEN-21325 $24.54** [SEN-21325]; chip ~$4.4 [flux] | [dsv16x]; LSM6DSV via [luxonis] (secondary) |
| LSM6DSV320X | ±4000 | 7.68 kHz | 3.8 | ±1 / ±0.006 | 60 (±16 g) + 320 g channel / ±0.07 mg | SFLP | 4.5 kB, 21.7 µs | I²C Fm+, I3C, SPI 10 MHz | not listed | none found | [dsv320x] |
| ISM330DHCX | ±4000 | 6.66 kHz | 5 (8 max) | ±1 / ±0.005 (±0.015 max) | 60 / ±0.1 mg | none | 9 kB, 25 µs | I²C Fm+, SPI 10 MHz; 0x6A/6B | not listed | SparkFun SEN-19895 (+MMC5983MA) $45.74 [SEN-19895] | [ism330] |
| LSM6DSR | ±4000 | 6.66 kHz *(inferred)* | 5 | ±1 / ±0.005 | – | none | 9 kB | as LSM6DSO *(inferred)* | Affordable, 25–30 min | none found | [dsr] (secondary) |
| **ICM-45686** | **±4000** | 6.4 kHz | 3.8 | **±0.3** / **±0.005** | 70 (±32 g max) / ±0.15 mg | none usable (EDMP, no documented quaternion) | 2–8 kB, timestamp in packet | I²C 1 MHz, I3C, SPI 24 MHz; 0x68/69 | **Superior, ranked 1st**, 45–60 min | SlimeVR Mumo (+QMC6309) $8, sold out, castellated [mumo] | [icm45686] |
| ICM-45605 | ±2000 | – | 3.8 | – / ±0.01 | 70 / ±0.15 mg | none | as 45686 *(inferred)* | as 45686 *(inferred)* | not listed | none found | [icm45605], [icm45605-range] |
| ICM-42688-P | ±2000 | 32 kHz | 2.8 | ±0.5 / ±0.005 (0–70 °C only) | 65–70 / ±0.15 mg | none (APEX) | 2 kB, timestamps | I²C 1 MHz, I3C, SPI 24 MHz; 0x68/69 | Poor ("temperature sensitive"), 25–30 min | Elecrow breakout [elecrow-42688] | [icm42688] |
| BMI270 | ±2000 | 6.4 kHz | 7 (perf.) | ±0.5 / ±0.015 | 160 / ±0.25 mg | none (config blob at boot) | 2 kB, 39 µs | I²C Fm+, SPI 10 MHz; 0x68/69 | Poor, 10–20 min | M5 6-DoF Unit $5.50 [m5-bmi270]; M5 9-DoF (+BMM350) $8.50 [m5-u229] | [bmi270] |
| BMI323 | ±2000 | 6.4 kHz | 7 | ±1 / ±0.04 | 180 / ±0.3 mg | none | 2 kB, timestamps | I²C 1 MHz, I3C, SPI 10 MHz | not listed | none found | [bmi323] |
| BNO085 / BNO086 | ±2000 (fixed) | fused 400 Hz; gyro RV 1 kHz | (Bosch die; not given) | – | ±8 g fixed | **full**: rotation vector, game RV, AR/VR-stabilized; game RV heading drift 0.5°/min dynamic (simulated) | – | I²C 400 kHz **with clock stretching**, SPI 3 MHz, UART; 0x4A/4B | Poor, 45–60 min; "discontinued", "stomp bug" | Adafruit 4754 $29.50 [ada-4754]; SparkFun BNO086 $34.95 [sf-bno086] | [ceva], [ada-stretch] |
| MPU6886 | ±2000 | 8 kHz | 4 | ±1 / ±0.01 | 100 / ±0.5–1 mg | none | 1 kB | I²C 400 kHz, SPI 10 MHz; 0x68/69 | not listed | M5 Unit $7.50 [m5-mpu6886] | [mpu6886] |
| QMI8658C | ±2048 | 8 kHz | 15 | ±10 / ±0.05 | 100–200 / ±0.5 mg | AttitudeEngine, 1–64 Hz only | 1.5 kB | I²C 400 kHz, I3C, SPI 15 MHz; 0x6A/6B | not listed | Waveshare boards only | [qmi8658] |

Notes on the table:

- **In-run bias instability (Allan deviation) is not published** for any of
  these chips. Luxonis lists a measured "gyro bias stability" of about 2.0
  with no units for the LSM6DSV ([luxonis], secondary). ZRO per °C is the
  closest published proxy.
- **ST's own figures for LSM6DSV16X SFLP:**
  - heading drift: 0.5°/5 min static, 0.7°/5 min low-dynamic, 5.9°/5 min
    high-dynamic;
  - pitch/roll error: 1.5° static, 1.6°/1.2° high-dynamic ([dsv16x],
    Table 1).

  SFLP knows nothing about the Dong's Rest-based bias re-learning, so treat
  it as a reference to compare against, not a replacement for our filter
  *(inferred)*.
- **BNO086** adds "Interactive Calibration" for lower heading drift ([ceva]).
  It does not remove the ±2000 dps cap or the clock stretching.
- **External clocks:** ICM-45686 and ICM-42688-P accept one, which reduces
  rate and scale error. SlimeVR feeds the ICM-45686 a 32.768 kHz clock
  ([icm45686], [slime-drivers]). Neither Grove nor Qwiic carries a clock pin.

## Magnetometers

| Chip | Range | RMS noise | Max ODR | Offset vs temp | I²C addr, interfaces | Supply | Module, price | Source |
|---|---|---|---|---|---|---|---|---|
| **MMC5603NJ** | ±30 G (±3000 µT) | 1.5 mG (0.15 µT) at 75 Hz BW | 1 kHz | 0.2 mG/°C typ; SET/RESET | 0x30; I²C 400 kHz, I3C | 1.62–3.6 V | Adafruit 5579, STEMMA QT, **$5.95** [ada-mmc5603] | [mmc5603] |
| **MMC5983MA** | ±8 G | **0.4 mG (40 nT)** | 1 kHz | 3 mG over −40…105 °C with SET/RESET | 0x30; I²C 400 kHz, **SPI 10 MHz** | 2.8–3.6 V | SparkFun SEN-19921 Qwiic $18.50 [sf-mmc5983] | [mmc5983] |
| **BMM350** | ±2000 µT | 0.19 µT xy / 0.45 µT z (100 Hz, avg 2) | 400 Hz | ±200 nT/K | 0x14/15; I²C Fm+, I3C | **core 1.72–1.98 V** | M5 Unit (Grove) $6.50 [m5-bmm350]; in M5 9-DoF U229 $8.50 [m5-u229] | [bmm350] |
| BMM150 | ±1300 µT xy | 0.3 µT (20 Hz preset) | 20 Hz normal | TCS ±0.01 %/K | 0x10–13; I²C 400 kHz, SPI | 1.62–3.6 V | Seeed Grove Compass v2 ~$11.80 [ss-bmm150] (secondary); in AtomS3R | [bmm150] |
| LIS2MDL | ±49 G | 3 mG | 100 Hz (150 single) | ±0.3 mG/°C | 0x1E; I²C up to 3.4 MHz, SPI | 1.71–3.6 V | Adafruit 4488 $7.95 [ada-lis2mdl] | [lis2mdl] |
| LIS3MDL | ±4–16 G | 3.2 mG xy (UHP) | 155 Hz UHP | not published | 0x1C/1E; I²C 400 kHz, SPI | 1.9–3.6 V | Adafruit 4479 $9.95 [ada-lis3mdl] | [lis3mdl] |
| QMC5883P | ±2–30 G | 2 mG xy | 1.5 kHz | ±0.05 %/°C (sensitivity) | 0x2C; I²C 400 kHz | 2.5–3.6 V | Adafruit 6388 $5.95 [ada-qmc5883p] | [qmc5883p] |
| RM3100 | ±800 µT | **15 nT** | 147 Hz | "free from offset drift" (claim) | I²C, SPI | – | PNI breakout $25 [rm3100] | [rm3100-ds] |

Reading the table:

- **Noise is not the constraint *(inferred)*.** A 1° heading error needs a
  field error of about tan(1°) × B_horizontal. For a 20 µT horizontal field
  that is 0.35 µT. Earth's horizontal component ranges up to 42 µT ([an4247]),
  so 20 µT is a mid-range value I assumed. Every chip above except BMM150 at
  low-power settings is below 0.35 µT RMS before averaging.
- **What separates them is ODR, 3.3 V operation, offset stability and
  module form.** BMM350 and AK0991x need a 1.8 V core, so they only make sense
  on a breakout with a regulator.

### Magnetometer next to 3 A LED wiring

- **A single conductor** produces B = μ0·I/(2πr) ([openstax]); NXP's form is
  B = 0.2·I/r µT with r in m ([an4247]).
- **A send/return pair spaced s apart** falls off as about 0.2·I·s/r²
  *(inferred, dipole approximation)*. Twisting the pair lowers it further
  ([kvh-patent], no closed form).

| Distance r | Single 3 A wire | 3 A pair, s = 2 mm *(inferred)* |
|---|---|---|
| 1 cm | 60 µT | 12 µT |
| 3 cm | 20 µT | 1.3 µT |
| 5 cm | 12 µT | 0.48 µT |
| 10 cm | 6 µT | 0.12 µT |
| 30 cm | 2 µT | 0.013 µT |

Earth's total field is 22–67 µT ([bgs]).

**Consequences:**
- **A lone LED supply wire within tens of cm ruins heading.** Keeping a single
  wire under 0.35 µT would need about 1.7 m of separation *(inferred)*.
- **A tight pair at ≥ 10 cm is fine** *(inferred)*.
- **Changing current cannot be calibrated out.** Fields from constant
  currents act like hard iron and can be calibrated. Fields that change with
  current, here LED brightness, cannot ([nxp-blog]).
- **Fast changes average out, slow ones do not.** Current changes faster than
  the magnetometer's sample rate average out ([an4247]), so SK9822 PWM is
  harmless and Cloud Field brightness swings are not *(inferred)*.
- **Batteries and steel parts distort the field.** Battery cells act as soft
  iron ([an4247]), and steel distorts the field 15–30 cm away ([slime]). Keep
  steel-can cells away from the sensor.
- **What the Dong would need *(inferred)*:**
  - run the LED supply and return twisted together from the battery to the
    Blade;
  - put the magnetometer on a short cable at the end of the Hilt farthest
    from the battery and the Blade feed;
  - gate magnetometer corrections on |B| and dip angle staying near their
    learned values.
- **Optional compensation *(inferred)*:** `PowerGovernor::milliamps()`
  already estimates LED current every frame, so the residual field vs
  current could be regressed and subtracted.
- **SlimeVR's experience:**
  - Its trackers ship 6-axis by default even when a QMC6309 is fitted, and
    the magnetometer is an opt-in toggle ([slime], [crowdsupply-mag],
    secondary).
  - SlimeVR's guidance is to check the room first: field fluctuation of
    ≤ 5 µT is a good baseline ([slime]).
  - CEVA's guidance for the BNO08x is the same: in unstable fields use the
    6-axis game rotation vector ([ceva]).

## Architecture (a): current board + IMU on Grove

The I²C bus is the constraint. Grove is the onboard IMU's bus (GPIO5/6), and
the ESP32-S3 I²C master is specified only up to 400 kHz, even though the
newer IMUs support 1 MHz ([esp-idf-i2c]). Today each 12-byte read takes about
550 µs, so 833 Hz keeps the bus about 45% busy
([README](../../README.md), IMU cost per sample).

Options:

1. **Use the external IMU instead of the onboard one.**
   - Leave the LSM6DS3TR-C in its power-down reset state; nothing needs to be
     configured *(inferred: its ODR registers default to power-down,
     [ds3trc])*.
   - Bus load for the LSM6DSV16X at its nearest rate, 960 Hz (its ODR steps halve down from 7.68 kHz, [dsv16x]): 960 × 550 µs ≈
     53% *(inferred)*.
   - The time budget per sample drops from 1224 µs to 1042 µs. A 550 µs read
     plus Kalman at about 200 µs still fits.
2. **Run both IMUs on one bus at full rate.** This needs about 100% of the
   bus at 400 kHz, so it is out *(inferred)*.
3. **Batch reads from the FIFO.**
   - Raw wire time does not shrink: 14 bytes per gyro+accel pair in the
     LSM6DSV16X FIFO costs about 315 µs per sample at 400 kHz *(inferred)*.
   - The ~200 µs of per-transaction overhead does shrink.
   - The chip's timestamps give exact dt.
   - The cost is up to one batch of added latency, for example 4 samples ≈
     4 ms *(inferred)*.
4. **Use a dedicated second bus.**
   - The S3 has two I²C controllers ([esp-idf-i2c]).
   - The free pads GPIO40/42 ([profiles.h](../../src/profiles.h)) could carry
     a soldered Qwiic pigtail as `Wire1`.
   - That gives the new IMU its own bus and keeps the onboard one usable as a
     reference.
   - It costs the pins the README reserves for a second Strand.

Other constraints:

- **No interrupt line.** Grove/Qwiic have only GND, VCC, SDA and SCL, so
  there is no data-ready line.
  - Without one, the IMU task polls, which costs extra bus transactions
    (`imu.dataReady()` in [main.cpp](../../src/main.cpp)).
  - Either solder the module's INT1 to GPIO40 or GPIO42, or pace FIFO batch
    reads from a timer and rely on the chip's timestamps *(inferred)*.
- **Address conflict.** Both ST parts answer at 0x6A/0x6B.
  - The onboard chip's address is not documented; the driver probes 0x6A
    first. An I²C scan settles it.
  - Set the breakout's address jumper to the other address.
  - ICM-45686 (0x68/69), MMC5603/MMC5983MA (0x30) and BMM350 (0x14/15) do not
    collide with it.
- **Pull-ups.** The breakout's pull-ups end up in parallel with the
  carrier's. SparkFun boards use 2.2 kΩ on a cut jumper ([sf-bno086]); for
  SEN-21325 that is assumed, not checked. 2.2 kΩ ∥ 5.1 kΩ ≈ 1.5 kΩ, which is
  fine at 3.3 V and 400 kHz *(inferred)*.
- **Cable.** The Grove (2.0 mm) to Qwiic (1.0 mm JST-SH) cable should stay
  short. Mount the module rigidly to the Hilt frame, not hanging on the cable;
  Mounting Calibration then handles its axes.

**Recommendation for (a):**
- LSM6DSV16X ([SEN-21325]) on Grove, onboard IMU powered down, at 960 Hz and
  ±4000 dps.
- Solder INT1 to GPIO40 if a second Strand is not planned, or else read the
  FIFO in batches.
- Add an MMC5603 ([ada-mmc5603], 0x30) only if the 6-axis upgrade still drifts.
- If a custom Hilt PCB is ever made: ICM-45686 over SPI with a 32 kHz clock,
  plus an MMC5983MA.

## Architecture (b): a different controller with a better onboard IMU

Requirements: ESP32-S3, two GPIOs for SK9822 data and clock (four for a second
Strand), three buttons, and I²C for the IMU. A display and battery charging are
nice to have.

| Board | IMU / mag | IMU bus, interrupt | Display | Buttons | Free GPIOs | Battery | Price | Source |
|---|---|---|---|---|---|---|---|---|
| **Current**: XIAO ESP32-S3 Plus + 1.14" display | LSM6DS3TR-C (per CNX "LSM6DS3"; Seeed does not name it) / – | shared with Grove; INT1 GPIO41 | 1.14" ST7789 | 3 | 38/39/40/42 pads | JST LiPo | ~$16–20 | [seeed-114], [cnx-114] |
| **M5Stack AtomS3R** | BMI270 / BMM150 (on BMI270 aux bus) | **private bus** GPIO0/45, 2.2 kΩ; INT1 → GPIO16 *(from schematic text, verify)* | 0.85" 128×128 | 1 | 8 (Grove G1/G2 + G5–8, G38, G39) | **none** | $17.50 ($14.50 no screen) | [atoms3r], [atoms3r-sch], [m5unified] |
| M5Stack M5StickS3 | BMI270 / – | internal bus shared with codec + PMIC; INT to PMIC, not ESP32 | 1.14" ST7789P3 | 2 | Grove G9/G10 + Hat2 G1–8, G43, G44 | 250 mAh + charger | $21.50 | [sticks3] |
| M5Stack Capsule | BMI270 (0x69) / – | G10/G8 | none | 2 (WAKE, G0) + reset | Grove + 7 | 250 mAh | $19.95 | [capsule] |
| M5Stack AtomS3 Lite (+ external IMU) | – / – | Grove free | RGB LED | 1 | 8 | none | $7.50 | [atoms3lite] |
| Waveshare ESP32-S3-Touch-LCD-1.69 | QMI8658C / – | – | 1.69" touch | 1 + BOOT/RST | 4 pads + I²C | charger | $20.49–21.99 | [ws-169] |
| Seeed XIAO (any) | ESP32 variants have **no IMU**; nRF52840/MG24/nRF54L15 Sense use LSM6DS3TR-C (same chip as today) | – | – | – | – | – | – | [xiao-intro] |

Older-ESP32 boards (Atom Matrix, M5StickC Plus2: MPU6886) and the LilyGO
T-Watch S3 (BMA423 accelerometer only) are excluded ([m5unified],
[sticks3-plus2], [cnx-twatch]).

The board survey found **no off-the-shelf ESP32 board with an ICM-42688,
ICM-45686, LSM6DSV16X or BNO085 onboard**. Only custom carriers and breakouts
turned up ([madflight], [xiao-bno085]). The best onboard IMU on offer is the
BMI270:

- **Better than today's chip:** ±0.015 vs ±0.05 dps/°C ([bmi270], [ds3trc]).
- **Worse than the LSM6DSV16X:**
  - 2.5× the temperature drift;
  - half the gyro range;
  - 2.7× the accelerometer noise;
  - SlimeVR "Poor", 10–20 min before yaw needs a reset ([slime]).

**Recommendation for (b):** only worth it if the controller is being
replaced anyway.

- **AtomS3R** is the one that adds something:
  - a magnetometer on a private bus;
  - an IMU interrupt line;
  - Grove free for an external LSM6DSV16X;
  - PSRAM.
- **Its costs:**
  - two buttons have to be added on GPIOs;
  - it has no battery charger;
  - its display is 0.85".
- In effect it becomes (a) on a different host *(inferred)*.
- **M5StickS3** is the closest drop-in for size, display and battery. But it
  has a mid-tier IMU whose interrupt does not reach the ESP32, and no
  magnetometer.

## Firmware changes

### Driver (`src/imu_lsm6.*`, `src/main.cpp`)

1. **Add a driver alongside the current one.** Add `ImuLsm6dsv`, or turn
   `ImuLsm6` into a small `Imu` interface (`begin`, `dataReady`/`read`,
   `chipName`, `rateHz`) with one class per family. Then `main.cpp` stops
   naming `ImuLsm6::Reading` and `ImuLsm6::kRateHz` directly.
   - The LSM6DSV16X register map differs from the DS3 family: WHO_AM_I, ODR
     codes, and FS bits in CTRL6/CTRL8. Take values from [dsv16x]. Do not
     extend `chipFor()`, because the current driver only uses registers
     shared by DS3/DS3TR-C/DSO.
   - Scale factors change for ±4000 dps.
2. **Change the rate from 833 Hz to 960 Hz.**
   - The tracker takes per-sample `dt` ([motion_tracker.h](../../lib/core/src/kd/motion_tracker.h)),
     so the core needs no change.
   - The recording header prints `rate=`.
   - Tests hard-code 833 as a sample count (`test_motion`, `test_mounting`,
     `test_replay`), which still works *(inferred)*.
3. **Pick the bus.**
   - Grove: `Wire` on GPIO5/6, with the onboard chip left in reset.
   - Dedicated: `Wire1` on GPIO40/42.
   - Keep the bus-recovery code that already handles a held SDA.
4. **Add the interrupt or FIFO path.**
   - With INT1 wired: the same semaphore path, on the new pin.
   - Without it: timer-paced FIFO batch reads, using chip timestamps for `dt`.
5. **Read the chip temperature into `ImuSample`.** It goes into the
   recordings and allows a bias-vs-temperature model in `MotionTracker`. This
   is worth doing on the current LSM6DS3TR-C first (see
   [What actually limits heading today](#what-actually-limits-heading-today)).
6. **Re-run Mounting Calibration** for the new sensor's axes, and record new
   `test/data/` sessions with it so the replay report compares chips.

### `OrientationFilter` interface (`lib/core/src/kd/orientation_filter.h`)

Add the magnetometer as a separate, optional call rather than widening
`update()`. It arrives at 75–1000 Hz, not at the gyro rate, and most filters
and tests do not use it:

```cpp
// Magnetic field, body frame (µT), hard/soft-iron corrected. Filters that
// cannot use it ignore it.
virtual void correctHeading(const Vec3& mag) {}
```

`FilterBank`, `MotionTracker::setFilter` and the existing tests are unchanged.
`MotionTracker` decides when a magnetometer reading is trustworthy (|B| and
dip angle near their learned values) and forwards only those. It also keeps
ownership of Re-level: with a magnetometer, Re-level would store a heading
offset relative to magnetic north instead of a free reference *(inferred)*.

An on-chip fused quaternion (LSM6DSV16X SFLP) does not fit
`update(gyro, accel, dt)`. If wanted for comparison, add it as an optional
field on `ImuSample` and a pass-through filter that reads it, so it shows in
the replay report.

### Adding the magnetometer to Mahony and Kalman

- **Mahony:** add the standard magnetometer error term, applied about world
  Z only.
  1. Rotate the measured field to world.
  2. Take its horizontal part.
  3. Compute the heading error against the stored reference direction.
  4. Feed the error into `omega` projected onto the body-frame image of world
     Z, so magnetic disturbances can never tilt the Cloud Field.

  Use its own small gain, with a time constant of tens of seconds
  *(inferred)*, so a brief LED-current spike moves heading only fractionally.
- **Kalman:** add a 1-dof heading measurement with its own noise and an
  innovation gate (χ², 1 dof). This makes the vertical gyro bias observable,
  which the class comment says it currently is not.
- **Calibration and storage:**
  - Hard-iron offset (3 values) and soft-iron matrix are learned from the
    field sphere traced while the Dong is waved. Ignition sweeps may be
    enough *(inferred)*.
  - Store them alongside the gyro bias (`store.cpp`).
  - Recordings gain `mx,my,mz` columns, and the replay test gains a
    "heading drift in motion" measure.

## Sources

Bracketed names in the text link to these.

- `ds3trc`: <https://cdn-shop.adafruit.com/product-files/4503/4503_LSM6DS3TR-C_datasheet.pdf>
- `dso`: <https://cdn.sparkfun.com/assets/2/3/c/6/5/lsm6dso.pdf>
- `dsv16x`: <https://cdn.sparkfun.com/assets/4/1/8/e/2/lsm6dsv16x.pdf>
- `luxonis`: <https://docs.luxonis.com/hardware/platform/sensors/imu/lsm6dsv.md>
- `dsv320x`: <https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6722/LSM6DSV320XTR.pdf>
- `ism330`: <https://cdn-learn.adafruit.com/assets/assets/000/092/508/original/ism330dhcx.pdf>
- `dsr`: <https://www.alldatasheet.net/html-pdf/1266584/STMICROELECTRONICS/LSM6DSR/20459/9/LSM6DSR.html>
- `icm45686`: <https://file01.andesource.com/dkpdf/ICM-45686_TDK001.pdf>
- `icm45605`: <https://invensense.tdk.com/products/motion-tracking/6-axis/icm-45605/>
- `icm45605-range`: <https://il.farnell.com/tdk/icm-45605/mems-module-6-axis-1-71v-3-6v/dp/4572493>
- `icm42688`: <https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/495/ICM-42688-P_DS_Rev1.2.pdf>
- `bmi270`: <https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf>
- `bmi323`: <https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi323-ds000.pdf>
- `ceva`: <https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf>
- `ada-stretch`: <https://learn.adafruit.com/i2c-addresses/troublesome-chips>
- `mpu6886`: <https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/MPU-6886-000193%2Bv1.1_GHIC_en.pdf>
- `qmi8658`: <https://files.waveshare.com/upload/5/5f/QMI8658C.pdf>
- `flux`: <https://www.flux.ai/jecstronic/lsm6dsv16xtr.md>
- `slime`: <https://docs.slimevr.dev/diy/imu-comparison.html>
- `slime-src`: <https://github.com/SlimeVR/SlimeVR-Docs-Site/blob/main/src/diy/imu-comparison.md>
- `slime-drivers`: <https://github.com/SlimeVR/SlimeVR-Tracker-ESP/tree/main/src/sensors/softfusion/drivers>
- `crowdsupply-mag`: <https://www.crowdsupply.com/slimevr/slimevr-full-body-tracker/updates/another-shipment-and-some-new-features>
- `mmc5603`: <https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5603NJDatasheetRev.B.pdf>
- `mmc5983`: <https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5983MADatasheetRevA.pdf>
- `bmm350`: <https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmm350-ds001.pdf>
- `bmm150`: <https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmm150-ds001.pdf>
- `lis2mdl`: <https://cdn.sparkfun.com/assets/d/b/f/b/3/lis2mdl.pdf>
- `lis3mdl`: <https://www.pololu.com/file/0J1089/LIS3MDL.pdf>
- `qmc5883p`: <https://cdn-shop.adafruit.com/product-files/6388/C2847467.pdf>
- `rm3100-ds`: <https://www.digimax.it/media_import/COMPONENTI/PNI/SENSORI%20MAGNETICI/14190/14190_DS_001.pdf>
- `rm3100`: <https://www.pnisensor.com/rm3100/>
- `openstax`: <https://openstax.org/books/university-physics-volume-2/pages/12-2-magnetic-field-due-to-a-thin-straight-wire>
- `an4247`: <https://www.nxp.com/docs/en/application-note/AN4247.pdf>
- `nxp-blog`: <https://www.nxp.com/company/blog/magnetometer-placement-where-and-why:BL-MAGNETOMETER-PLACEMENT-WHERE-WHY>
- `kvh-patent`: <https://patents.google.com/patent/US5126666A/en>
- `bgs`: <https://geomag.bgs.ac.uk/education/earthmag.html>
- `esp-idf-i2c`: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2c.html>
- `SEN-21325`: <https://www.sparkfun.com/sparkfun-6dof-imu-breakout-lsm6dsv16x-qwiic.html>
- `SEN-19895`: <https://www.sparkfun.com/sparkfun-9dof-imu-breakout-ism330dhcx-mmc5983ma-qwiic.html>
- `sf-mmc5983`: <https://www.sparkfun.com/sparkfun-micro-magnetometer-mmc5983ma-qwiic.html>
- `sf-bno086`: <https://www.sparkfun.com/sparkfun-vr-imu-breakout-bno086-qwiic.html>
- `ada-4754`: <https://www.adafruit.com/product/4754>
- `ada-4517`: <https://www.adafruit.com/product/4517>
- `ada-5543`: <https://www.adafruit.com/product/5543>
- `ada-mmc5603`: <https://www.adafruit.com/product/5579>
- `ada-lis2mdl`: <https://www.adafruit.com/product/4488>
- `ada-lis3mdl`: <https://www.adafruit.com/product/4479>
- `ada-qmc5883p`: <https://www.adafruit.com/product/6388>
- `m5-bmi270`: <https://shop.m5stack.com/products/6-dof-imu-unit-bmi270>
- `m5-u229`: <https://shop.m5stack.com/products/9-dof-imu-unit-bmi270-bmm350>
- `m5-bmm350`: <https://shop.m5stack.com/products/3-axis-magnetometer-unit-bmm350>
- `m5-mpu6886`: <https://shop.m5stack.com/products/6-axis-imu-unitmpu6886>
- `ss-bmm150`: <https://www.switch-science.com/products/3932>
- `mumo`: <https://shop.slimevr.dev/products/slimevr-mumo-breakout-module-v1-icm-45686-qmc6309>
- `elecrow-42688`: <https://www.elecrow.com/icm-42688-p-ultra-low-power-imu-breakout.html>
- `atoms3r`: <https://docs.m5stack.com/en/core/AtomS3R>
- `atoms3r-sch`: <https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/680/Sch_M5_AtomS3R_v0.4.1.pdf>
- `m5unified`: <https://raw.githubusercontent.com/m5stack/M5Unified/master/src/M5Unified.inl>
- `sticks3`: <https://docs.m5stack.com/en/core/StickS3>
- `sticks3-plus2`: <https://docs.m5stack.com/en/core/M5StickC%20PLUS2>
- `capsule`: <https://docs.m5stack.com/en/core/M5Capsule>
- `atoms3lite`: <https://docs.m5stack.com/en/core/AtomS3%20Lite>
- `ws-169`: <https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69>
- `xiao-intro`: <https://wiki.seeedstudio.com/SeeedStudio_XIAO_Series_Introduction/>
- `seeed-114`: <https://wiki.seeedstudio.com/getting_started_1.14_inch_display_esp32s3>
- `cnx-114`: <https://www.cnx-software.com/2026/10/17/seeed-studio-xiao-ips-display-series-features-esp32-s3-or-nrf52840-soc-with-0-96-inch-to-1-47-inch-lcd-options/>
- `cnx-twatch`: <https://www.cnx-software.com/2023/06/14/t-watch-s3-esp32-s3-smartwatch-supports-wifi-bluetooth-le-and-lora-connectivity/>
- `madflight`: <https://madflight.com/Sensor-Boards/>
- `xiao-bno085`: <https://github.com/weixinzhou/esp32s3-bno085-imu>

**Caveats:**
- **Read from mirrors:** st.com blocked direct fetches, so the ST datasheets
  were read from distributor or maker mirrors (document number and revision
  are in each PDF).
- **Prices** are vendor list prices as of 2026-10-26.
- **Not verified:** the AtomS3R IMU interrupt pin (GPIO16) comes from a text
  extraction of the schematic. The onboard IMU's I²C address is not
  documented by Seeed.

<!-- link definitions -->
[ds3trc]: https://cdn-shop.adafruit.com/product-files/4503/4503_LSM6DS3TR-C_datasheet.pdf
[dso]: https://cdn.sparkfun.com/assets/2/3/c/6/5/lsm6dso.pdf
[dsv16x]: https://cdn.sparkfun.com/assets/4/1/8/e/2/lsm6dsv16x.pdf
[luxonis]: https://docs.luxonis.com/hardware/platform/sensors/imu/lsm6dsv.md
[dsv320x]: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6722/LSM6DSV320XTR.pdf
[ism330]: https://cdn-learn.adafruit.com/assets/assets/000/092/508/original/ism330dhcx.pdf
[dsr]: https://www.alldatasheet.net/html-pdf/1266584/STMICROELECTRONICS/LSM6DSR/20459/9/LSM6DSR.html
[icm45686]: https://file01.andesource.com/dkpdf/ICM-45686_TDK001.pdf
[icm45605]: https://invensense.tdk.com/products/motion-tracking/6-axis/icm-45605/
[icm45605-range]: https://il.farnell.com/tdk/icm-45605/mems-module-6-axis-1-71v-3-6v/dp/4572493
[icm42688]: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/495/ICM-42688-P_DS_Rev1.2.pdf
[bmi270]: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf
[bmi323]: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi323-ds000.pdf
[ceva]: https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf
[ada-stretch]: https://learn.adafruit.com/i2c-addresses/troublesome-chips
[mpu6886]: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/MPU-6886-000193%2Bv1.1_GHIC_en.pdf
[qmi8658]: https://files.waveshare.com/upload/5/5f/QMI8658C.pdf
[flux]: https://www.flux.ai/jecstronic/lsm6dsv16xtr.md
[slime]: https://docs.slimevr.dev/diy/imu-comparison.html
[slime-src]: https://github.com/SlimeVR/SlimeVR-Docs-Site/blob/main/src/diy/imu-comparison.md
[slime-drivers]: https://github.com/SlimeVR/SlimeVR-Tracker-ESP/tree/main/src/sensors/softfusion/drivers
[crowdsupply-mag]: https://www.crowdsupply.com/slimevr/slimevr-full-body-tracker/updates/another-shipment-and-some-new-features
[mmc5603]: https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5603NJDatasheetRev.B.pdf
[mmc5983]: https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5983MADatasheetRevA.pdf
[bmm350]: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmm350-ds001.pdf
[bmm150]: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmm150-ds001.pdf
[lis2mdl]: https://cdn.sparkfun.com/assets/d/b/f/b/3/lis2mdl.pdf
[lis3mdl]: https://www.pololu.com/file/0J1089/LIS3MDL.pdf
[qmc5883p]: https://cdn-shop.adafruit.com/product-files/6388/C2847467.pdf
[rm3100-ds]: https://www.digimax.it/media_import/COMPONENTI/PNI/SENSORI%20MAGNETICI/14190/14190_DS_001.pdf
[rm3100]: https://www.pnisensor.com/rm3100/
[openstax]: https://openstax.org/books/university-physics-volume-2/pages/12-2-magnetic-field-due-to-a-thin-straight-wire
[an4247]: https://www.nxp.com/docs/en/application-note/AN4247.pdf
[nxp-blog]: https://www.nxp.com/company/blog/magnetometer-placement-where-and-why:BL-MAGNETOMETER-PLACEMENT-WHERE-WHY
[kvh-patent]: https://patents.google.com/patent/US5126666A/en
[bgs]: https://geomag.bgs.ac.uk/education/earthmag.html
[esp-idf-i2c]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2c.html
[SEN-21325]: https://www.sparkfun.com/sparkfun-6dof-imu-breakout-lsm6dsv16x-qwiic.html
[SEN-19895]: https://www.sparkfun.com/sparkfun-9dof-imu-breakout-ism330dhcx-mmc5983ma-qwiic.html
[sf-mmc5983]: https://www.sparkfun.com/sparkfun-micro-magnetometer-mmc5983ma-qwiic.html
[sf-bno086]: https://www.sparkfun.com/sparkfun-vr-imu-breakout-bno086-qwiic.html
[ada-4754]: https://www.adafruit.com/product/4754
[ada-4517]: https://www.adafruit.com/product/4517
[ada-5543]: https://www.adafruit.com/product/5543
[ada-mmc5603]: https://www.adafruit.com/product/5579
[ada-lis2mdl]: https://www.adafruit.com/product/4488
[ada-lis3mdl]: https://www.adafruit.com/product/4479
[ada-qmc5883p]: https://www.adafruit.com/product/6388
[m5-bmi270]: https://shop.m5stack.com/products/6-dof-imu-unit-bmi270
[m5-u229]: https://shop.m5stack.com/products/9-dof-imu-unit-bmi270-bmm350
[m5-bmm350]: https://shop.m5stack.com/products/3-axis-magnetometer-unit-bmm350
[m5-mpu6886]: https://shop.m5stack.com/products/6-axis-imu-unitmpu6886
[ss-bmm150]: https://www.switch-science.com/products/3932
[mumo]: https://shop.slimevr.dev/products/slimevr-mumo-breakout-module-v1-icm-45686-qmc6309
[elecrow-42688]: https://www.elecrow.com/icm-42688-p-ultra-low-power-imu-breakout.html
[atoms3r]: https://docs.m5stack.com/en/core/AtomS3R
[atoms3r-sch]: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/680/Sch_M5_AtomS3R_v0.4.1.pdf
[m5unified]: https://raw.githubusercontent.com/m5stack/M5Unified/master/src/M5Unified.inl
[sticks3]: https://docs.m5stack.com/en/core/StickS3
[sticks3-plus2]: https://docs.m5stack.com/en/core/M5StickC%20PLUS2
[capsule]: https://docs.m5stack.com/en/core/M5Capsule
[atoms3lite]: https://docs.m5stack.com/en/core/AtomS3%20Lite
[ws-169]: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69
[xiao-intro]: https://wiki.seeedstudio.com/SeeedStudio_XIAO_Series_Introduction/
[seeed-114]: https://wiki.seeedstudio.com/getting_started_1.14_inch_display_esp32s3
[cnx-114]: https://www.cnx-software.com/2026/10/17/seeed-studio-xiao-ips-display-series-features-esp32-s3-or-nrf52840-soc-with-0-96-inch-to-1-47-inch-lcd-options/
[cnx-twatch]: https://www.cnx-software.com/2023/06/14/t-watch-s3-esp32-s3-smartwatch-supports-wifi-bluetooth-le-and-lora-connectivity/
[madflight]: https://madflight.com/Sensor-Boards/
[xiao-bno085]: https://github.com/weixinzhou/esp32s3-bno085-imu
