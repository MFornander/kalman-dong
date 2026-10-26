// The Pose as the Blade view's header shows it: two rows of three small
// whole numbers that fit on a 135 px wide screen.
#pragma once

#include "blade_geometry.h"

namespace kd {

struct PoseReadout {
    // Travel plus Push in decimetres, wrapping every 10 m to -50..49.
    int pos[3] = {};
    // Roll, tip up and turn (Z-Y-X) as percent of 180 degrees, -100..100.
    // Positive: rolled clockwise looking out along the Blade, tip above
    // level, turned clockwise seen from above.
    int rot[3] = {};
};

PoseReadout readout(const Pose& pose);

}  // namespace kd
