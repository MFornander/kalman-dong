// Orientation filters: fuse gyro and accelerometer into an orientation.
//
// Two implementations are planned behind this interface: Mahony first, then
// a Kalman filter, compared on recorded swings. Heading cannot be corrected
// by a 6-axis IMU; gyro bias is handled by MotionTracker, which passes in
// bias-corrected rates.
#pragma once

#include "kd_math.h"

namespace kd {

class OrientationFilter {
public:
    virtual ~OrientationFilter() = default;
    virtual const char* name() const = 0;

    virtual void reset(const Quat& orientation) = 0;

    // gyro: body-frame rate, bias removed (rad/s). accel: body-frame specific
    // force (m/s^2; reads +g upward at rest). dt in seconds.
    virtual void update(const Vec3& gyro, const Vec3& accel, float dt) = 0;

    // body -> world (Z up).
    virtual Quat orientation() const = 0;
};

// Mahony complementary filter: tilt is pulled toward the measured gravity
// direction; the pull fades out while the accelerometer is dominated by swing
// forces rather than gravity.
class MahonyFilter : public OrientationFilter {
public:
    struct Gains {
        float kp = 1.0f;   // 1/s; how fast tilt follows gravity
        float ki = 0.02f;  // 1/s^2; residual tilt-axis bias correction
        // Gravity is trusted fully within `trustBand` of 1 g and not at all
        // beyond `distrustBand` (both in m/s^2).
        float trustBand = 0.5f;
        float distrustBand = 2.5f;
    };

    MahonyFilter() = default;
    explicit MahonyFilter(const Gains& g) : gains_(g) {}

    const char* name() const override { return "Mahony"; }
    void reset(const Quat& orientation) override;
    void update(const Vec3& gyro, const Vec3& accel, float dt) override;
    Quat orientation() const override { return q_; }

private:
    Gains gains_;
    Quat q_;
    Vec3 integral_;
};

}  // namespace kd
