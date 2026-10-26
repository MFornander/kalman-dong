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
    // The Cloud Field frozen at one Show Clock time. Take one per frame: it
    // does the double-precision time math (slow on the Dong, which has no
    // double FPU) once instead of for every sample.
    class Instant {
    public:
        Rgbf colorAt(const Vec3& p, const Palette& pal) const;
        float cloudiness(const Vec3& p) const;
        float hue(const Vec3& p) const;

    private:
        friend class CloudField;
        CloudSettings settings_;
        float invSize_ = 1.f;
        Vec3 hueOffset_;
        Vec3 densityOffset_;
        float cycle_ = 0.f;
    };

    explicit CloudField(const CloudSettings& settings = {}) : settings_(settings) {}

    const CloudSettings& settings() const { return settings_; }
    void setSettings(const CloudSettings& s) { settings_ = s; }
    void setClearSky(float share) { settings_.clearSky = clampf(share, 0.f, 0.9f); }
    void setCloudSize(float metres) { settings_.cloudSize = clampf(metres, 0.05f, 20.f); }

    Instant at(double showTime) const;

    // Color at world point `p` (metres, Hilt at origin, Z up) at Show Clock
    // time `showTime` (seconds).
    Rgbf colorAt(const Vec3& p, double showTime, const Palette& pal) const { return at(showTime).colorAt(p, pal); }

    // 0 = Clear Sky, 1 = solid cloud.
    float cloudiness(const Vec3& p, double showTime) const { return at(showTime).cloudiness(p); }

    // Position (0..1, wrapping) within the Palette.
    float hue(const Vec3& p, double showTime) const { return at(showTime).hue(p); }

private:
    CloudSettings settings_;
};

}  // namespace kd
