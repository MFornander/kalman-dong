#include "../doctest_main.h"

#include <cctype>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "kd/controls.h"
#include "kd/push_to_talk.h"
#include "kd/voice_commands.h"

using namespace kd;

TEST_CASE("every phrase is two lower-case words, and every command has one") {
    size_t n = 0;
    const VoicePhrase* phrases = voicePhrases(n);
    REQUIRE(n > 0);
    std::set<int> commands;
    std::set<std::string> seen;
    for (size_t i = 0; i < n; ++i) {
        const std::string w = phrases[i].words;
        CAPTURE(w);
        int spaces = 0;
        for (char ch : w) {
            spaces += ch == ' ';
            CHECK((std::islower(static_cast<unsigned char>(ch)) || ch == ' '));
        }
        CHECK(spaces == 1);
        CHECK(w.front() != ' ');
        CHECK(w.back() != ' ');
        CHECK(seen.insert(w).second);  // no phrase twice
        commands.insert(static_cast<int>(phrases[i].command));
    }
    CHECK(commands.size() == static_cast<size_t>(VoiceCommand::Count));
    for (int c = 0; c < static_cast<int>(VoiceCommand::Count); ++c)
        CHECK(std::strcmp(voiceCommandName(static_cast<VoiceCommand>(c)), "?") != 0);
}

TEST_CASE("spoken commands do what the knobs and buttons do") {
    Settings s;
    Controls c{s};
    uint32_t now = 1000;

    CHECK(applyVoice(VoiceCommand::NextPalette, c, now).changed == Setting::Palette);
    CHECK(s.palette == 1);
    applyVoice(VoiceCommand::PreviousPalette, c, now);
    CHECK(s.palette == 0);

    const uint8_t brightness = s.brightness;
    CHECK(applyVoice(VoiceCommand::Brighter, c, now).changed == Setting::Brightness);
    CHECK(s.brightness == brightness + 1);
    applyVoice(VoiceCommand::Dimmer, c, now);
    CHECK(s.brightness == brightness);

    applyVoice(VoiceCommand::Faster, c, now);
    applyVoice(VoiceCommand::Faster, c, now);
    CHECK(s.motionValue() == doctest::Approx(2.f));
    CHECK(applyVoice(VoiceCommand::Freeze, c, now).changed == Setting::Motion);
    CHECK(s.motionValue() == 0.f);

    const float sky = s.clearSkyValue();
    applyVoice(VoiceCommand::MoreClouds, c, now);
    CHECK(s.clearSkyValue() < sky);
    applyVoice(VoiceCommand::FewerClouds, c, now);
    CHECK(s.clearSkyValue() == doctest::Approx(sky));

    const float size = s.cloudSizeValue();
    applyVoice(VoiceCommand::BiggerClouds, c, now);
    CHECK(s.cloudSizeValue() > size);

    CHECK(applyVoice(VoiceCommand::SoundOn, c, now).kind == ControlEvent::Kind::SoundOn);
    CHECK(applyVoice(VoiceCommand::SoundOff, c, now).kind == ControlEvent::Kind::SoundOff);
    CHECK(applyVoice(VoiceCommand::Relevel, c, now).kind == ControlEvent::Kind::Relevel);
    CHECK(applyVoice(VoiceCommand::ShowStatus, c, now).kind == ControlEvent::Kind::WakeDisplay);
}

TEST_CASE("a cancelled press does nothing, then or when let go") {
    Settings s;
    Controls c{s};
    uint32_t now = 1000;
    int events = 0;
    for (int i = 0; i < 40; ++i) events += c.update(true, false, false, now += 5).count;
    c.cancel(Button::A);
    CHECK(c.pressInProgress(now).button == -1);
    for (int i = 0; i < 1200; ++i) events += c.update(true, false, false, now += 5).count;  // past Held
    for (int i = 0; i < 20; ++i) events += c.update(false, false, false, now += 5).count;
    CHECK(events == 0);
    CHECK(s.palette == 0);
    // The next press is a press again.
    for (int i = 0; i < 20; ++i) c.update(true, false, false, now += 5);
    for (int i = 0; i < 20; ++i) c.update(false, false, false, now += 5);
    CHECK(s.palette == 1);
}

namespace {

// The main loop in miniature: button samples queue up, each is offered to
// PushToTalk before Controls sees it, and a pretend recognizer finishes
// `recognizeMs` after it is asked to, having heard `heard` (if anything).
struct Bench {
    Settings settings;
    Controls controls{settings};
    PushToTalk ptt{Button::A};
    std::deque<std::pair<uint8_t, uint32_t>> log;
    uint32_t now = 1000;
    bool listening = false;
    int listens = 0;
    uint32_t finishAtMs = 0;
    bool finishing = false;
    uint32_t recognizeMs = 150;
    bool hear = false;  // the recognizer hears a command during the press
    uint8_t admitted = 0;  // the latest level Controls has seen
    std::vector<ControlEvent> events;

    void run(uint32_t ms, bool aDown) {
        for (uint32_t t = 0; t < ms; t += 5) {
            now += 5;
            const uint8_t level = aDown ? 1 : 0;
            if (log.empty() || log.back().first != level) log.push_back({level, now});
            tick();
        }
    }
    void tick() {
        if (listening && hear) {  // a command, heard mid-press
            hear = false;
            controls.cancel(ptt.talk());
            events.push_back(applyVoice(VoiceCommand::BiggerClouds, controls, now));
        }
        if (finishing && now >= finishAtMs) {
            finishing = false;
            listening = false;
            ptt.finished();
        }
        while (!log.empty()) {
            const auto [down, ms] = log.front();
            const PushToTalk::Admit a = ptt.admit(down, now);
            if (a.ask == PushToTalk::Ask::Listen) {
                listening = true;
                ++listens;
            }
            if (a.ask == PushToTalk::Ask::Finish) {
                finishing = true;
                finishAtMs = now + recognizeMs;
            }
            if (!a.now) return;
            log.pop_front();
            admitted = down;
            for (const ControlEvent& e : controls.update(down & 1, false, false, ms)) events.push_back(e);
        }
        if (!ptt.waiting())
            for (const ControlEvent& e : controls.update(admitted & 1, false, false, now)) events.push_back(e);
    }
};

}  // namespace

TEST_CASE("holding the talk button listens, and a tap with nothing said is still a tap") {
    Bench b;
    b.run(200, true);
    CHECK(b.listens == 1);
    CHECK(b.ptt.talking());
    b.run(100, false);  // the release waits for the recognizer...
    CHECK(b.settings.palette == 0);
    CHECK(b.ptt.waiting());
    b.run(200, false);  // ...then counts as a Quick press, timed from its edges
    CHECK_FALSE(b.ptt.talking());
    CHECK(b.settings.palette == 1);
    CHECK(b.settings.cloudSize == Settings{}.cloudSize);
}

TEST_CASE("waiting for the recognizer doesn't turn a quick press into a long one") {
    Bench b;
    b.recognizeMs = 900;
    b.run(1800, true);
    b.run(1200, false);
    CHECK(b.settings.palette == 1);  // Quick (1.8 s), not Long (2.7 s with the wait)
    CHECK(b.settings.cloudSize == Settings{}.cloudSize);
}

TEST_CASE("a command heard while talking replaces the button's own press") {
    Bench b;
    b.run(300, true);
    b.hear = true;
    b.run(600, true);
    b.run(400, false);
    CHECK(b.settings.palette == 0);  // no Quick A
    CHECK(b.settings.cloudSize == Settings{}.cloudSize + 1);
    REQUIRE(b.events.size() == 1);
    CHECK(b.events[0].changed == Setting::CloudSize);
}

TEST_CASE("a command heard just after letting go still replaces the press") {
    Bench b;
    b.run(600, true);
    b.run(30, false);
    REQUIRE(b.ptt.waiting());
    b.hear = true;  // the recognizer is still working through the audio
    b.run(400, false);
    CHECK(b.settings.palette == 0);
    CHECK(b.settings.cloudSize == Settings{}.cloudSize + 1);
}

TEST_CASE("a recognizer that never finishes holds the release back at most a second") {
    Bench b;
    b.recognizeMs = 100000;
    b.run(200, true);
    b.run(PushToTalk::kMaxWaitMs - 100, false);
    CHECK(b.settings.palette == 0);
    b.run(200, false);
    CHECK(b.settings.palette == 1);
    CHECK_FALSE(b.ptt.talking());
}

TEST_CASE("other buttons pass straight through") {
    Settings s;
    Controls c{s};
    PushToTalk ptt{Button::A};
    uint32_t now = 1000;
    for (int i = 0; i < 20; ++i) {
        const PushToTalk::Admit a = ptt.admit(0b010, now += 5);
        CHECK(a.now);
        CHECK(a.ask == PushToTalk::Ask::None);
    }
    CHECK_FALSE(ptt.talking());
}
