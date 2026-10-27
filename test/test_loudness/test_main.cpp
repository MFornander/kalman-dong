#include "../doctest_main.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "kd/loudness.h"

using namespace kd;

namespace {

constexpr float kRate = 16000.f;
constexpr size_t kBlock = 160;  // 10 ms, as on the board

// A room for the mic to hear: hiss at one level, an optional tone, a DC offset.
struct Room {
    float hissDb = -60.f;  // dBFS
    float toneDb = -200.f;
    float toneHz = 440.f;
    float offset = 0.f;  // raw counts
    uint32_t seed = 1;
    size_t n = 0;

    static float amplitude(float rmsDb) { return 32768.f * std::pow(10.f, rmsDb / 20.f); }

    float noise() {  // uniform in [-1, 1), rms 1/sqrt(3)
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 23) - 1.f;
    }

    void play(LoudnessMeter& meter, float seconds) {
        std::vector<int16_t> block(kBlock);
        for (int blocks = int(seconds * kRate / kBlock + 0.5f); blocks > 0; --blocks) {
            for (int16_t& s : block) {
                const float t = n++ / kRate;
                float v = offset + noise() * amplitude(hissDb) * std::sqrt(3.f) +
                          std::sin(2.f * 3.14159265f * toneHz * t) * amplitude(toneDb) * std::sqrt(2.f);
                v = std::fmax(-32768.f, std::fmin(32767.f, v));
                s = static_cast<int16_t>(std::lround(v));
            }
            meter.update(block.data(), block.size());
        }
    }
};

}  // namespace

TEST_CASE("silence reads 0") {
    LoudnessMeter meter(kRate);
    std::vector<int16_t> zeros(kBlock, 0);
    for (int i = 0; i < 100; ++i) meter.update(zeros.data(), zeros.size());
    CHECK(meter.level() == 0.f);
    CHECK(std::isfinite(meter.decibels()));
}

TEST_CASE("a quiet room reads 0, however its hiss flutters") {
    LoudnessMeter meter(kRate);
    Room room;
    room.play(meter, 2.f);
    for (int i = 0; i < 100; ++i) {
        room.hissDb = -60.f + float(i % 5) * 0.5f;  // a couple of dB of flutter
        room.play(meter, 0.03f);
        CHECK(meter.level() == 0.f);
    }
    CHECK(meter.decibels() == doctest::Approx(-60.f).epsilon(0.05));
}

TEST_CASE("a loud sound in a quiet room reads 1 within a few ms") {
    LoudnessMeter meter(kRate);
    Room room;
    room.play(meter, 2.f);
    room.toneDb = -25.f;
    room.play(meter, 0.05f);
    CHECK(meter.level() > 0.9f);
}

TEST_CASE("after the sound stops the level falls back within a second") {
    LoudnessMeter meter(kRate);
    Room room;
    room.play(meter, 2.f);
    room.toneDb = -25.f;
    room.play(meter, 0.3f);
    room.toneDb = -200.f;
    room.play(meter, 0.25f);
    CHECK(meter.level() < 0.6f);  // fading, not cut
    CHECK(meter.level() > 0.1f);
    room.play(meter, 0.75f);
    CHECK(meter.level() < 0.1f);
}

TEST_CASE("talking over a quiet room reads partway, not full") {
    // minRange keeps a modest sound modest when nothing louder has been heard.
    LoudnessMeter meter(kRate);
    Room room;
    room.play(meter, 2.f);
    room.toneDb = -48.f;  // about 12 dB over the hiss
    room.play(meter, 0.2f);
    CHECK(meter.level() > 0.4f);
    CHECK(meter.level() < 0.8f);
}

TEST_CASE("a steady hum fades out, and sounds over it still show") {
    LoudnessMeter meter(kRate);
    Room room;
    room.play(meter, 1.f);
    room.toneDb = -30.f;  // a fan starts
    room.play(meter, 0.2f);
    CHECK(meter.level() > 0.9f);
    room.play(meter, 30.f);
    CHECK(meter.level() < 0.15f);
    CHECK(meter.quietDecibels() == doctest::Approx(-30.f).epsilon(0.05));

    room.hissDb = -8.f;  // a shout over the fan
    room.play(meter, 0.05f);
    CHECK(meter.level() > 0.9f);
}

TEST_CASE("the loudest level sinks back so later sounds aren't dwarfed") {
    LoudnessMeter meter(kRate);
    Room room;
    room.play(meter, 2.f);
    room.toneDb = -5.f;  // a clap right at the mic
    room.play(meter, 0.05f);
    room.toneDb = -200.f;
    room.play(meter, 1.f);
    room.toneDb = -40.f;  // 20 dB over the hiss
    room.play(meter, 0.2f);
    const float soonAfter = meter.level();
    room.toneDb = -200.f;
    room.play(meter, 15.f);
    room.toneDb = -40.f;
    room.play(meter, 0.2f);
    CHECK(soonAfter < 0.6f);
    CHECK(meter.level() > 0.85f);
}

TEST_CASE("the mic's DC offset and slow drift don't count as sound") {
    LoudnessMeter plain(kRate), offset(kRate);
    Room a, b;
    b.offset = 4000.f;
    a.play(plain, 1.f);
    b.play(offset, 1.f);
    CHECK(offset.decibels() == doctest::Approx(plain.decibels()).epsilon(0.02));
    CHECK(offset.level() < 0.15f);

    // A 20 Hz rumble (handling, wind on the fur) is mostly filtered out.
    LoudnessMeter rumble(kRate);
    Room c;
    c.play(rumble, 2.f);
    c.toneHz = 20.f;
    c.toneDb = -45.f;
    c.play(rumble, 0.3f);
    CHECK(rumble.decibels() < -55.f);
}

TEST_CASE("block size doesn't change the reading") {
    LoudnessMeter small(kRate), large(kRate);
    Room room;
    std::vector<int16_t> samples(int(kRate * 1.5f));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / kRate;
        const float db = t < 1.f ? -60.f : -30.f;
        samples[i] = static_cast<int16_t>(std::lround(room.noise() * Room::amplitude(db) * std::sqrt(3.f)));
    }
    for (size_t i = 0; i < samples.size(); i += 32) small.update(&samples[i], 32);
    for (size_t i = 0; i < samples.size(); i += 320) large.update(&samples[i], 320);
    CHECK(small.decibels() == doctest::Approx(large.decibels()).epsilon(0.02));
    CHECK(small.level() == doctest::Approx(large.level()).epsilon(0.05));
}
