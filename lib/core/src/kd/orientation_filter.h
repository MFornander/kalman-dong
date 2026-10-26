// Orientation filters: fuse gyro and accelerometer into an orientation.
//
// Implementations are interchangeable: MotionTracker takes any of them and
// can swap them live. FilterBank (filter_bank.h) lists them all and names the
// default (Mahony; the Kalman filter tied it on real recordings but drifts
// more at rest and costs twice the CPU). Heading cannot be corrected by a
// 6-axis IMU; gyro bias is handled by MotionTracker, which passes in
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

// Error-state Kalman filter. State: orientation plus the gyro bias left over
// after MotionTracker's own bias estimate. It predicts with the gyro and
// corrects tilt with the accelerometer, trusting the accelerometer less the
// further its magnitude strays from 1 g (i.e. while swinging). Heading and
// its bias stay unobservable without a magnetometer; the residual bias about
// vertical is only learned through its correlation with tilt as the Dong
// turns.
class KalmanFilter : public OrientationFilter {
public:
    struct Noise {
        float angleWalk = 0.003f;     // rad/sqrt(s); gyro angle random walk
        float scaleError = 0.01f;     // fraction of rate; gyro scale/axis errors
        float biasWalk = 0.0005f;     // rad/s/sqrt(s); bias drift
        float initialTilt = 0.05f;    // rad
        float initialBias = 0.01f;    // rad/s
        float accelDirection = 0.05f; // rad; accelerometer direction noise at rest
        float swingDistrust = 3.0f;   // extra rad of direction noise per g of |accel| error
        float rejectBeyond = 0.15f;   // g; skip correction when |accel| is this far from 1 g
        // Skip readings whose disagreement with the prediction is this
        // unlikely given the filter's own uncertainty (chi-squared, 3 dof;
        // 16.3 = 0.1% false-reject). Swings often pass through |accel| = 1 g
        // while pointing nowhere near down; this catches them.
        float innovationGate = 16.3f;
    };

    KalmanFilter() = default;
    explicit KalmanFilter(const Noise& n) : noise_(n) {}

    const char* name() const override { return "Kalman"; }
    void reset(const Quat& orientation) override;
    void update(const Vec3& gyro, const Vec3& accel, float dt) override;
    Quat orientation() const override { return q_; }

    // Residual gyro bias the filter has learned (rad/s), for diagnostics.
    Vec3 residualBias() const { return bias_; }

private:
    void predict(const Vec3& rate, float dt);
    void correct(const Vec3& accel);

    Noise noise_;
    Quat q_;
    Vec3 bias_;
    float P_[6][6] = {};  // covariance of [orientation error (body frame), bias error]
};

}  // namespace kd
