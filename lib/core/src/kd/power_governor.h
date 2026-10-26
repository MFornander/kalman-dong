// Keeps the Pixels within the Power Budget, and stretches a low battery.
#pragma once

#include <cstddef>

#include "color.h"
#include "dong_profile.h"

namespace kd {

class PowerGovernor {
public:
    PowerGovernor(const PowerProfile& power, const BatteryProfile& battery, size_t physicalPixels);

    // 0..1 charge estimate from pack voltage; negative if the pack is not sensed.
    float batteryFraction(float packVolts) const;

    // Share of the Power Budget allowed at this battery level: full above the
    // low threshold, stepping down to a floor at the critical threshold.
    float budgetShare(float batteryFraction) const;

    // Estimated draw of these Pixels, plus idle draw of every physical Pixel.
    float milliamps(const Rgb8* px, size_t n) const;

    // Dims `px` just enough to stay within the (battery-adjusted) Power
    // Budget. Returns the scale applied (1 = untouched).
    float limit(Rgb8* px, size_t n, float batteryFraction) const;

    static constexpr float kCriticalBudgetShare = 0.35f;

private:
    PowerProfile power_;
    BatteryProfile battery_;
    size_t physicalPixels_;
};

}  // namespace kd
