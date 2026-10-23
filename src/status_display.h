// The Hilt's 1.14" display: a status screen that wakes on button presses and
// turns itself off so it doesn't drain the battery or glow through the fur.
#pragma once

#include <cstdint>

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
};

namespace status_display {

constexpr uint32_t kAwakeMs = 5000;

bool begin();

// Shows `status` and keeps the display awake for kAwakeMs.
void wake(const Status& status, uint32_t nowMs);

// Refreshes while awake (call a few times a second) and sleeps when due.
void update(const Status& status, uint32_t nowMs);

}  // namespace status_display
