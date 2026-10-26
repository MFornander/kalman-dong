// 3D gradient noise (Ken Perlin's "improved noise"). Our own implementation
// so the Cloud Field is host-testable without FastLED: see
// docs/adr/0001-own-noise-not-fastled.md.
#pragma once

namespace kd {

// Period of the noise lattice: noise3(p) == noise3(p + kNoisePeriod * axis).
// Callers can wrap large coordinates (such as time offsets) modulo this
// without a visible seam.
constexpr float kNoisePeriod = 256.f;

// Smooth noise in roughly [-1, 1], 0 at integer lattice points.
float noise3(float x, float y, float z);

// Fractal sum of `octaves` noise layers (each half the size and half the
// strength of the previous), normalized back to roughly [-1, 1].
float fbm3(float x, float y, float z, int octaves);

}  // namespace kd
