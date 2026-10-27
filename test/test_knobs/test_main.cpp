#include "../doctest_main.h"

#include <vector>

#include "kd/color.h"
#include "kd/knob_panel.h"

using namespace kd;

namespace {

constexpr int kStep = KnobPanel::kCountsPerStep;

// A wielder with the unit, read every 20 ms as on the board.
struct Hand {
    Settings settings;
    Controls controls{settings};
    KnobPanel panel;
    KnobReading unit;
    uint32_t now = 1000;
    std::vector<ControlEvent> events;

    void read() {
        unit.ms = now += 20;
        for (const ControlEvent& e : panel.update(unit, controls)) events.push_back(e);
    }
    void turn(int knob, int counts) {
        unit.counts[knob - 1] += counts;
        read();
    }
    void click(int knob, int detents) {
        for (int i = 0; i < (detents < 0 ? -detents : detents); ++i) turn(knob, detents < 0 ? -kStep : kStep);
    }
    void push(int knob, uint32_t ms) {
        unit.pushed |= 1 << (knob - 1);
        for (uint32_t t = 0; t < ms; t += 20) read();
        unit.pushed &= ~(1 << (knob - 1));
        for (int i = 0; i < 5; ++i) read();
    }
    Hand() { read(); }  // the first reading only sets where the counts start
};

}  // namespace

TEST_CASE("the first reading takes the unit's counts as they are") {
    Hand h;
    Settings before = h.settings;
    KnobReading r;
    for (int k = 0; k < KnobReading::kKnobs; ++k) r.counts[k] = 1000 + k;
    KnobPanel fresh;
    CHECK(fresh.update(r, h.controls).count == 0);
    CHECK(h.settings == before);
}

TEST_CASE("each knob steps its own setting one step per detent") {
    Hand h;
    h.click(1, 1);
    CHECK(h.settings.palette == 1);
    h.click(2, 1);
    CHECK(h.settings.brightnessValue() == doctest::Approx(1.f));
    h.click(3, -1);
    CHECK(h.settings.clearSkyValue() == 0.f);
    h.click(4, 1);
    CHECK(h.settings.cloudSizeValue() == doctest::Approx(1.2f));
    h.click(5, 2);
    CHECK(h.settings.motionValue() == 2.f);
    h.click(6, 1);
    CHECK(h.settings.girthValue() == doctest::Approx(0.2f));
    REQUIRE(h.events.size() == 7);
    CHECK(h.events[0].changed == Setting::Palette);
    CHECK(h.events[6].changed == Setting::Girth);
    for (const ControlEvent& e : h.events) CHECK(e.kind == ControlEvent::Kind::Changed);
}

TEST_CASE("half a detent does nothing, the other half finishes the step") {
    Hand h;
    h.turn(2, kStep - 1);
    CHECK(h.settings.brightness == 2);
    CHECK(h.events.empty());
    h.turn(2, 1);
    CHECK(h.settings.brightness == 3);
    // A wobble back and forth inside a detent never steps.
    for (int i = 0; i < 10; ++i) {
        h.turn(2, -1);
        h.turn(2, 1);
    }
    CHECK(h.settings.brightness == 3);
}

TEST_CASE("a fast spin steps as many times as it has detents") {
    Hand h;
    h.turn(1, 3 * kStep);  // three detents between two readings
    CHECK(h.settings.palette == 3);
    CHECK(h.events.size() == 1);
}

TEST_CASE("settings stop at their ends, and the Palette wraps around") {
    Hand h;
    h.click(2, 10);
    CHECK(h.settings.brightnessValue() == doctest::Approx(1.f));
    h.click(2, -10);
    CHECK(h.settings.brightnessValue() == doctest::Approx(0.25f));
    h.events.clear();
    h.click(2, -1);  // already at the end: still shown, so the wielder sees why
    REQUIRE(h.events.size() == 1);
    CHECK(h.events[0].changed == Setting::Brightness);

    h.click(1, -1);
    CHECK(h.settings.palette == paletteCount() - 1);
    h.click(1, 1);
    CHECK(h.settings.palette == 0);
}

TEST_CASE("the spare knobs do nothing") {
    Hand h;
    const Settings before = h.settings;
    h.click(7, 5);
    h.click(8, -5);
    h.push(7, 100);
    h.push(7, 3000);
    CHECK(h.settings == before);
    CHECK(h.events.empty());
}

TEST_CASE("pushing a setting's knob opens the Status page; a long push puts it back to default") {
    Hand h;
    h.click(4, 2);
    h.click(6, -1);
    h.events.clear();
    h.push(4, 100);
    REQUIRE(h.events.size() == 1);
    CHECK(h.events[0].kind == ControlEvent::Kind::WakeDisplay);
    CHECK(h.settings.cloudSize != Settings{}.cloudSize);  // a quick push changes nothing

    h.push(4, 3000);
    CHECK(h.settings.cloudSize == Settings{}.cloudSize);
    CHECK(h.settings.girth != Settings{}.girth);  // only that knob's setting
    h.push(6, 3000);
    CHECK(h.settings == Settings{});
}

TEST_CASE("knob 8's push is button C: Status page, Re-level, Mounting Calibration") {
    Hand h;
    h.push(8, 100);
    REQUIRE(h.events.size() == 1);
    CHECK(h.events[0].kind == ControlEvent::Kind::WakeDisplay);
    h.events.clear();
    h.push(8, 3000);
    REQUIRE(h.events.size() == 1);
    CHECK(h.events[0].kind == ControlEvent::Kind::Relevel);
    h.events.clear();
    h.push(8, 6000);
    REQUIRE(h.events.size() == 1);
    CHECK(h.events[0].kind == ControlEvent::Kind::CalibrateMounting);
}

TEST_CASE("a push in progress shows which knob and what letting go will do") {
    Hand h;
    h.unit.pushed = 1 << 7;
    for (int i = 0; i < 40; ++i) h.read();  // 0.8 s
    KnobPanel::PressInProgress p = h.panel.pressInProgress(h.now);
    CHECK(p.knob == 7);
    CHECK(p.segments == 1);
    CHECK(p.onRelease == ButtonTracker::Press::Quick);
    for (int i = 0; i < 75; ++i) h.read();  // 2.3 s
    CHECK(h.panel.pressInProgress(h.now).onRelease == ButtonTracker::Press::Long);
    h.unit.pushed = 0;
    for (int i = 0; i < 5; ++i) h.read();
    CHECK(h.panel.pressInProgress(h.now).knob == -1);

    h.unit.pushed = 1 << 6;  // the spare knob shows nothing
    for (int i = 0; i < 40; ++i) h.read();
    CHECK(h.panel.pressInProgress(h.now).knob == -1);
}

TEST_CASE("a unit that restarts (counts back to 0) doesn't spin anything") {
    Hand h;
    for (int i = 0; i < 30; ++i) h.click(1, 1);
    const uint8_t palette = h.settings.palette;
    h.events.clear();
    for (int k = 0; k < KnobReading::kKnobs; ++k) h.unit.counts[k] = 0;
    h.read();
    CHECK(h.settings.palette == palette);
    CHECK(h.events.empty());
    h.click(1, 1);  // and turning afterwards works again
    CHECK(h.events.size() == 1);
}

TEST_CASE("counts wrapping past the int32 limit still step normally") {
    Hand h;
    h.unit.counts[1] = INT32_MAX - 1;
    h.read();
    h.turn(2, kStep);
    CHECK(h.settings.brightness == 3);
}

TEST_CASE("settings changed by knobs are saved once the knobs go quiet") {
    Hand h;
    CHECK(!h.controls.shouldSave(h.now));
    h.click(1, 1);
    CHECK(!h.controls.shouldSave(h.now + 100));
    CHECK(h.controls.shouldSave(h.now + Controls::kSaveQuietMs));
    h.controls.markSaved();
    h.click(2, 5);  // runs into the end: a turn that changes nothing isn't saved
    h.controls.markSaved();
    h.click(2, 1);
    CHECK(!h.controls.shouldSave(h.now + Controls::kSaveQuietMs));
}
