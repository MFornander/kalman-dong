// Dong Profile: the description of one physical build of a Dong. Concrete
// profiles live in src/profiles.h and are chosen when the firmware is built.
#pragma once

#include <cstddef>
#include <cstdint>

namespace kd {

constexpr size_t kMaxLines = 4;
constexpr size_t kMaxStrands = 2;
constexpr size_t kMaxSegmentsPerStrand = 4;

// One run of a Strand that lights one whole Line.
struct StrandSegment {
    uint8_t line = 0;        // which Line this run lights
    bool fromTip = false;    // true if the run starts at the tip and runs toward the Hilt
    uint16_t skipBefore = 0; // unlit Pixels on the Strand before this run (e.g. a fold at the tip)
};

struct StrandLayout {
    uint8_t segmentCount = 0;
    StrandSegment segments[kMaxSegmentsPerStrand];
};

// Body frame: +X runs along the Blade from the Hilt to the tip. Line k faces
// outward at angle k * 360/lineCount degrees around X, starting at +Y.
struct BodyAxisMap {
    // body axis i = sign[i] * sensor axis source[i]
    uint8_t source[3] = {0, 1, 2};
    int8_t sign[3] = {1, 1, 1};
};

struct PowerProfile {
    float supplyVolts = 5.f;
    float budgetMilliamps = 3000.f;     // Power Budget
    float milliampsPerChannel = 20.f;   // one color channel of one Pixel at full
    float idleMilliampsPerPixel = 1.f;  // a dark Pixel still draws this
};

struct BatteryProfile {
    bool sensed = false;     // false: no pack voltage measurement wired
    float dividerRatio = 2.f;  // pack volts = pin volts * dividerRatio
    float emptyVolts = 3.3f;
    float fullVolts = 4.15f;
    float lowFraction = 0.20f;       // below: Power Budget steps down
    float criticalFraction = 0.05f;  // below: red warning pulse
};

struct DongProfile {
    const char* name = "unnamed";

    // Blade
    float bladeLength = 1.0f;     // m, first Pixel to last Pixel
    float hiltToFirstPixel = 0.15f;  // m, from the IMU to the first Pixel
    uint8_t lineCount = 4;
    uint16_t pixelsPerLine = 60;

    // Strands
    uint8_t strandCount = 1;
    StrandLayout strands[kMaxStrands];

    BodyAxisMap imuAxes;
    PowerProfile power;
    BatteryProfile battery;

    // Pixels needed on Strand s, including skipped ones.
    size_t strandLength(size_t s) const {
        size_t n = 0;
        for (size_t i = 0; i < strands[s].segmentCount; ++i)
            n += strands[s].segments[i].skipBefore + pixelsPerLine;
        return n;
    }
};

}  // namespace kd
