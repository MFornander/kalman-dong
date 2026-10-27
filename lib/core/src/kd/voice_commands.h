// Spoken commands, heard while the talk button is held (kd/push_to_talk.h).
// Every phrase is two words: the recognizer tells short phrases apart much
// less reliably than ones of a few syllables.
#pragma once

#include <cstddef>
#include <cstdint>

#include "controls.h"

namespace kd {

enum class VoiceCommand : uint8_t {
    NextPalette,
    PreviousPalette,
    Brighter,
    Dimmer,
    Faster,
    Slower,
    Freeze,
    BiggerClouds,
    SmallerClouds,
    MoreClouds,
    FewerClouds,
    SoundOn,
    SoundOff,
    Relevel,
    ShowStatus,
    Count
};

struct VoicePhrase {
    VoiceCommand command;
    const char* words;  // lower case, two words
};

// Every phrase the recognizer listens for; several may share a command.
const VoicePhrase* voicePhrases(size_t& count);

// A short name for logs, e.g. "next palette".
const char* voiceCommandName(VoiceCommand c);

// What a heard command does, as the matching knob or button would.
ControlEvent applyVoice(VoiceCommand c, Controls& controls, uint32_t nowMs);

}  // namespace kd
