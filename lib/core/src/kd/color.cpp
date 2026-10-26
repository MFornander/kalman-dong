#include "color.h"

#include <cmath>

#include "kd_math.h"

namespace kd {

Rgb8 toRgb8(const Rgbf& c) {
    auto q = [](float v) { return static_cast<uint8_t>(clampf(v, 0.f, 1.f) * 255.f + 0.5f); };
    return {q(c.r), q(c.g), q(c.b)};
}

Rgbf toRgbf(const Rgb8& c) {
    constexpr float k = 1.f / 255.f;  // multiply: the S3's FPU has no divide
    return {c.r * k, c.g * k, c.b * k};
}

Rgbf mix(const Rgbf& a, const Rgbf& b, float t) {
    return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)};
}

Rgbf Palette::at(float pos) const {
    float idx = (pos - static_cast<float>(fastFloor(pos))) * 16.f;
    int whole = fastFloor(idx);
    int i0 = whole & 15;
    int i1 = (i0 + 1) & 15;
    return mix(toRgbf(entries[i0]), toRgbf(entries[i1]), idx - static_cast<float>(whole));
}

namespace {

// Saturated but never fully dark; every Palette is a loop so hue bands wrap.
const Palette kPalettes[] = {
    {"Rainbow",
     {{255, 0, 0}, {255, 64, 0}, {255, 128, 0}, {255, 200, 0},
      {200, 255, 0}, {64, 255, 0}, {0, 255, 64}, {0, 255, 160},
      {0, 200, 255}, {0, 96, 255}, {32, 0, 255}, {112, 0, 255},
      {176, 0, 255}, {255, 0, 200}, {255, 0, 128}, {255, 0, 48}}},
    {"Pastel",
     {{255, 120, 120}, {255, 170, 110}, {255, 220, 120}, {230, 255, 130},
      {160, 255, 140}, {120, 255, 180}, {120, 255, 230}, {120, 220, 255},
      {130, 170, 255}, {160, 130, 255}, {200, 120, 255}, {240, 120, 255},
      {255, 120, 220}, {255, 130, 180}, {255, 140, 150}, {255, 125, 130}}},
    {"Unicorn",
     {{255, 0, 160}, {255, 64, 200}, {200, 64, 255}, {128, 64, 255},
      {64, 128, 255}, {0, 200, 255}, {0, 255, 220}, {96, 255, 200},
      {200, 255, 255}, {128, 200, 255}, {160, 96, 255}, {220, 64, 255},
      {255, 32, 220}, {255, 96, 180}, {255, 160, 220}, {255, 64, 180}}},
    {"Sunset",
     {{255, 32, 0}, {255, 80, 0}, {255, 140, 0}, {255, 200, 32},
      {255, 160, 64}, {255, 96, 96}, {255, 48, 128}, {200, 0, 160},
      {128, 0, 200}, {200, 0, 160}, {255, 32, 120}, {255, 64, 64},
      {255, 120, 32}, {255, 180, 0}, {255, 100, 0}, {255, 48, 0}}},
    {"Aurora",
     {{0, 255, 96}, {0, 255, 160}, {0, 220, 255}, {0, 128, 255},
      {64, 64, 255}, {128, 0, 255}, {200, 0, 255}, {128, 0, 255},
      {32, 96, 255}, {0, 200, 200}, {0, 255, 128}, {64, 255, 64},
      {160, 255, 0}, {64, 255, 64}, {0, 255, 96}, {0, 255, 64}}},
};

}  // namespace

size_t paletteCount() { return sizeof(kPalettes) / sizeof(kPalettes[0]); }

const Palette& palette(size_t index) { return kPalettes[index % paletteCount()]; }

}  // namespace kd
