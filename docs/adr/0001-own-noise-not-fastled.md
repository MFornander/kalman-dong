# Own Cloud Field noise instead of FastLED's

The Cloud Field uses our own small 3D Perlin noise and color code in the pure-C++ core library, not FastLED's `inoise16`/palette functions, even though FastLED is already a dependency. FastLED's native (host) build is heavy and fast-moving, and we want the entire Cloud Field — the heart of the illusion — unit-testable on a PC without it. FastLED is used only on the Dong, to push Pixels out to the Strands.

## Considered Options

- **FastLED noise everywhere** — less code, but ties every host test to building FastLED's `stub` platform, which needed unverified `lib_compat_mode` tweaks and changes release to release.
- **Own noise** — chosen: a float port of Ken Perlin's reference "improved noise" (~100 lines), deterministic, and periodic every 256 units so Show Clock offsets can wrap without a seam. FastLED's `inoise16` is the same algorithm in fixed point, so its MIT-licensed code remains a fallback if float cost ever matters on the Dong.
