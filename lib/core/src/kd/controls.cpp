#include "controls.h"

#include "color.h"

namespace kd {

namespace {

const float kBrightnessSteps[] = {0.25f, 0.5f, 0.75f, 1.0f};
const float kGirthSteps[] = {0.f, 0.15f, 0.3f, 0.6f};
const float kClearSkySteps[] = {0.f, 0.15f, 0.3f, 0.5f};

template <typename T, unsigned N>
constexpr unsigned count(const T (&)[N]) { return N; }

uint8_t next(uint8_t v, unsigned n) { return static_cast<uint8_t>((v + 1) % n); }

}  // namespace

float Settings::brightnessValue() const { return kBrightnessSteps[brightness % count(kBrightnessSteps)]; }
float Settings::girthValue() const { return kGirthSteps[girth % count(kGirthSteps)]; }
float Settings::clearSkyValue() const { return kClearSkySteps[clearSky % count(kClearSkySteps)]; }

void Settings::sanitize() {
    palette %= paletteCount();
    brightness %= count(kBrightnessSteps);
    girth %= count(kGirthSteps);
    clearSky %= count(kClearSkySteps);
}

ButtonTracker::Press ButtonTracker::update(bool down, uint32_t nowMs) {
    if (down != raw_) {
        raw_ = down;
        rawSinceMs_ = nowMs;
    }
    Press out = Press::None;
    if (raw_ != stable_ && nowMs - rawSinceMs_ >= kDebounceMs) {
        stable_ = raw_;
        if (stable_) {
            pressedAtMs_ = nowMs;
            longFired_ = false;
        } else if (!longFired_) {
            out = Press::Short;
        }
    }
    if (stable_ && !longFired_ && nowMs - pressedAtMs_ >= kLongMs) {
        longFired_ = true;
        out = Press::Long;
    }
    return out;
}

ControlEvent Controls::update(bool aDown, bool bDown, bool cDown, uint32_t nowMs) {
    const bool down[3] = {aDown, bDown, cDown};
    ButtonTracker::Press presses[3];
    for (int i = 0; i < 3; ++i) presses[i] = buttons_[i].update(down[i], nowMs);

    // A + C together is its own gesture: swallow both buttons' presses.
    ButtonTracker& a = buttons_[static_cast<int>(Button::A)];
    ButtonTracker& c = buttons_[static_cast<int>(Button::C)];
    if (a.isDown() && c.isDown()) {
        if (!comboHeld_) {
            comboHeld_ = true;
            comboFired_ = false;
            comboSinceMs_ = nowMs;
        }
        a.cancel();
        c.cancel();
        presses[static_cast<int>(Button::A)] = presses[static_cast<int>(Button::C)] = ButtonTracker::Press::None;
        if (!comboFired_ && nowMs - comboSinceMs_ >= kComboMs) {
            comboFired_ = true;
            return {ControlEvent::Kind::CalibrateMounting, Setting::None};
        }
    } else if (!a.isDown() && !c.isDown()) {
        comboHeld_ = false;
    }

    ControlEvent result;
    for (int i = 0; i < 3; ++i) {
        if (presses[i] == ButtonTracker::Press::None) continue;
        ControlEvent e = apply(static_cast<Button>(i), presses[i], nowMs);
        if (e.kind != ControlEvent::Kind::None) result = e;
    }
    return result;
}

ControlEvent Controls::apply(Button b, ButtonTracker::Press p, uint32_t nowMs) {
    const bool isLong = p == ButtonTracker::Press::Long;
    ControlEvent e{ControlEvent::Kind::Changed, Setting::None};
    switch (b) {
        case Button::A:
            if (isLong) {
                settings_.clearSky = next(settings_.clearSky, count(kClearSkySteps));
                e.changed = Setting::ClearSky;
            } else {
                settings_.palette = static_cast<uint8_t>((settings_.palette + 1) % paletteCount());
                e.changed = Setting::Palette;
            }
            break;
        case Button::B:
            if (isLong) {
                settings_.girth = next(settings_.girth, count(kGirthSteps));
                e.changed = Setting::Girth;
            } else {
                settings_.brightness = next(settings_.brightness, count(kBrightnessSteps));
                e.changed = Setting::Brightness;
            }
            break;
        case Button::C:
            return {isLong ? ControlEvent::Kind::Relevel : ControlEvent::Kind::WakeDisplay, Setting::None};
    }
    dirty_ = true;
    lastChangeMs_ = nowMs;
    return e;
}

}  // namespace kd
