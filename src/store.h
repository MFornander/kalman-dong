// Settings and gyro bias kept in flash across power cycles.
#pragma once

#include "kd/controls.h"
#include "kd/kd_math.h"

namespace store {

void begin();
kd::Settings loadSettings();
void saveSettings(const kd::Settings& s);
// Gyro bias in the sensor frame, so it survives a change of Mounting.
kd::Vec3 loadGyroBias();
void saveGyroBias(const kd::Vec3& bias);

// Mounting (sensor -> body). Returns false if none has been calibrated.
bool loadMounting(kd::Quat& sensorToBody);
void saveMounting(const kd::Quat& sensorToBody);

}  // namespace store
