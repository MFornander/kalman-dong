#include "voice_commands.h"

namespace kd {

namespace {

const VoicePhrase kPhrases[] = {
    {VoiceCommand::NextPalette, "next palette"},
    {VoiceCommand::NextPalette, "next colors"},
    {VoiceCommand::PreviousPalette, "previous palette"},
    {VoiceCommand::PreviousPalette, "previous colors"},
    {VoiceCommand::Brighter, "lights up"},
    {VoiceCommand::Brighter, "more light"},
    {VoiceCommand::Dimmer, "lights down"},
    {VoiceCommand::Dimmer, "less light"},
    {VoiceCommand::Faster, "go faster"},
    {VoiceCommand::Faster, "speed up"},
    {VoiceCommand::Slower, "go slower"},
    {VoiceCommand::Slower, "slow down"},
    {VoiceCommand::Freeze, "freeze clouds"},
    {VoiceCommand::Freeze, "stop clouds"},
    {VoiceCommand::BiggerClouds, "bigger clouds"},
    {VoiceCommand::SmallerClouds, "smaller clouds"},
    {VoiceCommand::MoreClouds, "more clouds"},
    {VoiceCommand::FewerClouds, "fewer clouds"},
    {VoiceCommand::FewerClouds, "more sky"},
    {VoiceCommand::SoundOn, "sound on"},
    {VoiceCommand::SoundOff, "sound off"},
    {VoiceCommand::Relevel, "reset heading"},
    {VoiceCommand::ShowStatus, "show status"},
};

const char* const kNames[] = {
    "next palette", "previous palette", "brighter",    "dimmer",   "faster",    "slower",
    "freeze",       "bigger clouds",    "smaller clouds", "more clouds", "fewer clouds", "sound on",
    "sound off",    "re-level",         "show status",
};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(VoiceCommand::Count), "a name per command");

}  // namespace

const VoicePhrase* voicePhrases(size_t& count) {
    count = sizeof(kPhrases) / sizeof(kPhrases[0]);
    return kPhrases;
}

const char* voiceCommandName(VoiceCommand c) {
    return c < VoiceCommand::Count ? kNames[static_cast<int>(c)] : "?";
}

ControlEvent applyVoice(VoiceCommand c, Controls& controls, uint32_t nowMs) {
    switch (c) {
        case VoiceCommand::NextPalette: return controls.turn(Setting::Palette, 1, nowMs);
        case VoiceCommand::PreviousPalette: return controls.turn(Setting::Palette, -1, nowMs);
        case VoiceCommand::Brighter: return controls.turn(Setting::Brightness, 1, nowMs);
        case VoiceCommand::Dimmer: return controls.turn(Setting::Brightness, -1, nowMs);
        case VoiceCommand::Faster: return controls.turn(Setting::Motion, 1, nowMs);
        case VoiceCommand::Slower: return controls.turn(Setting::Motion, -1, nowMs);
        case VoiceCommand::Freeze: return controls.turn(Setting::Motion, -8, nowMs);  // stops at frozen
        case VoiceCommand::BiggerClouds: return controls.turn(Setting::CloudSize, 1, nowMs);
        case VoiceCommand::SmallerClouds: return controls.turn(Setting::CloudSize, -1, nowMs);
        case VoiceCommand::MoreClouds: return controls.turn(Setting::ClearSky, -1, nowMs);
        case VoiceCommand::FewerClouds: return controls.turn(Setting::ClearSky, 1, nowMs);
        case VoiceCommand::SoundOn: return {ControlEvent::Kind::SoundOn, Setting::None};
        case VoiceCommand::SoundOff: return {ControlEvent::Kind::SoundOff, Setting::None};
        case VoiceCommand::Relevel: return {ControlEvent::Kind::Relevel, Setting::None};
        case VoiceCommand::ShowStatus: return {ControlEvent::Kind::WakeDisplay, Setting::None};
        case VoiceCommand::Count: break;
    }
    return {};
}

}  // namespace kd
