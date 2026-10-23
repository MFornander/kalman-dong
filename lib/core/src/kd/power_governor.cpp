#include "power_governor.h"

#include <cmath>

#include "kd_math.h"

namespace kd {

PowerGovernor::PowerGovernor(const PowerProfile& power, const BatteryProfile& battery, size_t physicalPixels)
    : power_(power), battery_(battery), physicalPixels_(physicalPixels) {}

float PowerGovernor::batteryFraction(float packVolts) const {
    if (!battery_.sensed) return -1.f;
    return clampf((packVolts - battery_.emptyVolts) / (battery_.fullVolts - battery_.emptyVolts), 0.f, 1.f);
}

float PowerGovernor::budgetShare(float fraction) const {
    if (fraction < 0.f || fraction >= battery_.lowFraction) return 1.f;
    float t = clampf((fraction - battery_.criticalFraction) / (battery_.lowFraction - battery_.criticalFraction),
                     0.f, 1.f);
    return lerpf(kCriticalBudgetShare, 1.f, t);
}

float PowerGovernor::milliamps(const Rgb8* px, size_t n) const {
    unsigned long sum = 0;
    for (size_t i = 0; i < n; ++i) sum += px[i].r + px[i].g + px[i].b;
    return physicalPixels_ * power_.idleMilliampsPerPixel + sum / 255.f * power_.milliampsPerChannel;
}

float PowerGovernor::limit(Rgb8* px, size_t n, float batteryFraction) const {
    float idle = physicalPixels_ * power_.idleMilliampsPerPixel;
    float allowed = power_.budgetMilliamps * budgetShare(batteryFraction) - idle;
    float lit = milliamps(px, n) - idle;
    if (lit <= allowed || lit <= 0.f) return 1.f;
    float scale = allowed > 0.f ? allowed / lit : 0.f;
    // Round each channel down so the result never exceeds the budget.
    auto dim = [scale](uint8_t v) { return static_cast<uint8_t>(std::floor(v * scale)); };
    for (size_t i = 0; i < n; ++i) px[i] = {dim(px[i].r), dim(px[i].g), dim(px[i].b)};
    return scale;
}

}  // namespace kd
