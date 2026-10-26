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
    float fps = 0.f;
    float imuHz = 0.f;
    bool atRest = false;
    bool recording = false;
    // The Blade view's header, from kd::readout() (kd/pose_readout.h).
    int pos[3] = {};
    int rot[3] = {};
    char banner[40] = "";  // e.g. Mounting Calibration instructions; shown prominently
    int holdSegments = -1;  // while a button is held: half-second segments filled
    bool asleep = false;    // idle for kSleepAfterMs: the Blade view is off
};

namespace status_display {

constexpr uint32_t kAwakeMs = 30000;
// The screen turns off once the Dong has been lying still, with no button
// pressed, for this long; moving it or pressing a button brings it back.
constexpr uint32_t kSleepAfterMs = 60000;

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

// Opens the Status page for kAwakeMs, or closes it if it is open.
void toggle(const Status& status, uint32_t nowMs);

// Call regularly from the display task. `lines` holds lineCount *
// pixelsPerLine Pixels, Line after Line, each from the Hilt outward.
void update(const Status& status, const kd::Rgb8* lines, size_t lineCount, size_t pixelsPerLine, uint32_t nowMs);

}  // namespace status_display
