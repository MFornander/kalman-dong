// The Pose as the Blade view's header shows it: two rows of three whole
// numbers that fit on a 135 px wide screen.
#pragma once

#include "blade_geometry.h"

namespace kd {

struct PoseReadout {
    // Travel plus Push in centimetres, wrapping every 10 m to -500..499.
    int pos[3] = {};
    // How the Dong has turned since `zero`: roll, tip up and turn (Z-Y-X) as
    // percent of 180 degrees, -100..100. Positive: rolled clockwise looking
    // out along the Blade, tip raised, turned clockwise seen from above.
    int rot[3] = {};
};

// `zero` is the orientation that reads 0, 0, 0 (MotionTracker::zeroOrientation()).
PoseReadout readout(const Pose& pose, const Quat& zero = {});

}  // namespace kd
