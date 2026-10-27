// Holding the talk button listens for a spoken command (kd/voice_commands.h).
// The button keeps its own presses: a command heard while it is held cancels
// the press (Controls::cancel), and its release waits, briefly, for the
// recognizer to finish, so a command spoken just before letting go still
// cancels it. A tap too short to hold two words skips the recognizer, so
// the button's own press comes at once.
//
// Sits between the ButtonLog and Controls: every sample is offered to
// admit() before Controls sees it.
#pragma once

#include <cstdint>

#include "controls.h"

namespace kd {

class PushToTalk {
public:
    // Longest a release waits for the recognizer.
    static constexpr uint32_t kMaxWaitMs = 1000;
    // Shorter presses are taps: nothing said, so no waiting.
    static constexpr uint32_t kMinTalkMs = 400;

    explicit PushToTalk(Button talk) : talk_(talk) {}

    Button talk() const { return talk_; }

    // What the recognizer should do now: start, finish what it heard, or
    // drop it (a tap).
    enum class Ask : uint8_t { None, Listen, Finish, Cancel };
    struct Admit {
        bool now = true;  // false: hold the sample back and offer it again later
        Ask ask = Ask::None;
    };

    // The next button sample (bit i: button i down, as in ButtonLog), taken
    // at sampleMs.
    Admit admit(uint8_t down, uint32_t sampleMs, uint32_t nowMs);

    // The recognizer is done with this press; it reports every command it
    // heard before saying so.
    void finished() { finished_ = true; }

    // The talk button is held, or its release is waiting for the recognizer.
    bool talking() const { return state_ != State::Idle; }
    // A release is being held back.
    bool waiting() const { return state_ == State::Finishing; }

private:
    enum class State : uint8_t { Idle, Listening, Finishing };
    Button talk_;
    State state_ = State::Idle;
    bool finished_ = false;
    uint32_t waitFromMs_ = 0;
    uint32_t pressedAtMs_ = 0;
};

}  // namespace kd
