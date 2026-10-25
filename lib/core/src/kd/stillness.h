// Detects the Dong being held or laid still, and averages the readings over
// the still stretch. Shared by MotionTracker (Ignition, Rest) and Mounting
// Calibration.
#pragma once

#include "kd_math.h"

namespace kd {

class Stillness {
public:
    struct Limits {
        float gyro = 0.03f;  // rad/s; allowed wobble of the smoothed gyro around its recent mean
        float accel = 0.2f;  // m/s^2; same for the accelerometer
    };

    Stillness() = default;
    explicit Stillness(const Limits& limits) : limits_(limits) {}

    void reset();

    // Readings in any consistent frame.
    void update(const Vec3& gyro, const Vec3& accel, float dt);

    // Seconds the readings have stayed still; 0 while moving.
    float stillTime() const { return stillTime_; }

    // Averages over the current still stretch (zero when moving).
    Vec3 stillGyro() const { return stillWeight_ > 0.f ? gyroSum_ * (1.f / stillWeight_) : Vec3{}; }
    Vec3 stillAccel() const { return stillWeight_ > 0.f ? accelSum_ * (1.f / stillWeight_) : Vec3{}; }

    // Recent (~0.3 s) means, moving or not.
    Vec3 recentGyro() const { return gyroMean_; }
    Vec3 recentAccel() const { return accelMean_; }

private:
    Limits limits_;
    bool primed_ = false;
    Vec3 gyroFast_, accelFast_;
    Vec3 gyroMean_, accelMean_;
    float stillTime_ = 0;
    Vec3 gyroSum_, accelSum_;
    float stillWeight_ = 0;
};

}  // namespace kd
