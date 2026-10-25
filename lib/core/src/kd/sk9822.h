// SK9822 / APA102 framing: the bytes a Strand expects for one frame. Kept in
// the core so it is tested on the host; the SPI driver only moves the bytes.
#pragma once

#include <cstddef>
#include <cstdint>

#include "color.h"

namespace kd {

// Color byte order on the wire, e.g. "BGR" (most SK9822/APA102 strips).
struct WireOrder {
    uint8_t channel[3] = {2, 1, 0};  // per wire byte: 0 = red, 1 = green, 2 = blue

    // Accepts any arrangement of the letters R, G and B.
    static bool parse(const char* text, WireOrder& out);
};

// Bytes in one frame for `pixels` Pixels.
size_t sk9822FrameBytes(size_t pixels);

// Writes a complete frame (sk9822FrameBytes(n) bytes) into `out`: a zero
// start frame, then per Pixel full global brightness and the three colors in
// wire order, then zero bytes to clock the data through every Pixel.
void sk9822Encode(const Rgb8* px, size_t n, const WireOrder& order, uint8_t* out);

}  // namespace kd
