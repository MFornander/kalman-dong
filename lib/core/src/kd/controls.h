// The wielder's settings and the three buttons that change them.
//
// Every button has three presses: Quick (let go before 2 s), Long (let go
// between 2 and 5 s) and Held (reaches 5 s; acts then, and letting go
// afterwards does nothing).
//
//        Quick              Long              Held
//   A    next Palette       step Clear Sky    -
//   B    step brightness    step Girth        -
//   C    wake the display   Re-level          Mounting Calibration
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

// Debounces one button and reports its presses: Quick or Long on release,
// Held the moment the press reaches kHeldMs (and then nothing on release).
class ButtonTracker {
public:
    enum class Press : uint8_t { None, Quick, Long, Held };

    static constexpr uint32_t kDebounceMs = 25;
    static constexpr uint32_t kLongMs = 2000;
    static constexpr uint32_t kHeldMs = 5000;

    Press update(bool down, uint32_t nowMs);

    bool isDown() const { return stable_; }

    // How long the current press has lasted; 0 when released.
    uint32_t heldMs(uint32_t nowMs) const { return stable_ ? nowMs - pressedAtMs_ : 0; }

private:
    bool stable_ = false;
    bool raw_ = false;
    uint32_t rawSinceMs_ = 0;
    uint32_t pressedAtMs_ = 0;
    bool held_ = false;
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
    // A press in progress is shown as a bar of half-second segments.
    static constexpr uint32_t kSegmentMs = 500;
    static constexpr int kSegments = ButtonTracker::kHeldMs / kSegmentMs;
    static constexpr int kLongSegments = ButtonTracker::kLongMs / kSegmentMs;

    explicit Controls(Settings& settings) : settings_(settings) {}

    // Feed the raw (debounced here) button levels; returns at most one event.
    ControlEvent update(bool aDown, bool bDown, bool cDown, uint32_t nowMs);

    // True once settings changed and no button has changed them for a while.
    bool shouldSave(uint32_t nowMs) const { return dirty_ && nowMs - lastChangeMs_ >= kSaveQuietMs; }

    // A press in progress, for showing what letting go will do.
    struct PressInProgress {
        int button = -1;   // 0, 1, 2 for A, B, C; -1 when none is held a segment yet
        int segments = 0;  // filled, one per kSegmentMs, up to kSegments
        ButtonTracker::Press onRelease = ButtonTracker::Press::None;  // Quick or Long; None once Held
    };
    PressInProgress pressInProgress(uint32_t nowMs) const;
    void markSaved() { dirty_ = false; }

private:
    ControlEvent apply(Button b, ButtonTracker::Press p, uint32_t nowMs);

    Settings& settings_;
    ButtonTracker buttons_[3];
    bool dirty_ = false;
    uint32_t lastChangeMs_ = 0;
};

}  // namespace kd
