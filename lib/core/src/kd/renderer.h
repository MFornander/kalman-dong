// Renderer fills every Line with its Slice of the Cloud Field, then draws the
// Local Layers on top: Ignition, Rest dimming and the low-battery warning.
#pragma once

#include <cstddef>
#include <vector>

#include "blade_geometry.h"
#include "cloud_field.h"
#include "color.h"
#include "controls.h"
#include "dong_profile.h"

namespace kd {

struct FrameInput {
    double showTime = 0;           // Show Clock, s
    double localTime = 0;          // this Dong's own uptime, s
    Pose pose;                     // already predicted to when the Pixels light
    float ignitionProgress = 1.f;  // 0..1
    float sinceIgnition = 1e9f;    // s since Ignition finished
    float restSeconds = 0.f;       // s at Rest; 0 while moving
    float batteryFraction = -1.f;  // 0..1, negative if unknown
};

class Renderer {
public:
    static constexpr float kSampleSpacing = 0.04f;   // m between Cloud Field samples along a Line
    static constexpr float kIgnitionFade = 0.6f;     // s to fade from Ignition into the clouds
    static constexpr float kRestDimAfter = 60.f;     // s at Rest before dimming
    static constexpr float kRestDimRamp = 3.f;       // s to reach the dimmed level
    static constexpr float kRestDimLevel = 0.2f;
    static constexpr float kWarningShare = 0.15f;    // share of each Line, from the Hilt, that pulses red

    explicit Renderer(const DongProfile& profile);

    const BladeGeometry& geometry() const { return geometry_; }
    const CloudField& cloudField() const { return field_; }

    // `lines` holds lineCount * pixelsPerLine Pixels, Line after Line, each
    // counted from the Hilt outward.
    void render(const FrameInput& in, const Settings& settings, Rgb8* lines);

private:
    Rgbf ignitionColor(float along, float front, const Palette& pal, double localTime) const;
    void renderSlice(const FrameInput& in, const Palette& pal, size_t line, float girth);

    BladeGeometry geometry_;
    CloudField field_;
    float criticalBattery_;
    size_t sampleCount_;
    std::vector<Rgbf> samples_;
    std::vector<Rgbf> pixels_;  // one Line
};

}  // namespace kd
