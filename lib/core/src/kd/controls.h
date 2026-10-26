// The wielder's settings and the three buttons that change them.
//
//   A short: next Palette          A long: step Clear Sky
//   B short: step brightness       B long: step Girth
//   C short: wake the display      C long: Re-level
//   A + C held together: Mounting Calibration
#pragma once

#include <cstdint>

namespace kd {

// Persisted across power cycles. Each field is an index into a fixed table of
// steps so a saved value can never be out of range.
struct Settings {
    uint8_t palette = 0;
    uint8_t brightness = 2;
    uint8_t girth = 2;
    uint8_t clearSky = 1;

    float brightnessValue() const;  // 0..1
    float girthValue() const;       // m
    float clearSkyValue() const;    // 0..1 share of the Cloud Field

    // Clamps every field into range (e.g. after loading from flash).
    void sanitize();

    bool operator==(const Settings& o) const {
        return palette == o.palette && brightness == o.brightness && girth == o.girth && clearSky == o.clearSky;
    }
};

enum class Button : uint8_t { A, B, C };

// Debounces one button and reports presses. A long press fires as soon as the
// button has been held long enough, not on release.
class ButtonTracker {
public:
    enum class Press : uint8_t { None, Short, Long };

    static constexpr uint32_t kDebounceMs = 25;
    static constexpr uint32_t kLongMs = 600;

    Press update(bool down, uint32_t nowMs);

    bool isDown() const { return stable_; }

    // Swallows the current press: no Short or Long until it is released.
    void cancel() { longFired_ = true; }

private:
    bool stable_ = false;
    bool raw_ = false;
    uint32_t rawSinceMs_ = 0;
    uint32_t pressedAtMs_ = 0;
    bool longFired_ = false;
};

enum class Setting : uint8_t { None, Palette, ClearSky, Brightness, Girth };

struct ControlEvent {
    enum class Kind : uint8_t { None, Changed, WakeDisplay, Relevel, CalibrateMounting };
    Kind kind = Kind::None;
    Setting changed = Setting::None;
};

class Controls {
public:
    static constexpr uint32_t kSaveQuietMs = 3000;
    static constexpr uint32_t kComboMs = 1500;  // A + C held this long

    explicit Controls(Settings& settings) : settings_(settings) {}

    // Feed the raw (debounced here) button levels; returns at most one event.
    ControlEvent update(bool aDown, bool bDown, bool cDown, uint32_t nowMs);

    // True once settings changed and no button has changed them for a while.
    bool shouldSave(uint32_t nowMs) const { return dirty_ && nowMs - lastChangeMs_ >= kSaveQuietMs; }
    void markSaved() { dirty_ = false; }

private:
    ControlEvent apply(Button b, ButtonTracker::Press p, uint32_t nowMs);

    Settings& settings_;
    ButtonTracker buttons_[3];
    bool dirty_ = false;
    uint32_t lastChangeMs_ = 0;
    bool comboHeld_ = false;
    bool comboFired_ = false;
    uint32_t comboSinceMs_ = 0;
};

}  // namespace kd
