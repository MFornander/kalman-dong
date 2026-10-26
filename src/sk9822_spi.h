// SK9822 / APA102 Pixel output on the ESP32-S3's SPI2 peripheral, sent by
// DMA in the background so the next frame renders while this one goes out.
//
// Several Strands share the one SPI host: just before each Strand's transfer
// the host's data and clock signals are re-routed to that Strand's pins, and
// the other Strands' pins are parked low (their Pixels ignore a still clock).
// The display uses SPI3; see docs/adr/0005-own-led-driver.md.
#pragma once

#include <cstddef>
#include <cstdint>

#include "kd/color.h"
#include "kd/dong_profile.h"
#include "kd/sk9822.h"

class Sk9822Spi {
public:
    struct Strand {
        int dataPin;
        int clockPin;
        size_t length;  // Pixels
    };

    // `wireOrder` names the color byte order on the wire, e.g. "BGR".
    // Framing lives in kd/sk9822.h; this class only moves the bytes.
    bool begin(const Strand* strands, size_t count, uint32_t clockHz, const char* wireOrder);

    // Encodes `pixels[s]` for every Strand, waits for the previous frame to
    // finish sending, then queues this one and returns.
    void show(const kd::Rgb8* const* pixels);

    // Microseconds the last show() spent waiting for the previous frame.
    uint32_t lastWaitUs() const { return lastWaitUs_; }

    // Microseconds to clock one frame out to every Strand.
    uint32_t sendUs() const { return sendUs_; }

private:
    struct Buffer {
        uint8_t* bytes = nullptr;
        size_t size = 0;
    };

    size_t count_ = 0;
    Strand strands_[kd::kMaxStrands] = {};
    Buffer buffers_[2][kd::kMaxStrands];
    int back_ = 0;
    size_t inFlight_ = 0;
    kd::WireOrder order_;
    uint32_t lastWaitUs_ = 0;
    uint32_t sendUs_ = 0;
};
