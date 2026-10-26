#include "blade_geometry.h"

#include <cmath>

namespace kd {

BladeGeometry::BladeGeometry(const DongProfile& profile)
    : lineCount_(profile.lineCount),
      pixelsPerLine_(profile.pixelsPerLine),
      first_(profile.hiltToFirstPixel),
      length_(profile.bladeLength) {}

float BladeGeometry::pixelDistance(size_t i) const {
    if (pixelsPerLine_ < 2) return first_;
    return first_ + length_ * float(i) / float(pixelsPerLine_ - 1);
}

Vec3 BladeGeometry::lineNormal(size_t line) const {
    float a = 2.f * kPi * float(line) / float(lineCount_ ? lineCount_ : 1);
    return {0.f, std::cos(a), std::sin(a)};
}

Vec3 BladeGeometry::bodyPoint(size_t line, float distance, float girth) const {
    return Vec3{distance, 0.f, 0.f} + lineNormal(line) * (girth * 0.5f);
}

}  // namespace kd
