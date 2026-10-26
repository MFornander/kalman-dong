// Synthetic IMU for host tests: integrates a true orientation and produces
// the gyro and accelerometer readings a perfect (or biased, noisy) sensor
// would report.
#pragma once

#include <cstdint>
#include <functional>

#include "kd/kd_math.h"
#include "kd/motion_tracker.h"

namespace sim {

using kd::Quat;
using kd::Vec3;

class Imu {
public:
    float rate = 833.f;  // Hz
    Vec3 gyroBias;
    float gyroNoise = 0.f;   // rad/s, uniform amplitude
    float accelNoise = 0.f;  // m/s^2, uniform amplitude

    Quat truth;          // body -> world
    double time = 0.0;   // s

    // Body-frame angular velocity (rad/s) and world-frame linear acceleration
    // (m/s^2) as functions of time.
    std::function<Vec3(double)> omega = [](double) { return Vec3{}; };
    std::function<Vec3(double)> linear = [](double) { return Vec3{}; };

    kd::ImuSample step() {
        float dt = 1.f / rate;
        // Integrate truth in substeps for accuracy.
        constexpr int kSub = 8;
        for (int i = 0; i < kSub; ++i) {
            Vec3 w = omega(time + (i + 0.5) * dt / kSub);
            truth = (truth * Quat::fromRotationVector(w * (dt / kSub))).normalized();
        }
        time += dt;
        kd::ImuSample s;
        s.dt = dt;
        s.gyro = omega(time) + gyroBias + noise3(gyroNoise);
        Vec3 specificWorld = linear(time) + Vec3{0, 0, kd::kGravity};
        s.accel = truth.conj().rotate(specificWorld) + noise3(accelNoise);
        return s;
    }

    // Runs `seconds` of samples into `tracker`.
    void run(kd::MotionTracker& tracker, double seconds) {
        int n = static_cast<int>(seconds * rate + 0.5);
        for (int i = 0; i < n; ++i) tracker.update(step());
    }

private:
    uint32_t seed_ = 12345;
    float uniform() {  // -1..1
        seed_ = seed_ * 1664525u + 1013904223u;
        return (seed_ >> 8) / float(1 << 24) * 2.f - 1.f;
    }
    Vec3 noise3(float amp) {
        if (amp <= 0.f) return {};
        return {uniform() * amp, uniform() * amp, uniform() * amp};
    }
};

// Angle (rad) between two directions.
inline float angleBetween(const Vec3& a, const Vec3& b) {
    return std::acos(kd::clampf(a.normalized().dot(b.normalized()), -1.f, 1.f));
}

// Heading (rad) of the Blade axis (+X) projected onto the horizontal.
inline float bladeHeading(const Quat& q) {
    Vec3 f = q.rotate(Vec3{1, 0, 0});
    return std::atan2(f.y, f.x);
}

inline float wrapAngle(float a) {
    while (a > kd::kPi) a -= 2 * kd::kPi;
    while (a < -kd::kPi) a += 2 * kd::kPi;
    return a;
}

}  // namespace sim
