#include "loudness.h"

#include <cmath>

#include "kd_math.h"

namespace kd {

LoudnessMeter::LoudnessMeter(float sampleRate, const Settings& settings)
    : settings_(settings),
      highPass_(1.f / (1.f + 2.f * kPi * settings.highPassHz / sampleRate)),
      windowSize_(static_cast<size_t>(std::lround(std::fmax(sampleRate * settings.window, 1.f)))),
      windowSeconds_(windowSize_ / sampleRate),
      env_(settings.silence),
      floor_(settings.silence),
      peak_(settings.silence + settings.minRange) {}

void LoudnessMeter::update(const int16_t* samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        float v = samples[i];
        if (!filterPrimed_) lastIn_[0] = v;  // start the filter settled on the mic's offset
        filterPrimed_ = true;
        for (int stage = 0; stage < 2; ++stage) {
            lastOut_[stage] = highPass_ * (lastOut_[stage] + v - lastIn_[stage]);
            lastIn_[stage] = v;
            v = lastOut_[stage];
        }
        sumSquares_ += v * v;
        if (++windowCount_ == windowSize_) {
            measure(sumSquares_ / windowSize_);
            sumSquares_ = 0.f;
            windowCount_ = 0;
        }
    }
}

void LoudnessMeter::measure(float meanSquare) {
    const float rms = std::sqrt(meanSquare) / 32768.f;
    const float db = rms > 0.f ? std::fmax(20.f * std::log10(rms), settings_.silence) : settings_.silence;

    // A mic that is still starting up sends nothing; start from the first
    // real sound so the quiet level doesn't have to climb up from silence.
    if (!heard_) {
        if (db <= settings_.silence) return;
        heard_ = true;
        env_ = floor_ = db;
        peak_ = db + settings_.minRange;
        return;
    }

    const float dt = windowSeconds_;
    const float tau = db > env_ ? settings_.attack : settings_.release;
    env_ += (db - env_) * (1.f - std::exp(-dt / tau));

    floor_ = env_ < floor_ ? env_ : std::fmin(floor_ + settings_.floorRise * dt, env_);
    peak_ = env_ > peak_ ? env_ : peak_ - settings_.peakFall * dt;
    peak_ = std::fmax(peak_, floor_ + settings_.minRange);
}

float LoudnessMeter::level() const {
    const float quiet = floor_ + settings_.margin;
    return clampf((env_ - quiet) / (peak_ - quiet), 0.f, 1.f);
}

}  // namespace kd
