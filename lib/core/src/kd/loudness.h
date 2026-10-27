// Loudness of the sound around the Dong, from blocks of microphone samples.
//
// Reads 0 in the room's usual quiet and 1 at its recent loudest, so the same
// Dong works in a quiet room and next to a sound system. Both ends adapt:
//   - the quiet level drops at once to the quietest moment and creeps up
//     while the room stays loud, so a steady hum (a fan, a crowd) fades out;
//   - the loudest level jumps to any louder sound and sinks back, but stays at
//     least minRange above the quiet level, so small noises stay small.
// Loudness counts from a few dB over the quiet level, so the flutter of a
// quiet room reads 0.
#pragma once

#include <cstddef>
#include <cstdint>

namespace kd {

class LoudnessMeter {
public:
    struct Settings {
        float highPassHz = 80.f;  // below this is wind, handling noise and the mic's DC offset
        float attack = 0.01f;     // s for the envelope to rise to a louder sound
        float release = 0.25f;    // s for it to fall back
        float floorRise = 1.5f;   // dB/s the quiet level creeps up while the room stays loud
        float peakFall = 3.f;     // dB/s the loudest level sinks
        float minRange = 20.f;    // dB from the quiet level to Loudness 1, at least
        float margin = 3.f;       // dB over the quiet level that still reads 0
        float silence = -90.f;    // dBFS; anything quieter counts as this
        float window = 0.01f;     // s of sound in each measurement
    };

    explicit LoudnessMeter(float sampleRate) : LoudnessMeter(sampleRate, Settings{}) {}
    LoudnessMeter(float sampleRate, const Settings& settings);

    // Signed 16-bit samples, oldest first, in blocks of any size. The sound is
    // measured over fixed windows, so the block size doesn't change the reading.
    void update(const int16_t* samples, size_t count);

    float level() const;                    // Loudness, 0..1
    float decibels() const { return env_; }  // smoothed sound level, dBFS
    float quietDecibels() const { return floor_; }
    float loudDecibels() const { return peak_; }

private:
    void measure(float meanSquare);

    Settings settings_;
    float highPass_;  // coefficient of each of the two one-pole high-pass stages
    size_t windowSize_;
    float windowSeconds_;
    float lastIn_[2] = {}, lastOut_[2] = {};
    bool filterPrimed_ = false;
    float sumSquares_ = 0.f;
    size_t windowCount_ = 0;
    bool heard_ = false;  // a window louder than silence has come in
    float env_, floor_, peak_;
};

}  // namespace kd
