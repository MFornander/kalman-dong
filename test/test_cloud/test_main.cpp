#include "../doctest_main.h"

#include <cmath>

#include "kd/cloud_field.h"
#include "kd/noise.h"

using namespace kd;

namespace {

// A spread of points covering a 2 m Blade's reach.
template <typename F>
void forGrid(F f) {
    for (int i = 0; i < 24; ++i)
        for (int j = 0; j < 24; ++j)
            for (int k = 0; k < 8; ++k) f(Vec3{-2.f + i * 0.17f, -2.f + j * 0.17f, -2.f + k * 0.5f});
}

}  // namespace

TEST_CASE("noise is deterministic, bounded, and periodic") {
    float a = noise3(1.3f, 2.7f, 0.4f);
    CHECK(a == noise3(1.3f, 2.7f, 0.4f));
    CHECK(noise3(3.f, 5.f, 7.f) == doctest::Approx(0.f));  // zero on lattice points
    for (float x = 0.f; x < 8.f; x += 0.37f) {
        float v = noise3(x, x * 0.7f, 1.f - x);
        CHECK(v >= -1.f);
        CHECK(v <= 1.f);
        CHECK(noise3(x + kNoisePeriod, x * 0.7f, 1.f - x) == doctest::Approx(v).epsilon(1e-3));
    }
}

TEST_CASE("noise is smooth: nearby points have nearby values") {
    for (float x = 0.f; x < 4.f; x += 0.11f) {
        float d = std::fabs(noise3(x, 0.5f, 0.25f) - noise3(x + 0.01f, 0.5f, 0.25f));
        CHECK(d < 0.05f);
    }
}

TEST_CASE("Clear Sky covers the requested share of the Cloud Field") {
    for (float share : {0.f, 0.15f, 0.3f, 0.5f}) {
        CloudField field;
        field.setClearSky(share);
        int clear = 0, total = 0;
        forGrid([&](const Vec3& p) {
            clear += field.cloudiness(p, 12.0) < 0.5f;
            ++total;
        });
        CAPTURE(share);
        CHECK(std::fabs(float(clear) / total - share) < 0.06f);
    }
}

TEST_CASE("Clear Sky is dimmer but never dark") {
    CloudSettings s;
    s.clearSky = 0.5f;
    CloudField field(s);
    const Palette& pal = palette(0);
    float minLevel = 1.f;
    forGrid([&](const Vec3& p) {
        Rgbf c = field.colorAt(p, 3.0, pal);
        float peak = std::fmax(c.r, std::fmax(c.g, c.b));
        minLevel = std::fmin(minLevel, peak);
    });
    // Blends between neighboring Palette entries peak a little below full.
    CHECK(minLevel >= s.clearSkyLevel * 0.8f);
}

TEST_CASE("every part of the Palette shows up") {
    CloudField field;
    int buckets[8] = {};
    forGrid([&](const Vec3& p) { ++buckets[static_cast<int>(field.hue(p, 0.0) * 8) & 7]; });
    int total = 0;
    for (int b : buckets) total += b;
    for (int b : buckets) CHECK(float(b) / total > 0.06f);
}

TEST_CASE("the Cloud Field depends only on place and Show Clock") {
    CloudField a, b;
    Vec3 p{0.4f, -1.1f, 0.9f};
    Rgbf ca = a.colorAt(p, 1234.5, palette(1));
    Rgbf cb = b.colorAt(p, 1234.5, palette(1));
    CHECK(ca.r == cb.r);
    CHECK(ca.g == cb.g);
    CHECK(ca.b == cb.b);
}

TEST_CASE("clouds drift slowly and never jump, even after a very long show") {
    CloudField field;
    Vec3 p{0.8f, 0.2f, 0.5f};
    for (double t0 : {0.0, 3600.0, 36000.0, 360000.0}) {
        float maxStep = 0.f;
        float prev = field.cloudiness(p, t0);
        for (int i = 1; i <= 200; ++i) {
            float c = field.cloudiness(p, t0 + i * (1.0 / 240.0));
            maxStep = std::fmax(maxStep, std::fabs(c - prev));
            prev = c;
        }
        CAPTURE(t0);
        CHECK(maxStep < 0.05f);
    }
}

TEST_CASE("moving through the field changes what you see") {
    CloudField field;
    int differ = 0;
    for (int i = 0; i < 20; ++i) {
        float h0 = field.hue({i * 0.1f, 0, 0}, 0.0);
        float h1 = field.hue({i * 0.1f, 0.6f, 0}, 0.0);
        differ += std::fabs(h0 - h1) > 0.05f;
    }
    CHECK(differ > 10);
}
