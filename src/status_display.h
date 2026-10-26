// The Hilt's 1.14" display. Two pages:
//  - Status: settings and rates; wakes on button presses, then turns itself
//    off so it doesn't drain the battery or glow through the fur.
//  - Blade view: the Lines drawn as the Pixels would light them (Hilt left,
//    tip right), for bench testing without an LED build. Stays on.
#pragma once

#include <cstddef>
#include <cstdint>

#include "kd/color.h"
#include "kd/controls.h"

struct Status {
    kd::Settings settings;
    kd::Setting changed = kd::Setting::None;  // highlighted after a button press
    const char* profile = "";
    const char* imuChip = "";
    float batteryFraction = -1.f;
    float fps = 0.f;
    float imuHz = 0.f;
    bool atRest = false;
    bool recording = false;
    int bladePitch = 0;      // degrees above level, rounded to 5
    int bladeHeading = 0;    // degrees side to side from the Re-level heading, clockwise positive, rounded to 5
    bool headingKnown = false;  // false while the Blade points nearly straight up or down
    char banner[40] = "";  // e.g. Mounting Calibration instructions; shown prominently
    int holdSegments = -1;  // while a button is held: half-second segments filled
};

namespace status_display {

constexpr uint32_t kAwakeMs = 5000;

bool begin();

void setBladeView(bool on);
bool bladeView();

// The page on show: "off", "status" or "blade".
const char* pageName();

// Microseconds the last full draw of the Status page took.
uint32_t statusDrawUs();

// Microseconds the last Blade view spent drawing its header text and bars.
void drawTimes(uint32_t& textUs, uint32_t& barsUs);

// Shows the Status page for kAwakeMs.
void wake(const Status& status, uint32_t nowMs);

// Call regularly from the display task. `lines` holds lineCount *
// pixelsPerLine Pixels, Line after Line, each from the Hilt outward.
void update(const Status& status, const kd::Rgb8* lines, size_t lineCount, size_t pixelsPerLine, uint32_t nowMs);

}  // namespace status_display
