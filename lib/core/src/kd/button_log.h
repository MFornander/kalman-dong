// Button levels sampled off the main loop, each change queued with the time
// it was seen. The main loop can stall (a slow display draw, a blocked log
// write) and still time every press from its real edges.
//
// One writer (the sampler, on a timer) and one reader (the main loop).
#pragma once

#include <atomic>
#include <cstdint>

namespace kd {

class ButtonLog {
public:
    struct Sample {
        uint8_t down = 0;  // bit i set: button i (A, B, C) is down
        uint32_t ms = 0;
    };
    static constexpr uint32_t kCapacity = 128;  // a power of two

    // Sampler side: call at a steady rate. Queues the levels when they differ
    // from the last queued ones; when the queue is full it tries again on the
    // next call, so the latest level always gets through.
    void sample(uint8_t down, uint32_t ms) {
        if (down == queued_) return;
        const uint32_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= kCapacity) return;
        ring_[head % kCapacity] = {down, ms};
        head_.store(head + 1, std::memory_order_release);
        queued_ = down;
    }

    // Main loop side: takes the oldest queued change, if there is one.
    bool pop(Sample& out) {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = ring_[tail % kCapacity];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    Sample ring_[kCapacity];
    std::atomic<uint32_t> head_{0}, tail_{0};
    uint8_t queued_ = 0;  // sampler only
};

}  // namespace kd
