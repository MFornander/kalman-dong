#include "sk9822.h"

#include <cstring>

namespace kd {

namespace {
constexpr size_t kStartBytes = 4;
constexpr size_t kResetBytes = 4;  // SK9822 latches on a 32-bit zero frame
constexpr uint8_t kFullGlobalBrightness = 0xE0 | 31;
}  // namespace

bool WireOrder::parse(const char* text, WireOrder& out) {
    if (!text) return false;
    bool seen[3] = {};
    WireOrder order;
    for (int i = 0; i < 3; ++i) {
        const char c = text[i];
        int channel = c == 'R' ? 0 : (c == 'G' ? 1 : (c == 'B' ? 2 : -1));
        if (channel < 0 || seen[channel]) return false;
        seen[channel] = true;
        order.channel[i] = static_cast<uint8_t>(channel);
    }
    if (text[3] != '\0') return false;
    out = order;
    return true;
}

size_t sk9822FrameBytes(size_t pixels) {
    // Each Pixel delays the clock by half a cycle, so pushing data through n
    // Pixels needs n/2 extra clock edges after the last one.
    return kStartBytes + 4 * pixels + (pixels + 15) / 16 + kResetBytes;
}

void sk9822Encode(const Rgb8* px, size_t n, const WireOrder& order, uint8_t* out) {
    std::memset(out, 0, kStartBytes);
    uint8_t* p = out + kStartBytes;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t rgb[3] = {px[i].r, px[i].g, px[i].b};
        *p++ = kFullGlobalBrightness;
        *p++ = rgb[order.channel[0]];
        *p++ = rgb[order.channel[1]];
        *p++ = rgb[order.channel[2]];
    }
    std::memset(p, 0, sk9822FrameBytes(n) - kStartBytes - 4 * n);
}

}  // namespace kd
