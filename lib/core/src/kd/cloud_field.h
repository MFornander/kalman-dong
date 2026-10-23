// The Cloud Field: an invisible, slowly evolving volume of rainbow clouds
// anchored to the space around the wielder.
//
// It is a pure function of (point in space, Show Clock, settings, Palette):
// see docs/adr/0003-frame-is-pure-function-of-show-clock.md.
#pragma once

#include "color.h"
#include "kd_math.h"

namespace kd {

struct CloudSettings {
    float cloudSize = 0.6f;      // m; typical size of one cloud
    float windSpeed = 0.05f;     // m/s; how fast clouds drift past
    float clearSky = 0.15f;      // 0..0.9; share of the field that is Clear Sky
    float clearSkyLevel = 0.4f;  // 0..1; brightness of Clear Sky relative to cloud
    float hueSpread = 1.0f;      // how many times the Palette repeats across the hue range
    float hueCycle = 0.01f;      // Palette rotations per second
};

class CloudField {
public:
    explicit CloudField(const CloudSettings& settings = {}) : settings_(settings) {}

    const CloudSettings& settings() const { return settings_; }
    void setSettings(const CloudSettings& s) { settings_ = s; }
    void setClearSky(float share) { settings_.clearSky = clampf(share, 0.f, 0.9f); }

    // Color at world point `p` (metres, Hilt at origin, Z up) at Show Clock
    // time `showTime` (seconds).
    Rgbf colorAt(const Vec3& p, double showTime, const Palette& pal) const;

    // 0 = Clear Sky, 1 = solid cloud.
    float cloudiness(const Vec3& p, double showTime) const;

    // Position (0..1, wrapping) within the Palette.
    float hue(const Vec3& p, double showTime) const;

private:
    CloudSettings settings_;
};

}  // namespace kd
