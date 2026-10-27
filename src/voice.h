// Spoken commands with Espressif's MultiNet recognizer (ESP-SR), run only
// while the talk button is held (kd/push_to_talk.h).
//
// The mic task hands every block to feed(); while listening, the audio goes
// through a stream buffer to the voice task, which runs on core 0 (the IMU
// core) below the IMU and mic tasks. When it falls behind, the Status display
// on that core slows down; the LED core never notices.
//
// The model (3.3 MB) lives in its own "model" partition, written once with
// `pio run -e xiao_screen -t upload_voice_model` (README, Voice).
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/stream_buffer.h>

#include <atomic>

#include "kd/voice_commands.h"

class Voice {
public:
    // Loads the model and starts the voice task. False (with the reason in
    // error()) if the model partition is missing or empty.
    bool begin(UBaseType_t priority, BaseType_t core);
    const char* error() const { return error_; }
    const char* modelName() const { return modelName_; }

    // Mic task: every block, listening or not (cheap when not).
    void feed(const int16_t* samples, size_t count);

    // Main loop: the talk button went down / up.
    void listen();
    void finish();

    struct Result {
        enum class Kind : uint8_t { Heard, Finished } kind = Kind::Finished;
        kd::VoiceCommand command = kd::VoiceCommand::Count;
        const char* words = "";  // the phrase heard
        float probability = 0.f;
        uint32_t afterMs = 0;    // since listen(); for Finished: how long finishing took after finish()
        float peakDb = -90.f;    // Finished: loudest sample while listening (dB below full scale)
    };
    // Main loop: the next result, if any. Only results for the latest listen().
    bool poll(Result& out);

    // Applied before the recognizer: the PDM mic is quiet, and MultiNet
    // wants speech well above -40 dBFS. Serial 'g' steps it.
    void setGain(float g) { gain_ = g; }
    float gain() const { return gain_; }

    // For serial 'i'.
    struct Stats {
        float detectUs = 0.f;      // smoothed time per chunk
        uint32_t worstDetectUs = 0;
        uint32_t chunkSamples = 0;  // per detect call
        uint32_t worstBacklogMs = 0;  // most audio waiting while listening
        uint32_t dropped = 0;        // blocks lost to a full buffer
        float gain = 0.f;
    };
    Stats stats() const;

private:
    static void taskEntry(void* self) { static_cast<Voice*>(self)->run(); }
    void run();
    void detect(int16_t* chunk);
    void post(const Result& r);

    const char* error_ = "not started";
    const char* modelName_ = "";
    StreamBufferHandle_t stream_ = nullptr;
    QueueHandle_t results_ = nullptr;
    TaskHandle_t task_ = nullptr;

    std::atomic<uint32_t> generation_{0};  // bumped by listen()
    std::atomic<bool> accepting_{false};   // feed() passes audio on
    std::atomic<bool> finishing_{false};
    uint32_t taskGeneration_ = 0;
    uint32_t listenMs_ = 0, finishMs_ = 0;

    // Mic-side filter: DC blocker, then gain.
    float dcIn_ = 0.f, dcOut_ = 0.f;
    std::atomic<int32_t> peak_{0};
    float gain_ = 4.f;

    Stats stats_;
};
