// Stress tests for turning button levels into commands (kd/controls.h,
// kd/button_log.h): random press scripts with contact bounce, fed the way the
// firmware feeds them, checked against what each press should have done.
#include "../doctest_main.h"

#include <algorithm>
#include <iterator>
#include <random>
#include <vector>

#include "kd/button_log.h"
#include "kd/color.h"
#include "kd/controls.h"

using namespace kd;
using Press = ButtonTracker::Press;

namespace {

// ---- A script of physical presses -----------------------------------------

struct PhysicalPress {
    int button;
    uint32_t startMs;  // offsets from the script's start
    uint32_t durationMs;
    Press expected;
};

// One button's signal: the times its level changes, contact bounce included.
struct Signal {
    std::vector<std::pair<uint32_t, bool>> changes;  // (offset ms, down)

    bool downAt(uint32_t t) const {
        auto after = std::upper_bound(changes.begin(), changes.end(), t,
                                      [](uint32_t v, const std::pair<uint32_t, bool>& c) { return v < c.first; });
        return after != changes.begin() && std::prev(after)->second;
    }
};

struct Script {
    std::vector<PhysicalPress> presses;
    Signal signals[3];
    uint32_t endMs = 0;  // every button is up from here on

    uint8_t downBits(uint32_t t) const {
        uint8_t bits = 0;
        for (int b = 0; b < 3; ++b) bits |= static_cast<uint8_t>(signals[b].downAt(t)) << b;
        return bits;
    }
};

// Durations stay this far from the 2 s and 5 s thresholds in random scripts,
// so sampling and bounce can't honestly move a press across one. The
// thresholds themselves are tested exactly below.
constexpr uint32_t kMargin = 60;
constexpr uint32_t kBounceMs = 10;  // well inside the 25 ms debounce

Press classify(uint32_t durationMs) {
    return durationMs >= ButtonTracker::kHeldMs   ? Press::Held
           : durationMs >= ButtonTracker::kLongMs ? Press::Long
                                                  : Press::Quick;
}

// Adds a level change at t, with a burst of contact bounce after it.
void addEdge(Signal& s, uint32_t t, bool down, std::mt19937& rng, bool bounce) {
    if (bounce) {
        const int flips = std::uniform_int_distribution<int>(0, 2)(rng) * 2;  // even: ends where it started
        std::vector<uint32_t> at;
        for (int i = 0; i < flips; ++i) at.push_back(t + std::uniform_int_distribution<uint32_t>(1, kBounceMs)(rng));
        std::sort(at.begin(), at.end());
        s.changes.push_back({t, down});
        bool level = down;
        for (uint32_t a : at) s.changes.push_back({a, level = !level});
        return;
    }
    s.changes.push_back({t, down});
}

// Each button gets its own presses; buttons overlap one another freely. Edges
// are added in time order (gaps outlast the bounce), so changes stay sorted.
Script randomScript(std::mt19937& rng, int pressesPerButton, bool bounce) {
    Script sc;
    auto pick = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(rng); };
    for (int b = 0; b < 3; ++b) {
        uint32_t t = pick(0, 500);
        for (int i = 0; i < pressesPerButton; ++i) {
            uint32_t d;
            switch (pick(0, 9)) {
                case 0: d = pick(ButtonTracker::kHeldMs + kMargin, 8000); break;                          // Held
                case 1: case 2: d = pick(ButtonTracker::kLongMs + kMargin, ButtonTracker::kHeldMs - kMargin); break;  // Long
                default: d = pick(80, ButtonTracker::kLongMs - kMargin); break;                           // Quick
            }
            sc.presses.push_back({b, t, d, classify(d)});
            addEdge(sc.signals[b], t, true, rng, bounce);
            addEdge(sc.signals[b], t + d, false, rng, bounce);
            t += d + pick(80, 1500);
        }
        sc.endMs = std::max(sc.endMs, t + 200);
    }
    return sc;
}

// ---- What the presses should do -------------------------------------------

ControlEvent expectedEvent(int button, Press p) {
    using K = ControlEvent::Kind;
    static const ControlEvent kTable[3][3] = {
        {{K::Changed, Setting::Palette}, {K::Changed, Setting::CloudSize}, {K::Changed, Setting::ClearSky}},
        {{K::Changed, Setting::Motion}, {K::Changed, Setting::Brightness}, {K::Changed, Setting::Girth}},
        {{K::WakeDisplay, Setting::None}, {K::Relevel, Setting::None}, {K::CalibrateMounting, Setting::None}},
    };
    const int col = p == Press::Quick ? 0 : p == Press::Long ? 1 : 2;
    return kTable[button][col];
}

bool same(const ControlEvent& a, const ControlEvent& b) { return a.kind == b.kind && a.changed == b.changed; }

// Settings after the script, worked out by counting presses.
Settings expectedSettings(const Script& sc, const Settings& start) {
    Settings s = start;
    for (const PhysicalPress& p : sc.presses) {
        const ControlEvent e = expectedEvent(p.button, p.expected);
        auto step = [](uint8_t& v) { ++v; };
        switch (e.changed) {
            case Setting::Palette: step(s.palette); break;
            case Setting::CloudSize: step(s.cloudSize); break;
            case Setting::ClearSky: step(s.clearSky); break;
            case Setting::Motion: step(s.motion); break;
            case Setting::Brightness: step(s.brightness); break;
            case Setting::Girth: step(s.girth); break;
            case Setting::QuietGlow: step(s.quietGlow); break;  // a knob's, never a button's
            case Setting::None: break;
        }
    }
    s.sanitize();  // every setting wraps around at the end of its steps
    return s;
}

// Collects every event, and which button it came from, as Controls reports it.
struct Recorder {
    Settings settings;
    Controls controls{settings};
    std::vector<ControlEvent> perButton[3];
    int unexpected = 0;

    void feed(uint8_t bits, uint32_t ms) {
        for (const ControlEvent& e : controls.update(bits & 1, bits & 2, bits & 4, ms)) {
            const int b = buttonOf(e);
            if (b < 0) {
                ++unexpected;
                continue;
            }
            perButton[b].push_back(e);
        }
    }

    static int buttonOf(const ControlEvent& e) {
        for (int b = 0; b < 3; ++b)
            for (Press p : {Press::Quick, Press::Long, Press::Held})
                if (same(e, expectedEvent(b, p))) return b;
        return -1;
    }
};

// Every press did exactly what it should, in order, button by button.
void checkAgainst(const Script& sc, const Recorder& r, const Settings& start) {
    CHECK(r.unexpected == 0);
    for (int b = 0; b < 3; ++b) {
        std::vector<ControlEvent> want;
        for (const PhysicalPress& p : sc.presses)
            if (p.button == b) want.push_back(expectedEvent(b, p.expected));
        INFO("button ", "ABC"[b]);
        REQUIRE(r.perButton[b].size() == want.size());
        for (size_t i = 0; i < want.size(); ++i) {
            INFO("press ", i);
            CHECK(same(r.perButton[b][i], want[i]));
        }
    }
    CHECK(r.settings == expectedSettings(sc, start));
}

// Feeds a script the way the firmware does: a sampler on a steady timer
// queues level changes in a ButtonLog; the main loop drains it whenever it
// gets to run, then feeds the current level at the time it reads afterwards.
// loopGaps yields how long each main-loop pass takes (stalls included).
template <typename Gaps>
void runThroughLog(const Script& sc, uint32_t t0, uint32_t samplerMs, Gaps loopGap, Recorder& r) {
    ButtonLog log;
    uint8_t current = 0;
    uint32_t t = 0, nextLoop = 0;
    for (; t <= sc.endMs + 6000; t += samplerMs) {
        log.sample(sc.downBits(t), t0 + t);
        while (nextLoop <= t) {
            ButtonLog::Sample s;
            while (log.pop(s)) {
                r.feed(s.down, s.ms);
                current = s.down;
            }
            r.feed(current, t0 + t);  // the main loop's millis(), read after draining
            nextLoop += loopGap();
        }
    }
}

}  // namespace

// ---- Exact thresholds -------------------------------------------------------

TEST_CASE("a press is Quick below 2 s, Long from 2 s, Held from 5 s, to the millisecond") {
    struct Case {
        uint32_t ms;
        Press want;
    };
    for (const Case& c : {Case{30, Press::Quick}, Case{1999, Press::Quick}, Case{2000, Press::Long},
                          Case{4999, Press::Long}, Case{5000, Press::Held}, Case{12000, Press::Held}}) {
        INFO("held for ", c.ms, " ms");
        ButtonTracker bt;
        std::vector<Press> got;
        uint32_t t = 1000;
        for (uint32_t i = 0; i < c.ms; ++i, ++t)
            if (Press p = bt.update(true, t); p != Press::None) got.push_back(p);
        for (int i = 0; i < 100; ++i, ++t)
            if (Press p = bt.update(false, t); p != Press::None) got.push_back(p);
        REQUIRE(got.size() == 1);
        CHECK(got[0] == c.want);
    }
}

TEST_CASE("Held fires while the button is still down, and letting go adds nothing") {
    ButtonTracker bt;
    uint32_t t = 0, firedAt = 0;
    int events = 0;
    for (; t < 7000; ++t)
        if (bt.update(true, t) == Press::Held) {
            ++events;
            firedAt = t;
        }
    CHECK(events == 1);
    CHECK(firedAt == ButtonTracker::kHeldMs);
    for (int i = 0; i < 200; ++i, ++t) CHECK(bt.update(false, t) == Press::None);
}

TEST_CASE("a release just before 5 s is Long even when it is only confirmed after 5 s") {
    ButtonTracker bt;
    std::vector<Press> got;
    for (uint32_t t = 0; t < 4990; ++t)
        if (Press p = bt.update(true, t); p != Press::None) got.push_back(p);
    // Let go at 4990; the next look is at 5010, before the debounce settles.
    for (uint32_t t : {4990u, 5010u, 5020u, 5100u})
        if (Press p = bt.update(false, t); p != Press::None) got.push_back(p);
    REQUIRE(got.size() == 1);
    CHECK(got[0] == Press::Long);
}

// ---- Sparse and uneven sampling ------------------------------------------

TEST_CASE("a press seen in only two samples still counts, timed from its edges") {
    ButtonTracker bt;
    CHECK(bt.update(true, 100) == Press::None);
    CHECK(bt.update(false, 250) == Press::None);  // let go 150 ms later
    CHECK(bt.update(false, 4000) == Press::Quick);
}

TEST_CASE("a tap is still Quick when the next look at it comes much later") {
    // The firmware's main loop once stalled for 2 s in a USB log write; the
    // samples queued meanwhile must time the press, not the stall.
    ButtonTracker bt;
    bt.update(true, 1000);
    bt.update(false, 1150);
    CHECK(bt.update(false, 3500) == Press::Quick);
    CHECK(bt.update(false, 9000) == Press::None);
}

TEST_CASE("a glitch shorter than the debounce doesn't split a long press") {
    ButtonTracker bt;
    std::vector<Press> got;
    uint32_t t = 0;
    for (; t < 1500; t += 5) bt.update(true, t);
    bt.update(false, t);  // 10 ms of contact chatter
    bt.update(true, t + 10);
    for (t += 10; t < 3000; t += 5)
        if (Press p = bt.update(true, t); p != Press::None) got.push_back(p);
    for (int i = 0; i < 20; ++i, t += 5)
        if (Press p = bt.update(false, t); p != Press::None) got.push_back(p);
    REQUIRE(got.size() == 1);
    CHECK(got[0] == Press::Long);
}

TEST_CASE("presses time correctly across the millis() wraparound") {
    ButtonTracker bt;
    const uint32_t start = 0xFFFFFFFFu - 700;  // 49.7 days after power-on
    std::vector<Press> got;
    uint32_t t = start;
    for (uint32_t i = 0; i < 2500; i += 5, t += 5)
        if (Press p = bt.update(true, t); p != Press::None) got.push_back(p);
    for (int i = 0; i < 20; ++i, t += 5)
        if (Press p = bt.update(false, t); p != Press::None) got.push_back(p);
    REQUIRE(got.size() == 1);
    CHECK(got[0] == Press::Long);
}

// ---- Several buttons -------------------------------------------------------

TEST_CASE("buttons let go in the same sample both report their command") {
    Settings s;
    Controls c(s);
    for (uint32_t t = 0; t < 300; t += 5) c.update(true, false, true, t);
    ControlEvents events;
    for (uint32_t t = 300; t < 400; t += 5) {
        ControlEvents e = c.update(false, false, false, t);
        if (e.count) events = e;
    }
    REQUIRE(events.count == 2);
    CHECK(same(events.events[0], {ControlEvent::Kind::Changed, Setting::Palette}));
    CHECK(same(events.events[1], {ControlEvent::Kind::WakeDisplay, Setting::None}));
    CHECK(s.palette == 1);
}

TEST_CASE("time running backwards (a racing sampler) can't fake a press") {
    Settings s;
    Controls c(s);
    for (uint32_t t = 1000; t < 1200; t += 5) c.update(false, false, false, t);
    int events = 0;
    events += c.update(true, false, false, 1200).count;
    events += c.update(true, false, false, 1199).count;  // a sample stamped 1 ms earlier
    events += c.update(false, false, false, 1201).count;
    for (uint32_t t = 1201; t < 1400; t += 5) events += c.update(false, false, false, t).count;
    CHECK(events == 0);  // down for 1 ms: contact noise, not a press
    CHECK(s.palette == 0);
}

// ---- The press bar --------------------------------------------------------

TEST_CASE("the press bar only grows during a press and always says what letting go does") {
    std::mt19937 rng(7);
    const Script sc = randomScript(rng, 12, true);
    Settings s;
    Controls c(s);
    int lastButton = -1, lastSegments = 0;
    for (uint32_t t = 0; t <= sc.endMs; t += 7) {
        c.update(sc.signals[0].downAt(t), sc.signals[1].downAt(t), sc.signals[2].downAt(t), t);
        const Controls::PressInProgress p = c.pressInProgress(t);
        if (p.button < 0) {
            lastButton = -1;
            continue;
        }
        CHECK(p.segments >= 1);
        CHECK(p.segments <= Controls::kSegments);
        const Press want = p.segments >= Controls::kSegments       ? Press::None
                           : p.segments >= Controls::kLongSegments ? Press::Long
                                                                   : Press::Quick;
        CHECK(p.onRelease == want);
        if (p.button == lastButton) CHECK(p.segments >= lastSegments);
        lastButton = p.button;
        lastSegments = p.segments;
    }
}

// ---- Random press scripts --------------------------------------------------

TEST_CASE("random presses with bounce, sampled by an uneven main loop") {
    for (unsigned seed = 1; seed <= 150; ++seed) {
        INFO("seed ", seed);
        std::mt19937 rng(seed);
        const Script sc = randomScript(rng, 25, true);
        Recorder r;
        const Settings start = r.settings;
        const uint32_t t0 = std::uniform_int_distribution<uint32_t>(0, 1u << 30)(rng);
        for (uint32_t t = 0; t <= sc.endMs + 6000; t += std::uniform_int_distribution<uint32_t>(1, 20)(rng))
            r.feed(sc.downBits(t), t0 + t);
        checkAgainst(sc, r, start);
    }
}

TEST_CASE("random presses through the ButtonLog, with main-loop stalls of up to 3 s") {
    for (unsigned seed = 1; seed <= 150; ++seed) {
        INFO("seed ", seed);
        std::mt19937 rng(seed * 7919u);
        const Script sc = randomScript(rng, 25, true);
        Recorder r;
        const Settings start = r.settings;
        auto gap = [&] {
            const uint32_t roll = std::uniform_int_distribution<uint32_t>(0, 99)(rng);
            if (roll < 3) return std::uniform_int_distribution<uint32_t>(500, 3000)(rng);  // a stall
            return std::uniform_int_distribution<uint32_t>(3, 30)(rng);
        };
        runThroughLog(sc, 0xFFFFFFFFu - 30000 + seed, 5, gap, r);  // crosses the wraparound too
        checkAgainst(sc, r, start);
    }
}

TEST_CASE("a tap made entirely inside a long stall is still a Quick press") {
    Script sc;
    sc.presses.push_back({2, 1000, 150, Press::Quick});
    sc.signals[2].changes = {{1000, true}, {1150, false}};
    sc.endMs = 1500;
    Recorder r;
    // The main loop runs once at 0 and then stalls for 2.5 s.
    std::vector<uint32_t> gaps = {2500};
    size_t i = 0;
    runThroughLog(sc, 0, 5, [&] { return i < gaps.size() ? gaps[i++] : 10u; }, r);
    checkAgainst(sc, r, Settings{});
}

TEST_CASE("palettes wrap around however many times they are stepped") {
    Settings s;
    Controls c(s);
    uint32_t t = 0;
    const size_t taps = paletteCount() * 5 + 2;
    for (size_t i = 0; i < taps; ++i) {
        const uint8_t before = s.palette;
        for (uint32_t k = 0; k < 100; k += 5) c.update(true, false, false, t += 5);
        ControlEvents e;
        for (uint32_t k = 0; k < 100; k += 5)
            if (ControlEvents got = c.update(false, false, false, t += 5); got.count) e = got;
        REQUIRE(e.count == 1);
        CHECK(e.events[0].changed == Setting::Palette);
        CHECK(s.palette == (before + 1) % paletteCount());
    }
    CHECK(s.palette == taps % paletteCount());
}

// ---- The log itself -------------------------------------------------------

TEST_CASE("the ButtonLog keeps changes in order and only changes") {
    ButtonLog log;
    log.sample(0, 10);  // same as the starting level: nothing to queue
    log.sample(1, 20);
    log.sample(1, 25);
    log.sample(5, 30);
    log.sample(0, 40);
    ButtonLog::Sample s;
    std::vector<std::pair<uint8_t, uint32_t>> got;
    while (log.pop(s)) got.push_back({s.down, s.ms});
    CHECK(got == std::vector<std::pair<uint8_t, uint32_t>>{{1, 20}, {5, 30}, {0, 40}});
}

TEST_CASE("a full ButtonLog catches up with the latest level once there is room") {
    ButtonLog log;
    uint32_t t = 0;
    for (uint32_t i = 0; i < ButtonLog::kCapacity * 3; ++i) log.sample(i % 2 ? 0 : 1, t += 5);
    // Nobody drained it; the last level the sampler saw is "down".
    log.sample(1, t += 5);
    ButtonLog::Sample s;
    uint32_t lastMs = 0;
    uint8_t last = 0;
    int n = 0;
    while (log.pop(s)) {
        CHECK(s.ms > lastMs);
        lastMs = s.ms;
        last = s.down;
        ++n;
    }
    CHECK(n == static_cast<int>(ButtonLog::kCapacity));
    log.sample(1, t += 5);  // room again: the current level is queued if it differs
    while (log.pop(s)) last = s.down;
    CHECK(last == 1);
}
