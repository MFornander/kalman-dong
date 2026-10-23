// Pixel colors and Palettes.
#pragma once

#include <cstddef>
#include <cstdint>

namespace kd {

struct Rgb8 {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb8& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb8& o) const { return !(*this == o); }
};

// Linear float color, nominally 0..1 per channel.
struct Rgbf {
    float r = 0, g = 0, b = 0;
    Rgbf operator*(float s) const { return {r * s, g * s, b * s}; }
    Rgbf operator+(const Rgbf& o) const { return {r + o.r, g + o.g, b + o.b}; }
};

Rgb8 toRgb8(const Rgbf& c);
Rgbf toRgbf(const Rgb8& c);
Rgbf mix(const Rgbf& a, const Rgbf& b, float t);

// A Palette: 16 evenly spaced colors forming a closed loop, sampled with
// linear interpolation. Position 0..1 wraps around.
struct Palette {
    const char* name;
    Rgb8 entries[16];

    Rgbf at(float pos) const;
};

size_t paletteCount();
const Palette& palette(size_t index);  // index is taken modulo paletteCount()

}  // namespace kd
