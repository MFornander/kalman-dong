#include "cloud_field.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

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
// gets equal share. Built once; lookups are a table read with no division
// (the S3's FPU has no divide instruction).
class NoiseCdf {
public:
    NoiseCdf() {
        constexpr int kSamples = 8192;
        std::vector<float> samples(kSamples);
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
        std::sort(samples.begin(), samples.end());
        float quantile[kBins];
        for (int i = 0; i < kBins; ++i) quantile[i] = samples[i * (kSamples - 1) / (kBins - 1)];

        lo_ = quantile[0];
        float hi = quantile[kBins - 1];
        scale_ = kGrid / (hi - lo_);
        for (int g = 0; g <= kGrid; ++g) grid_[g] = slowRank(quantile, lo_ + (hi - lo_) * g / kGrid);
    }

    float rank(float v) const {
        float t = (v - lo_) * scale_;
        if (t <= 0.f) return 0.f;
        if (t >= float(kGrid)) return 1.f;
        int i = static_cast<int>(t);
        return lerpf(grid_[i], grid_[i + 1], t - static_cast<float>(i));
    }

private:
    static constexpr int kBins = 65;
    static constexpr int kGrid = 256;

    static float slowRank(const float* quantile, float v) {
        if (v <= quantile[0]) return 0.f;
        if (v >= quantile[kBins - 1]) return 1.f;
        const float* hi = std::upper_bound(quantile, quantile + kBins, v);
        int i = static_cast<int>(hi - quantile) - 1;
        float span = quantile[i + 1] - quantile[i];
        float t = span > 0.f ? (v - quantile[i]) / span : 0.f;
        return (i + t) / float(kBins - 1);
    }

    float lo_ = 0.f;
    float scale_ = 1.f;
    float grid_[kGrid + 1];
};

// Built during static initialization (~10 ms on the Dong) so lookups skip
// the thread-safe guard a function-local static would add to every call.
const NoiseCdf kNoiseCdf;

const NoiseCdf& cdf() { return kNoiseCdf; }

// Layer offset at `showTime`, wrapped by the noise period in double precision
// so the Cloud Field never jumps or loses precision however long the show.
Vec3 drift(const Vec3& wind, float speed, float cloudSize, double showTime) {
    double d = showTime * double(speed) / double(cloudSize);
    auto wrap = [](double v) { return static_cast<float>(std::fmod(v, double(kNoisePeriod))); };
    return {wrap(d * wind.x), wrap(d * wind.y), wrap(d * wind.z)};
}

}  // namespace

CloudField::Instant CloudField::at(double showTime) const {
    Instant i;
    i.settings_ = settings_;
    i.invSize_ = 1.f / settings_.cloudSize;
    i.hueOffset_ = drift(kHueWind, settings_.windSpeed, settings_.cloudSize, showTime);
    i.densityOffset_ = kDensitySeed + drift(kDensityWind, settings_.windSpeed, settings_.cloudSize, showTime);
    i.cycle_ = static_cast<float>(std::fmod(showTime * settings_.hueCycle, 1.0));
    return i;
}

float CloudField::Instant::hue(const Vec3& p) const {
    Vec3 u = p * invSize_ + hueOffset_;
    float r = cdf().rank(fbm3(u.x, u.y, u.z, kOctaves));
    float h = r * settings_.hueSpread + cycle_;
    return h - static_cast<float>(fastFloor(h));
}

float CloudField::Instant::cloudiness(const Vec3& p) const {
    Vec3 u = p * invSize_ + densityOffset_;
    float r = cdf().rank(fbm3(u.x, u.y, u.z, kOctaves));
    constexpr float kSoft = 0.08f;  // soft cloud edges, in rank units
    float edge = settings_.clearSky;
    if (edge <= 0.f) return 1.f;
    return smoothstep(edge - kSoft, edge + kSoft, r);
}

Rgbf CloudField::Instant::colorAt(const Vec3& p, const Palette& pal) const {
    float level = lerpf(settings_.clearSkyLevel, 1.f, cloudiness(p));
    return pal.at(hue(p)) * level;
}

}  // namespace kd
