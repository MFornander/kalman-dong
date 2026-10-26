// Where each Pixel of the Blade is in space.
#pragma once

#include <cstddef>

#include "dong_profile.h"
#include "kd_math.h"

namespace kd {

// The Dong's placement in the Cloud Field: its orientation, and how far the
// Hilt has moved from the origin by Travel plus any Push. See
// docs/adr/0002-orientation-only-anchoring.md.
struct Pose {
    Quat orientation;  // body -> Cloud Field frame (Z up, heading set by Re-level)
    Vec3 displacement;  // m; Travel plus Push
};

class BladeGeometry {
public:
    explicit BladeGeometry(const DongProfile& profile);

    size_t lineCount() const { return lineCount_; }
    size_t pixelsPerLine() const { return pixelsPerLine_; }

    // Distance (m) from the Hilt origin to Pixel `i` of any Line.
    float pixelDistance(size_t i) const;
    float firstPixelDistance() const { return first_; }
    float lastPixelDistance() const { return first_ + length_; }

    // Outward direction of Line `line` in the body frame.
    Vec3 lineNormal(size_t line) const;

    // Body-frame point `distance` m out along Line `line` when the Lines are
    // spread to `girth` m across.
    Vec3 bodyPoint(size_t line, float distance, float girth) const;

    // Same point in the Cloud Field, for the Dong at `pose`.
    Vec3 fieldPoint(const Pose& pose, size_t line, float distance, float girth) const {
        return pose.orientation.rotate(bodyPoint(line, distance, girth)) + pose.displacement;
    }

private:
    size_t lineCount_;
    size_t pixelsPerLine_;
    float first_;
    float length_;
};

}  // namespace kd
