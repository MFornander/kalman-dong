#include "controls.h"

#include "color.h"

namespace kd {

namespace {

const float kBrightnessSteps[] = {0.25f, 0.5f, 0.75f, 1.0f};
const float kGirthSteps[] = {0.f, 0.1f, 0.2f, 0.4f};
const float kClearSkySteps[] = {0.f, 0.15f, 0.3f, 0.5f};
const float kMotionSteps[] = {0.f, 1.f, 2.f};
const float kCloudSizeSteps[] = {0.3f, 0.6f, 1.2f, 2.4f};

template <typename T, unsigned N>
constexpr unsigned count(const T (&)[N]) { return N; }

uint8_t next(uint8_t v, unsigned n) { return static_cast<uint8_t>((v + 1) % n); }

}  // namespace

float Settings::brightnessValue() const { return kBrightnessSteps[brightness % count(kBrightnessSteps)]; }
float Settings::girthValue() const { return kGirthSteps[girth % count(kGirthSteps)]; }
float Settings::clearSkyValue() const { return kClearSkySteps[clearSky % count(kClearSkySteps)]; }
float Settings::motionValue() const { return kMotionSteps[motion % count(kMotionSteps)]; }
float Settings::cloudSizeValue() const { return kCloudSizeSteps[cloudSize % count(kCloudSizeSteps)]; }

void Settings::sanitize() {
    palette %= paletteCount();
    brightness %= count(kBrightnessSteps);
    girth %= count(kGirthSteps);
    clearSky %= count(kClearSkySteps);
    motion %= count(kMotionSteps);
    cloudSize %= count(kCloudSizeSteps);
}

ButtonTracker::Press ButtonTracker::update(bool down, uint32_t nowMs) {
    // A level that has lasted kDebounceMs is real, and dates from when it
    // began, so a press is timed from its edges however late or sparsely it
    // is looked at.
    Press out = Press::None;
    if (raw_ != stable_ && nowMs - rawSinceMs_ >= kDebounceMs) {
        stable_ = raw_;
        if (stable_) {
            pressedAtMs_ = rawSinceMs_;
            held_ = false;
        } else if (!held_) {
            out = rawSinceMs_ - pressedAtMs_ >= kLongMs ? Press::Long : Press::Quick;
        }
    }
    if (down != raw_) {
        raw_ = down;
        rawSinceMs_ = nowMs;
    }
    // Held once the press has lasted kHeldMs, counting only the time it is
    // known to have been down (not past a release still being debounced).
    const uint32_t downUntilMs = raw_ ? nowMs : rawSinceMs_;
    if (stable_ && !held_ && downUntilMs - pressedAtMs_ >= kHeldMs) {
        held_ = true;
        out = Press::Held;
    }
    return out;
}

ControlEvents Controls::update(bool aDown, bool bDown, bool cDown, uint32_t nowMs) {
    // Samples can arrive a millisecond out of order (a sampler racing the
    // main loop); time never runs backwards here.
    if (started_ && static_cast<int32_t>(nowMs - lastMs_) < 0) nowMs = lastMs_;
    started_ = true;
    lastMs_ = nowMs;
    const bool down[3] = {aDown, bDown, cDown};
    ControlEvents result;
    for (int i = 0; i < 3; ++i) {
        const ButtonTracker::Press p = buttons_[i].update(down[i], nowMs);
        if (p == ButtonTracker::Press::None) continue;
        const ControlEvent e = apply(static_cast<Button>(i), p, nowMs);
        if (e.kind != ControlEvent::Kind::None) result.events[result.count++] = e;
    }
    return result;
}

Controls::PressInProgress Controls::pressInProgress(uint32_t nowMs) const {
    PressInProgress p;
    for (int i = 0; i < 3; ++i) {
        const uint32_t held = buttons_[i].heldMs(nowMs);
        if (held < kSegmentMs) continue;
        p.button = i;
        p.segments = held >= ButtonTracker::kHeldMs ? kSegments : static_cast<int>(held / kSegmentMs);
        p.onRelease = held >= ButtonTracker::kHeldMs ? ButtonTracker::Press::None
                      : held >= ButtonTracker::kLongMs ? ButtonTracker::Press::Long
                                                       : ButtonTracker::Press::Quick;
        break;
    }
    return p;
}

ControlEvent Controls::apply(Button b, ButtonTracker::Press p, uint32_t nowMs) {
    if (p == ButtonTracker::Press::Held) {
        if (b == Button::C) return {ControlEvent::Kind::CalibrateMounting, Setting::None};
    }
    const bool isLong = p == ButtonTracker::Press::Long;
    ControlEvent e{ControlEvent::Kind::Changed, Setting::None};
    switch (b) {
        case Button::A:
            if (p == ButtonTracker::Press::Held) {
                settings_.clearSky = next(settings_.clearSky, count(kClearSkySteps));
                e.changed = Setting::ClearSky;
            } else if (isLong) {
                settings_.cloudSize = next(settings_.cloudSize, count(kCloudSizeSteps));
                e.changed = Setting::CloudSize;
            } else {
                settings_.palette = static_cast<uint8_t>((settings_.palette + 1) % paletteCount());
                e.changed = Setting::Palette;
            }
            break;
        case Button::B:
            if (p == ButtonTracker::Press::Held) {
                settings_.girth = next(settings_.girth, count(kGirthSteps));
                e.changed = Setting::Girth;
            } else if (isLong) {
                settings_.brightness = next(settings_.brightness, count(kBrightnessSteps));
                e.changed = Setting::Brightness;
            } else {
                settings_.motion = next(settings_.motion, count(kMotionSteps));
                e.changed = Setting::Motion;
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
