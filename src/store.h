// Settings and gyro bias kept in flash across power cycles.
#pragma once

#include "kd/controls.h"
#include "kd/kd_math.h"

namespace store {

void begin();
kd::Settings loadSettings();
void saveSettings(const kd::Settings& s);
kd::Vec3 loadGyroBias();
void saveGyroBias(const kd::Vec3& bias);

}  // namespace store
