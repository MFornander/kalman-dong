#include "cloud_field.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "noise.h"

namespace kd {

namespace {

constexpr int kOctaves = 2;

// The hue and density layers drift in different directions at different
// speeds, so clouds change shape instead of just sliding past.
const Vec3 kHueWind{1.f, 0.35f, 0.08f};
const Vec3 kDensityWind{-0.45f, 1.1f, 0.15f};
const Vec3 kDensitySeed{71.3f, 13.9f, 29.5f};

// Converts a raw fbm value into its rank (0..1) among all fbm values, so
// settings like "15% Clear Sky" mean exactly that and every Palette color
// gets equal share.
class NoiseCdf {
public:
    NoiseCdf() {
        constexpr int kSamples = 8192;
        static float samples[kSamples];
        uint32_t s = 0x2545F491u;
        auto rnd = [&s]() {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            return (s & 0xFFFFFF) / float(0x1000000) * 64.f;
        };
        for (float& v : samples) {
            float x = rnd(), y = rnd(), z = rnd();
            v = fbm3(x, y, z, kOctaves);
        }
        std::sort(samples, samples + kSamples);
        for (int i = 0; i < kBins; ++i) quantile_[i] = samples[i * (kSamples - 1) / (kBins - 1)];
    }

    float rank(float v) const {
        if (v <= quantile_[0]) return 0.f;
        if (v >= quantile_[kBins - 1]) return 1.f;
        const float* hi = std::upper_bound(quantile_, quantile_ + kBins, v);
        int i = static_cast<int>(hi - quantile_) - 1;
        float span = quantile_[i + 1] - quantile_[i];
        float t = span > 0.f ? (v - quantile_[i]) / span : 0.f;
        return (i + t) / float(kBins - 1);
    }

private:
    static constexpr int kBins = 65;
    float quantile_[kBins];
};

const NoiseCdf& cdf() {
    static const NoiseCdf instance;
    return instance;
}

// Layer offset at `showTime`, wrapped by the noise period in double precision
// so the Cloud Field never jumps or loses precision however long the show.
Vec3 drift(const Vec3& wind, float speed, float cloudSize, double showTime) {
    double d = showTime * double(speed) / double(cloudSize);
    auto wrap = [](double v) { return static_cast<float>(std::fmod(v, double(kNoisePeriod))); };
    return {wrap(d * wind.x), wrap(d * wind.y), wrap(d * wind.z)};
}

}  // namespace

float CloudField::hue(const Vec3& p, double showTime) const {
    Vec3 u = p * (1.f / settings_.cloudSize) +
             drift(kHueWind, settings_.windSpeed, settings_.cloudSize, showTime);
    float r = cdf().rank(fbm3(u.x, u.y, u.z, kOctaves));
    float cycle = static_cast<float>(std::fmod(showTime * settings_.hueCycle, 1.0));
    float h = r * settings_.hueSpread + cycle;
    return h - std::floor(h);
}

float CloudField::cloudiness(const Vec3& p, double showTime) const {
    Vec3 u = p * (1.f / settings_.cloudSize) + kDensitySeed +
             drift(kDensityWind, settings_.windSpeed, settings_.cloudSize, showTime);
    float r = cdf().rank(fbm3(u.x, u.y, u.z, kOctaves));
    constexpr float kSoft = 0.08f;  // soft cloud edges, in rank units
    float edge = settings_.clearSky;
    if (edge <= 0.f) return 1.f;
    return smoothstep(edge - kSoft, edge + kSoft, r);
}

Rgbf CloudField::colorAt(const Vec3& p, double showTime, const Palette& pal) const {
    float level = lerpf(settings_.clearSkyLevel, 1.f, cloudiness(p, showTime));
    return pal.at(hue(p, showTime)) * level;
}

}  // namespace kd
