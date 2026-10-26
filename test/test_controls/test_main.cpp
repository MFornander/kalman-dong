#include "../doctest_main.h"

#include "kd/color.h"
#include "kd/controls.h"

using namespace kd;

namespace {

struct Hand {
    Settings settings;
    Controls controls{settings};
    uint32_t now = 1000;

    ControlEvent hold(int button, uint32_t ms) {
        ControlEvent last;
        for (uint32_t t = 0; t < ms; t += 5) {
            ControlEvent e = controls.update(button == 0, button == 1, button == 2, now += 5);
            if (e.kind != ControlEvent::Kind::None) last = e;
        }
        for (int i = 0; i < 20; ++i) {
            ControlEvent e = controls.update(false, false, false, now += 5);
            if (e.kind != ControlEvent::Kind::None) last = e;
        }
        return last;
    }
    ControlEvent tap(int b) { return hold(b, 120); }      // Quick
    ControlEvent press(int b) { return hold(b, 3000); }   // Long
};

}  // namespace

TEST_CASE("quick A cycles Palettes, long A steps Clear Sky") {
    Hand h;
    ControlEvent e = h.tap(0);
    CHECK(e.kind == ControlEvent::Kind::Changed);
    CHECK(e.changed == Setting::Palette);
    CHECK(h.settings.palette == 1);
    for (size_t i = 1; i < paletteCount(); ++i) h.tap(0);
    CHECK(h.settings.palette == 0);

    float before = h.settings.clearSkyValue();
    e = h.press(0);
    CHECK(e.changed == Setting::ClearSky);
    CHECK(h.settings.clearSkyValue() != before);
    CHECK(h.settings.palette == 0);  // a long press is not also a quick one
}

TEST_CASE("quick B steps brightness, long B steps Girth through 0 to 0.4 m") {
    Hand h;
    float b0 = h.settings.brightnessValue();
    CHECK(h.tap(1).changed == Setting::Brightness);
    CHECK(h.settings.brightnessValue() != b0);

    h.settings.girth = 0;
    float seen[4];
    for (float& g : seen) {
        g = h.settings.girthValue();
        CHECK(h.press(1).changed == Setting::Girth);
    }
    CHECK(seen[0] == 0.f);
    CHECK(seen[3] == doctest::Approx(0.4f));
    CHECK(h.settings.girthValue() == 0.f);  // wraps around
}

TEST_CASE("quick C wakes the display, long C Re-levels") {
    Hand h;
    Settings before = h.settings;
    CHECK(h.tap(2).kind == ControlEvent::Kind::WakeDisplay);
    CHECK(h.press(2).kind == ControlEvent::Kind::Relevel);
    CHECK(h.settings == before);
}

TEST_CASE("a long press acts when the button is released") {
    Hand h;
    ControlEvent fired;
    for (uint32_t t = 0; t < 3000; t += 5) {
        ControlEvent e = h.controls.update(false, false, true, h.now += 5);
        if (e.kind != ControlEvent::Kind::None) fired = e;
    }
    CHECK(fired.kind == ControlEvent::Kind::None);  // still held: nothing yet
    for (int i = 0; i < 20; ++i) {
        ControlEvent e = h.controls.update(false, false, false, h.now += 5);
        if (e.kind != ControlEvent::Kind::None) fired = e;
    }
    CHECK(fired.kind == ControlEvent::Kind::Relevel);
}


TEST_CASE("contact bounce is ignored") {
    Hand h;
    for (int i = 0; i < 6; ++i) h.controls.update(i % 2 == 0, false, false, h.now += 3);
    for (int i = 0; i < 20; ++i) h.controls.update(false, false, false, h.now += 5);
    CHECK(h.settings.palette == 0);
}

TEST_CASE("settings are saved once the buttons have been quiet a while") {
    Hand h;
    CHECK_FALSE(h.controls.shouldSave(h.now));
    h.tap(0);
    CHECK_FALSE(h.controls.shouldSave(h.now));
    h.tap(1);
    CHECK_FALSE(h.controls.shouldSave(h.now + 2000));
    CHECK(h.controls.shouldSave(h.now + Controls::kSaveQuietMs));
    h.controls.markSaved();
    CHECK_FALSE(h.controls.shouldSave(h.now + 10000));
}

TEST_CASE("settings loaded from flash are forced into range") {
    Settings s;
    s.palette = 200;
    s.brightness = 9;
    s.girth = 77;
    s.clearSky = 5;
    s.sanitize();
    CHECK(s.palette < paletteCount());
    CHECK(s.brightnessValue() > 0.f);
    CHECK(s.girthValue() <= 0.6f);
    CHECK(s.clearSkyValue() <= 0.5f);
}

TEST_CASE("holding C for 5 seconds starts Mounting Calibration, and nothing else") {
    Hand h;
    Settings before = h.settings;
    int calibrations = 0, others = 0;
    for (uint32_t t = 0; t < 7000; t += 5) {
        ControlEvent e = h.controls.update(false, false, true, h.now += 5);
        calibrations += e.kind == ControlEvent::Kind::CalibrateMounting;
        others += e.kind != ControlEvent::Kind::None && e.kind != ControlEvent::Kind::CalibrateMounting;
    }
    for (int i = 0; i < 20; ++i) {
        ControlEvent e = h.controls.update(false, false, false, h.now += 5);
        others += e.kind != ControlEvent::Kind::None;  // no Re-level on release
    }
    CHECK(calibrations == 1);
    CHECK(others == 0);
    CHECK(h.settings == before);
}

TEST_CASE("presses are quick before 2 seconds and long from 2 to 5 seconds") {
    Hand h;
    CHECK(h.hold(2, 1900).kind == ControlEvent::Kind::WakeDisplay);
    CHECK(h.hold(2, 2100).kind == ControlEvent::Kind::Relevel);
    CHECK(h.hold(2, 4900).kind == ControlEvent::Kind::Relevel);
}

TEST_CASE("a press in progress shows half-second segments and what letting go will do") {
    Hand h;
    for (uint32_t t = 0; t < 300; t += 5) h.controls.update(true, false, false, h.now += 5);
    CHECK(h.controls.pressInProgress(h.now).button == -1);  // too soon to show
    for (uint32_t t = 0; t < 500; t += 5) h.controls.update(true, false, false, h.now += 5);
    auto p = h.controls.pressInProgress(h.now);
    CHECK(p.button == 0);
    CHECK(p.segments == 1);
    CHECK(p.onRelease == ButtonTracker::Press::Quick);
    for (uint32_t t = 0; t < 1500; t += 5) h.controls.update(true, false, false, h.now += 5);
    p = h.controls.pressInProgress(h.now);
    CHECK(p.segments == 4);  // ~2.3 s
    CHECK(p.onRelease == ButtonTracker::Press::Long);
    for (uint32_t t = 0; t < 3000; t += 5) h.controls.update(true, false, false, h.now += 5);
    p = h.controls.pressInProgress(h.now);
    CHECK(p.segments == Controls::kSegments);
    CHECK(p.onRelease == ButtonTracker::Press::None);  // Held: letting go does nothing
}

TEST_CASE("holding A or B past 5 seconds does nothing, yet") {
    Hand h;
    Settings before = h.settings;
    CHECK(h.hold(0, 6000).kind == ControlEvent::Kind::None);
    CHECK(h.hold(1, 6000).kind == ControlEvent::Kind::None);
    CHECK(h.settings == before);
}
