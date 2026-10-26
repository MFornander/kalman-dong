# Kalman Dong

A handheld, fur-covered LED staff (the "hippie lightsaber") that uses its own motion sensing to make the wielder feel like they are slicing through slowly drifting rainbow clouds hanging in the space around them.

## Language

### The device

**Dong**:
One complete handheld device: Hilt plus Blade.
_Avoid_: Saber, stick, node, unit

**Hilt**:
The end of the Dong that is held; houses the controller, display, buttons and battery.
_Avoid_: Handle, base, grip

**Blade**:
The 1–2 m fur-covered rod carrying the Lines.
_Avoid_: Rod, staff, shaft

**Line**:
One of the four rows of Pixels running the length of the Blade, each facing a different side (90° apart). Pixels within a Line are counted from the Hilt outward.
_Avoid_: Strand view, strip, side

**Strand**:
One physical chain of Pixels driven from one output of the controller. A Strand may fold back and forth to form several Lines.
_Avoid_: Strip, chain, output

**Mounting**:
How the IMU sits relative to the Blade; learned once per build by Mounting Calibration (tip up, then Line 0 to the sky) and kept across power cycles.
_Avoid_: Axis map, IMU orientation, calibration (on its own)

**Pixel**:
One addressable LED.
_Avoid_: LED (when meaning a position on the Blade)

**Dong Profile**:
The description of one physical build of a Dong — Blade length, Pixels per Line, how Strands fold into Lines, LED type and Power Budget — chosen when the firmware is built.
_Avoid_: Config, hardware variant, board

### The illusion

**Cloud Field**:
An invisible, slowly evolving 3D volume of rainbow clouds anchored to the space around the wielder; what each Pixel shows is the Cloud Field's color at that Pixel's position in space.
_Avoid_: Noise, pattern, effect

**Slice**:
What a single Line displays at an instant: the cross-section of the Cloud Field along that Line's current position in space.
_Avoid_: Frame (when meaning one Line's view)

**Girth**:
How far apart the Lines are placed within the Cloud Field, deliberately exaggerated beyond the Blade's real thickness so twisting the Dong rolls color around the Blade; adjustable by the wielder, and zero makes all Lines show the same Slice.
_Avoid_: Radius, spread, line offset

**Clear Sky**:
The thin regions between clouds in the Cloud Field, shown dimmer but still colored — never dark. How much of the Cloud Field is Clear Sky is adjustable.
_Avoid_: Gap, background, black

**Travel**:
How far the Dong has moved through the Cloud Field by deliberate level moves of the Hilt without rotating: each move is kept, at a little over half its size, once the Hilt comes to a stop. Cleared by Re-level. Local to each Dong.
_Avoid_: Position, translation, offset

**Push**:
A brief displacement of the Cloud Field relative to the Blade from moves that aren't kept as Travel (small jiggles, moving up or down, moves cut short by a swing), springing back to rest within about a second. Local to each Dong.
_Avoid_: Translation, offset, nudge

**Local Layer**:
Anything drawn on top of the Cloud Field that depends on one Dong's own history (such as Travel and Push) and therefore is never shared between Dongs.
_Avoid_: Overlay, effect

**Ignition**:
The power-on sequence: the Blade lights from the Hilt outward like an extending lightsaber, its progress showing how far the Dong has got in learning which way is down and its sensor drift, then fades into the Cloud Field.
_Avoid_: Boot animation, startup, calibration

**Palette**:
The set of colors the Cloud Field is drawn in; the wielder cycles between Palettes.
_Avoid_: Theme, color scheme, mode

**Re-level**:
The wielder's action of declaring the Dong's current heading as the reference for anchoring the Cloud Field, discarding accumulated heading drift. Tilt is never affected; gravity anchors it.
_Avoid_: Calibrate, reset, recenter, zero

**Rest**:
The Dong being held or laid still; used to quietly re-learn sensor drift, and after long enough, to dim the Pixels until the next motion.
_Avoid_: Idle, sleep, standby

**Show Clock**:
The single time base that drives all Cloud Field evolution; with Show Clock and orientation equal, two Dongs render identically. Later shared between Dongs for sync.
_Avoid_: Effect time, node time, millis

**Power Budget**:
The maximum current the Pixels may draw, set to match the battery pack; brightness is scaled down to stay within it.
_Avoid_: Brightness cap, current limit
