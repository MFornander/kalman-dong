#include "stillness.h"

namespace kd {

namespace {
// Compare a lightly smoothed reading against a slower mean. Comparing raw
// samples would let sensor noise (~0.03 rad/s on a real LSM6DS3TR-C) break
// stillness on almost every sample.
constexpr float kFastTimeConstant = 0.05f;  // s
constexpr float kMeanTimeConstant = 0.3f;   // s
}  // namespace

void Stillness::reset() {
    primed_ = false;
    stillTime_ = 0;
    gyroSum_ = accelSum_ = {};
    stillWeight_ = 0;
}

void Stillness::update(const Vec3& gyro, const Vec3& accel, float dt) {
    if (!primed_) {
        gyroMean_ = gyroFast_ = gyro;
        accelMean_ = accelFast_ = accel;
        primed_ = true;
    }
    float fast = dt / (kFastTimeConstant + dt);
    float slow = dt / (kMeanTimeConstant + dt);
    gyroFast_ += (gyro - gyroFast_) * fast;
    accelFast_ += (accel - accelFast_) * fast;
    gyroMean_ += (gyro - gyroMean_) * slow;
    accelMean_ += (accel - accelMean_) * slow;

    bool still = (gyroFast_ - gyroMean_).norm() < limits_.gyro && (accelFast_ - accelMean_).norm() < limits_.accel;
    if (still) {
        stillTime_ += dt;
        gyroSum_ += gyro * dt;
        accelSum_ += accel * dt;
        stillWeight_ += dt;
    } else {
        stillTime_ = 0;
        gyroSum_ = accelSum_ = {};
        stillWeight_ = 0;
    }
}

}  // namespace kd
