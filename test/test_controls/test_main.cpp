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
    ControlEvent tap(int b) { return hold(b, 120); }
    ControlEvent press(int b) { return hold(b, 900); }
};

}  // namespace

TEST_CASE("A cycles Palettes, long A steps Clear Sky") {
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
    CHECK(h.settings.palette == 0);  // a long press is not also a short one
}

TEST_CASE("B steps brightness, long B steps Girth through 0 to 0.6 m") {
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
    CHECK(seen[3] == doctest::Approx(0.6f));
    CHECK(h.settings.girthValue() == 0.f);  // wraps around
}

TEST_CASE("C wakes the display, long C Re-levels") {
    Hand h;
    Settings before = h.settings;
    CHECK(h.tap(2).kind == ControlEvent::Kind::WakeDisplay);
    CHECK(h.press(2).kind == ControlEvent::Kind::Relevel);
    CHECK(h.settings == before);
}

TEST_CASE("a long press fires while still held") {
    Hand h;
    ControlEvent fired;
    for (uint32_t t = 0; t < 900 && fired.kind == ControlEvent::Kind::None; t += 5)
        fired = h.controls.update(false, false, true, h.now += 5);
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
