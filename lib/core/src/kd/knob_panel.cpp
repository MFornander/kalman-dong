#include "knob_panel.h"

namespace kd {

namespace {

constexpr Setting kTurns[KnobReading::kKnobs] = {
    Setting::Palette, Setting::Brightness, Setting::ClearSky, Setting::CloudSize,
    Setting::Motion,  Setting::Girth,      Setting::None,     Setting::None,
};
constexpr int kButtonCKnob = 7;

}  // namespace

ControlEvents KnobPanel::update(const KnobReading& r, Controls& controls) {
    ControlEvents out;
    switchOn_ = r.switchOn;
    for (int k = 0; k < KnobReading::kKnobs; ++k) {
        const int32_t delta = static_cast<int32_t>(static_cast<uint32_t>(r.counts[k]) -
                                                   static_cast<uint32_t>(lastCounts_[k]));
        lastCounts_[k] = r.counts[k];
        if (!started_ || delta > kMaxCountsPerReading || delta < -kMaxCountsPerReading) {
            carry_[k] = 0;
            continue;
        }
        carry_[k] += delta;
        const int steps = carry_[k] / kCountsPerStep;  // toward zero, so both ways need a full detent
        carry_[k] -= steps * kCountsPerStep;
        if (steps != 0) out.add(controls.turn(kTurns[k], steps, r.ms));
    }
    started_ = true;

    for (int k = 0; k < KnobReading::kKnobs; ++k) {
        const ButtonTracker::Press p = pushes_[k].update((r.pushed >> k) & 1, r.ms);
        if (p == ButtonTracker::Press::None) continue;
        if (k == kButtonCKnob) {
            out.add(controls.press(Button::C, p, r.ms));
        } else if (kTurns[k] != Setting::None) {
            if (p == ButtonTracker::Press::Quick) out.add({ControlEvent::Kind::WakeDisplay, Setting::None});
            if (p == ButtonTracker::Press::Long) out.add(controls.restore(kTurns[k], r.ms));
        }
    }
    return out;
}

KnobPanel::PressInProgress KnobPanel::pressInProgress(uint32_t nowMs) const {
    PressInProgress p;
    for (int k = 0; k < KnobReading::kKnobs; ++k) {
        if (k != kButtonCKnob && kTurns[k] == Setting::None) continue;
        const uint32_t held = pushes_[k].heldMs(nowMs);
        if (held < Controls::kSegmentMs) continue;
        p.knob = k;
        p.segments = held >= ButtonTracker::kHeldMs ? Controls::kSegments : static_cast<int>(held / Controls::kSegmentMs);
        p.onRelease = held >= ButtonTracker::kHeldMs ? ButtonTracker::Press::None
                      : held >= ButtonTracker::kLongMs ? ButtonTracker::Press::Long
                                                       : ButtonTracker::Press::Quick;
        break;
    }
    return p;
}

}  // namespace kd
