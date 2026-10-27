#include "push_to_talk.h"

namespace kd {

PushToTalk::Admit PushToTalk::admit(uint8_t down, uint32_t nowMs) {
    const bool talkDown = (down >> static_cast<int>(talk_)) & 1;
    switch (state_) {
        case State::Idle:
            if (!talkDown) return {};
            state_ = State::Listening;
            finished_ = false;
            return {true, Ask::Listen};
        case State::Listening:
            if (talkDown) return {};
            state_ = State::Finishing;
            waitFromMs_ = nowMs;
            return {false, Ask::Finish};
        case State::Finishing:
            if (!finished_ && nowMs - waitFromMs_ < kMaxWaitMs) return {false, Ask::None};
            state_ = State::Idle;
            return {};
    }
    return {};
}

}  // namespace kd
