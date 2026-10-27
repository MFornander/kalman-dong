// The knobs of an M5Stack 8-Encoder unit as controls, alongside buttons A-C.
// Knobs count 1-8 as printed on the unit (0-7 here).
//
//   Knob   Turn           Push: quick     long (2 s)          held (5 s)
//   1      Palette        Status page     back to default
//   2      brightness     Status page     back to default
//   3      Clear Sky      Status page     back to default
//   4      cloud size     Status page     back to default
//   5      cloud motion   Status page     back to default
//   6      Girth          Status page     back to default
//   7      quiet glow     Status page     back to default
//   8      (spare)        Status page     Re-level            Mounting Calibration
//
// Knob 8's push is button C. A quick push never changes a setting, so
// bumping a knob while turning it is harmless. The toggle switch turns sound
// on and off: it decides when the unit is first read and whenever it is
// flipped; in between, serial 'm' can still change it.
#pragma once

#include <cstdint>

#include "controls.h"

namespace kd {

// One look at the unit.
struct KnobReading {
    static constexpr int kKnobs = 8;
    int32_t counts[kKnobs] = {};  // each knob's running count, as the unit keeps it
    uint8_t pushed = 0;           // bit i: knob i is pushed in
    bool switchOn = false;
    uint32_t ms = 0;              // when it was read
};

class KnobPanel {
public:
    // Counts the unit adds per detent ("click") of a knob.
    static constexpr int kCountsPerStep = 2;
    // A jump bigger than this between two readings means the unit restarted
    // (its counts went back to 0), not that a knob was spun.
    static constexpr int kMaxCountsPerReading = 40;

    // Turns and finished presses since the last reading become events.
    ControlEvents update(const KnobReading& r, Controls& controls);

    // A push in progress, for showing what letting go will do (as
    // Controls::pressInProgress does for the buttons).
    struct PressInProgress {
        int knob = -1;     // 0-7; -1 when none is held a segment yet
        int segments = 0;  // filled, one per Controls::kSegmentMs, up to Controls::kSegments
        ButtonTracker::Press onRelease = ButtonTracker::Press::None;
    };
    PressInProgress pressInProgress(uint32_t nowMs) const;

    bool switchOn() const { return switchOn_; }

private:
    bool started_ = false;  // a first reading has set where the counts start
    int32_t lastCounts_[KnobReading::kKnobs] = {};
    int carry_[KnobReading::kKnobs] = {};  // counts toward the next step
    ButtonTracker pushes_[KnobReading::kKnobs];
    bool switchOn_ = false;
};

}  // namespace kd
