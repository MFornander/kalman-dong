#include "renderer.h"

#include <cmath>

namespace kd {

namespace {

const Rgbf kWhite{1.f, 1.f, 1.f};
const Rgbf kWarningRed{1.f, 0.f, 0.f};

float restDim(float restSeconds) {
    float t = clampf((restSeconds - Renderer::kRestDimAfter) / Renderer::kRestDimRamp, 0.f, 1.f);
    return lerpf(1.f, Renderer::kRestDimLevel, t);
}

}  // namespace

Renderer::Renderer(const DongProfile& profile)
    : geometry_(profile), criticalBattery_(profile.battery.criticalFraction) {
    float span = geometry_.lastPixelDistance() - geometry_.firstPixelDistance();
    sampleCount_ = static_cast<size_t>(std::ceil(span / kSampleSpacing)) + 1;
    if (sampleCount_ < 2) sampleCount_ = 2;
    samples_.resize(sampleCount_);
    pixels_.resize(geometry_.pixelsPerLine());
}

// A lightsaber blade extending to `front` (0..1 along the Blade): a pale
// core tinted by the Palette, with a white-hot leading edge.
Rgbf Renderer::ignitionColor(float along, float front, const Palette& pal, float hum) const {
    if (along > front) return {};
    Rgbf core = mix(pal.at(along * 0.25f), kWhite, 0.45f) * hum;
    float edge = 1.f - smoothstep(0.f, 0.06f, front - along);
    return front < 1.f ? mix(core, kWhite, edge) : core;
}

// Pose 1 points the tip up: every Line shows a bright tip. Pose 2 puts Line 0
// on top: only Line 0 lights. Green fills from the Hilt as the pose is held.
Rgbf Renderer::calibrationColor(const CalibrationView& cal, size_t line, float along) {
    const Rgbf kGreen{0.f, 1.f, 0.2f};
    if (cal.step == 1 && line != 0) return {};
    if (along < cal.progress) return kGreen * 0.6f;
    if (cal.step == 0) return kWhite * (along > 1.f - kWarningShare ? 0.8f : 0.08f);
    return kWhite * 0.5f;
}

void Renderer::renderSlice(const CloudField::Instant& clouds, const Pose& pose, const Palette& pal, size_t line,
                           float girth) {
    const float first = geometry_.firstPixelDistance();
    const float span = geometry_.lastPixelDistance() - first;
    for (size_t j = 0; j < sampleCount_; ++j) {
        float d = first + span * float(j) / float(sampleCount_ - 1);
        samples_[j] = clouds.colorAt(geometry_.fieldPoint(pose, line, d, girth), pal);
    }
    const size_t ppl = pixels_.size();
    for (size_t i = 0; i < ppl; ++i) {
        float s = ppl > 1 ? float(i) / float(ppl - 1) * float(sampleCount_ - 1) : 0.f;
        size_t j = static_cast<size_t>(s);
        if (j >= sampleCount_ - 1) j = sampleCount_ - 2;
        pixels_[i] = mix(samples_[j], samples_[j + 1], s - float(j));
    }
}

void Renderer::render(const FrameInput& in, const Settings& settings, Rgb8* lines) {
    const Palette& pal = palette(settings.palette);
    const size_t ppl = geometry_.pixelsPerLine();
    const size_t lineCount = geometry_.lineCount();
    const float girth = settings.girthValue();
    field_.setClearSky(settings.clearSkyValue());

    const bool igniting = in.ignitionProgress < 1.f;
    const float fade = igniting ? 0.f : clampf(in.sinceIgnition / kIgnitionFade, 0.f, 1.f);
    const float level = settings.brightnessValue() * restDim(in.restSeconds);
    const bool warn = in.batteryFraction >= 0.f && in.batteryFraction < criticalBattery_;
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(std::fmod(in.localTime * kPi, 2.0 * kPi)));
    const float hum = 0.92f + 0.08f * std::sin(static_cast<float>(std::fmod(in.localTime * 37.0, 2.0 * kPi)));
    const CloudField::Instant clouds = field_.at(in.showTime);

    if (in.calibration.active) {
        for (size_t line = 0; line < lineCount; ++line)
            for (size_t i = 0; i < ppl; ++i)
                lines[line * ppl + i] =
                    toRgb8(calibrationColor(in.calibration, line, ppl > 1 ? float(i) / float(ppl - 1) : 0.f));
        return;
    }

    for (size_t line = 0; line < lineCount; ++line) {
        if (!igniting && (line == 0 || girth > 0.f)) renderSlice(clouds, in.pose, pal, line, girth);
        Rgb8* out = lines + line * ppl;
        for (size_t i = 0; i < ppl; ++i) {
            float along = ppl > 1 ? float(i) / float(ppl - 1) : 0.f;
            Rgbf c;
            if (igniting) {
                c = ignitionColor(along, in.ignitionProgress, pal, hum);
            } else if (fade < 1.f) {
                c = mix(ignitionColor(along, 1.f, pal, hum), pixels_[i], fade);
            } else {
                c = pixels_[i];
            }
            c = c * level;
            if (warn && along < kWarningShare) c = kWarningRed * (0.15f + 0.6f * pulse);
            out[i] = toRgb8(c);
        }
    }
}

}  // namespace kd
