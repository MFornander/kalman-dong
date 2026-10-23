// MotionTracker turns IMU samples into the Dong's Pose in the Cloud Field.
//
// It owns everything around the orientation filter: Ignition (learning which
// way is down and the gyro bias), Rest detection and bias re-learning,
// Re-level, Push, and predicting the Pose forward to when Pixels light up.
#pragma once

#include "blade_geometry.h"
#include "kd_math.h"
#include "orientation_filter.h"

namespace kd {

// One IMU reading, already in the Dong's body frame.
struct ImuSample {
    float dt = 0;  // s since the previous sample
    Vec3 gyro;     // rad/s, raw (bias not removed)
    Vec3 accel;    // m/s^2, specific force (+g upward at rest)
};

struct MotionSettings {
    // Rest: smoothed gyro and accelerometer steady for `restHold` seconds.
    float restGyroNoise = 0.03f;   // rad/s; allowed wobble around the recent mean
    float restAccelNoise = 0.2f;   // m/s^2; allowed wobble around the recent mean
    float restMaxBiasStep = 0.03f; // rad/s; larger steady rates are real turning, not bias
    float restHold = 0.5f;         // s
    float biasTimeConstant = 2.0f; // s; how fast bias is re-learned at Rest

    // Ignition finishes after `ignitionStill` s of Rest, or after
    // `ignitionTimeout` s regardless (falling back to the saved bias).
    float ignitionStill = 1.0f;
    float ignitionTimeout = 2.0f;

    // Push: a critically damped spring pulling the Dong's apparent
    // displacement back to zero.
    float pushHz = 0.8f;        // spring frequency
    float pushGain = 1.0f;      // 1 = displacement as measured
    float pushDeadband = 0.4f;  // m/s^2 of linear acceleration ignored
    float pushMax = 0.3f;       // m
};

class MotionTracker {
public:
    explicit MotionTracker(OrientationFilter& filter, const MotionSettings& settings = {});

    // Starts Ignition. `savedBias` is the last gyro bias stored on the Dong.
    void begin(const Vec3& savedBias);

    void update(const ImuSample& sample);

    // Ignition progress 0..1; never goes backwards.
    float ignitionProgress() const { return ignitionProgress_; }
    bool ignited() const { return ignited_; }

    bool atRest() const { return stillTime_ >= settings_.restHold; }
    float restSeconds() const { return atRest() ? stillTime_ - settings_.restHold : 0.f; }

    // Make the current heading the Cloud Field's reference heading.
    void relevel();

    // Pose now, or predicted `lookahead` seconds ahead.
    Pose pose(float lookahead = 0.f) const;

    Vec3 gyroBias() const { return bias_; }
    // Increments whenever the bias is re-learned, so callers know to save it.
    unsigned biasGeneration() const { return biasGeneration_; }

    const OrientationFilter& filter() const { return filter_; }

private:
    void ignite(const Vec3& accel);
    void updateStillness(const ImuSample& s);
    void updatePush(const Vec3& accel, float dt);

    OrientationFilter& filter_;
    MotionSettings settings_;

    bool ignited_ = false;
    float elapsed_ = 0;
    float ignitionProgress_ = 0;

    Vec3 bias_;
    unsigned biasGeneration_ = 0;

    // Stillness: short-term means and how long readings stayed near them.
    bool haveMeans_ = false;
    Vec3 gyroFast_;
    Vec3 accelFast_;
    Vec3 gyroMean_;
    Vec3 accelMean_;
    float stillTime_ = 0;
    Vec3 stillGyroSum_;
    Vec3 stillAccelSum_;
    float stillWeight_ = 0;
    float learnedThisRest_ = 0;  // s of bias learning in the current Rest

    Quat reference_;     // heading reference set by Re-level
    Vec3 lastRate_;       // bias-corrected body rate, for prediction
    Vec3 pushPos_;        // world frame
    Vec3 pushVel_;
};

}  // namespace kd
