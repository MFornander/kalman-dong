#include "../doctest_main.h"

#include <cstdlib>
#include <vector>

#include "kd/power_governor.h"
#include "kd/renderer.h"

using namespace kd;

namespace {

DongProfile profile(uint16_t ppl = 60) {
    DongProfile p;
    p.bladeLength = 1.5f;
    p.hiltToFirstPixel = 0.15f;
    p.lineCount = 4;
    p.pixelsPerLine = ppl;
    p.strandCount = 1;
    p.strands[0].segmentCount = 4;
    for (uint8_t l = 0; l < 4; ++l) p.strands[0].segments[l] = {l, (l & 1) != 0, 0};
    p.power.budgetMilliamps = 2000.f;
    p.battery.sensed = true;
    return p;
}

struct Frame {
    DongProfile prof;
    Renderer renderer;
    std::vector<Rgb8> px;
    explicit Frame(const DongProfile& p) : prof(p), renderer(prof), px(p.lineCount * p.pixelsPerLine) {}
    const Rgb8& at(size_t line, size_t i) const { return px[line * prof.pixelsPerLine + i]; }
    int lit(size_t line) const {
        int n = 0;
        for (size_t i = 0; i < prof.pixelsPerLine; ++i) n += (at(line, i).r | at(line, i).g | at(line, i).b) != 0;
        return n;
    }
    float meanLevel() const {
        double s = 0;
        for (auto& c : px) s += c.r + c.g + c.b;
        return float(s / (px.size() * 3 * 255.0));
    }
};

FrameInput steady(double showTime = 10.0) {
    FrameInput in;
    in.showTime = showTime;
    in.localTime = showTime;
    in.pose.orientation = Quat::fromAxisAngle({0, 1, 0}, degToRad(-20));
    return in;
}

}  // namespace

TEST_CASE("Ignition lights the Blade from the Hilt outward like an extending lightsaber") {
    Frame f(profile());
    Settings s;
    FrameInput in = steady();
    int prevLit = -1;
    for (float progress : {0.f, 0.25f, 0.5f, 0.75f, 0.99f}) {
        in.ignitionProgress = progress;
        f.renderer.render(in, s, f.px.data());
        int lit = f.lit(0);
        CHECK(lit > prevLit);
        prevLit = lit;
        // Lit Pixels are the ones nearest the Hilt.
        if (lit > 0 && lit < 60) {
            CHECK((f.at(0, lit - 1).r | f.at(0, lit - 1).g | f.at(0, lit - 1).b) != 0);
            CHECK((f.at(0, lit).r | f.at(0, lit).g | f.at(0, lit).b) == 0);
        }
    }
    CHECK(prevLit > 55);
}

TEST_CASE("after Ignition the Blade fades into the clouds") {
    Frame ignition(profile()), clouds(profile()), mid(profile());
    Settings s;
    FrameInput in = steady();
    in.sinceIgnition = 0.f;
    ignition.renderer.render(in, s, ignition.px.data());
    in.sinceIgnition = 10.f;
    clouds.renderer.render(in, s, clouds.px.data());
    in.sinceIgnition = Renderer::kIgnitionFade / 2;
    mid.renderer.render(in, s, mid.px.data());
    CHECK(ignition.px != clouds.px);
    CHECK(mid.px != clouds.px);
    CHECK(mid.px != ignition.px);
}

TEST_CASE("with zero Girth every Line shows the same Slice; with Girth they differ") {
    Frame f(profile());
    Settings s;
    s.girth = 0;
    f.renderer.render(steady(), s, f.px.data());
    for (size_t i = 0; i < 60; ++i) CHECK(f.at(0, i) == f.at(2, i));

    s.girth = 3;  // 0.4 m
    f.renderer.render(steady(), s, f.px.data());
    int differ = 0;
    for (size_t i = 0; i < 60; ++i) differ += f.at(0, i) != f.at(2, i);
    CHECK(differ > 30);
}

TEST_CASE("the same Show Clock and orientation render the same frame on any Dong") {
    Frame a(profile()), b(profile());
    Settings s;
    a.renderer.render(steady(42.0), s, a.px.data());
    b.renderer.render(steady(42.0), s, b.px.data());
    CHECK(a.px == b.px);
}

TEST_CASE("swinging the Blade slices a different part of the clouds") {
    Frame a(profile()), b(profile());
    Settings s;
    FrameInput in = steady();
    a.renderer.render(in, s, a.px.data());
    in.pose.orientation = Quat::fromAxisAngle({0, 0, 1}, degToRad(45)) * in.pose.orientation;
    b.renderer.render(in, s, b.px.data());
    int differ = 0;
    for (size_t i = 0; i < a.px.size(); ++i) differ += a.px[i] != b.px[i];
    CHECK(differ > int(a.px.size() / 2));
}

TEST_CASE("brightness setting scales the Blade") {
    Frame dim(profile()), bright(profile());
    Settings s;
    s.brightness = 0;
    dim.renderer.render(steady(), s, dim.px.data());
    s.brightness = 3;
    bright.renderer.render(steady(), s, bright.px.data());
    CHECK(dim.meanLevel() == doctest::Approx(bright.meanLevel() * 0.25f).epsilon(0.05));
}

TEST_CASE("a minute at Rest dims the Blade; any motion brings it straight back") {
    Frame awake(profile()), resting(profile());
    Settings s;
    FrameInput in = steady();
    awake.renderer.render(in, s, awake.px.data());
    in.restSeconds = 59.f;
    resting.renderer.render(in, s, resting.px.data());
    CHECK(resting.px == awake.px);
    in.restSeconds = 70.f;
    resting.renderer.render(in, s, resting.px.data());
    CHECK(resting.meanLevel() == doctest::Approx(awake.meanLevel() * Renderer::kRestDimLevel).epsilon(0.05));
}

TEST_CASE("when sound drives brightness, silence dims the Blade and loud sound brings it to full") {
    Frame plain(profile()), quiet(profile()), half(profile()), loud(profile());
    Settings s;
    FrameInput in = steady();
    plain.renderer.render(in, s, plain.px.data());
    in.loudness = 0.f;
    quiet.renderer.render(in, s, quiet.px.data());
    in.loudness = 0.5f;
    half.renderer.render(in, s, half.px.data());
    in.loudness = 1.f;
    loud.renderer.render(in, s, loud.px.data());
    CHECK(loud.px == plain.px);
    CHECK(quiet.meanLevel() == doctest::Approx(plain.meanLevel() * Renderer::kQuietLevel).epsilon(0.05));
    CHECK(half.meanLevel() > quiet.meanLevel());
    CHECK(half.meanLevel() < loud.meanLevel());
}

TEST_CASE("a nearly empty battery pulses red at the Hilt end") {
    Frame f(profile());
    Settings s;
    FrameInput in = steady();
    in.batteryFraction = 0.03f;
    in.localTime = 0.5;  // pulse at its peak
    f.renderer.render(in, s, f.px.data());
    CHECK(f.at(0, 0).g == 0);
    CHECK(f.at(0, 0).b == 0);
    CHECK(f.at(0, 0).r > 100);
    CHECK(f.at(0, 59) != f.at(0, 0));
}

TEST_CASE("the Power Budget is never exceeded") {
    DongProfile p = profile(144);
    PowerGovernor gov(p.power, p.battery, 4 * 144);
    std::vector<Rgb8> px(4 * 144, Rgb8{255, 255, 255});
    float scale = gov.limit(px.data(), px.size(), 1.f);
    CHECK(scale < 1.f);
    CHECK(gov.milliamps(px.data(), px.size()) <= p.power.budgetMilliamps);
    // Rounding down to whole color steps gives away a little of the budget.
    CHECK(gov.milliamps(px.data(), px.size()) > p.power.budgetMilliamps * 0.9f);
}

TEST_CASE("frames already within the Power Budget are untouched") {
    DongProfile p = profile();
    PowerGovernor gov(p.power, p.battery, 240);
    std::vector<Rgb8> px(240, Rgb8{10, 20, 30});
    auto before = px;
    CHECK(gov.limit(px.data(), px.size(), 1.f) == 1.f);
    CHECK(px == before);
}

TEST_CASE("a low battery steps the Power Budget down") {
    DongProfile p = profile();
    PowerGovernor gov(p.power, p.battery, 240);
    CHECK(gov.budgetShare(-1.f) == 1.f);  // not sensed
    CHECK(gov.budgetShare(0.5f) == 1.f);
    CHECK(gov.budgetShare(0.12f) < 1.f);
    CHECK(gov.budgetShare(0.12f) > PowerGovernor::kCriticalBudgetShare);
    CHECK(gov.budgetShare(0.01f) == doctest::Approx(PowerGovernor::kCriticalBudgetShare));
    CHECK(gov.batteryFraction(p.battery.fullVolts + 0.2f) == 1.f);
    CHECK(gov.batteryFraction(p.battery.emptyVolts) == 0.f);
}

TEST_CASE("Mounting Calibration shows which way to hold the Dong, and the hold filling up") {
    Frame f(profile());
    Settings s;
    FrameInput in = steady();
    in.calibration.active = true;

    in.calibration.step = 0;  // tip up: a bright tip on every Line
    in.calibration.progress = 0.f;
    f.renderer.render(in, s, f.px.data());
    for (size_t line = 0; line < 4; ++line) CHECK(f.at(line, 59).r > 4 * f.at(line, 10).r);

    in.calibration.step = 1;  // Line 0 to the sky: only Line 0 lights
    f.renderer.render(in, s, f.px.data());
    CHECK(f.lit(0) == 60);
    CHECK(f.lit(1) + f.lit(2) + f.lit(3) == 0);

    in.calibration.progress = 0.5f;  // green fills from the Hilt
    f.renderer.render(in, s, f.px.data());
    CHECK(f.at(0, 5).g > f.at(0, 5).r);
    CHECK(f.at(0, 55).r == f.at(0, 55).g);
}

TEST_CASE("larger clouds spread each color over more of the Blade") {
    Frame small(profile()), large(profile());
    Settings s;
    s.cloudSize = 0;  // 0.3 m
    small.renderer.render(steady(), s, small.px.data());
    s.cloudSize = 3;  // 2.4 m
    large.renderer.render(steady(), s, large.px.data());
    auto roughness = [](const Frame& f) {
        long sum = 0;
        for (size_t i = 1; i < 60; ++i) {
            const Rgb8 &a = f.at(0, i), &b = f.at(0, i - 1);
            sum += std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
        }
        return sum;
    };
    CHECK(roughness(large) * 3 < roughness(small));
}
