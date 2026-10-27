// MotionTracker turns IMU samples into the Dong's Pose in the Cloud Field.
//
// It owns everything around the orientation filter: Ignition (learning which
// way is down and the gyro bias), Rest detection and bias re-learning,
// Re-level, Travel and Push, and predicting the Pose forward to when Pixels light up.
#pragma once

#include "blade_geometry.h"
#include "kd_math.h"
#include "orientation_filter.h"
#include "stillness.h"

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
    // Once at Rest, the Dong counts as lying down (and its Rest time keeps
    // running) until it is picked up: turned `pickUpTurn` from where it lay,
    // or moving for `pickUpMoving` s in all. A bump of the table does neither.
    float pickUpTurn = 0.35f;      // rad, about 20 degrees
    float pickUpMoving = 2.0f;     // s

    // Ignition finishes after `ignitionStill` s of Rest, or after
    // `ignitionTimeout` s regardless (falling back to the saved bias).
    float ignitionStill = 1.0f;
    float ignitionTimeout = 2.0f;

    // Travel: a stroke of the Hilt without rotation moves the Dong through
    // the Cloud Field for good. A stroke starts when the linear acceleration
    // passes `strokeDeadband` while the Dong isn't rotating, and ends once it
    // has been calm for `strokeCalmHold`. Its level part is then kept as
    // Travel; everything else (up and down, small jiggles, strokes cut short
    // by a swing) becomes Push, a critically damped spring back to zero.
    float strokeDeadband = 0.4f;   // m/s^2 of linear acceleration ignored
    float strokeGateRate = 1.5f;   // rad/s; turning faster than this is a swing, not a stroke
    float strokeGateHold = 0.2f;   // s; ... and for this long after
    float strokeCalmAccel = 0.6f;  // m/s^2; smoothed linear acceleration below this is calm
    float strokeCalmRate = 0.6f;   // rad/s
    float strokeCalmHold = 0.15f;  // s
    float strokeMaxTime = 2.f;     // s; longer strokes (shaking, walking) are not trusted
    float strokeMin = 0.05f;       // m; shorter level strokes spring back instead
    float strokeMax = 1.f;         // m
    float travelGain = 0.6f;       // share of each stroke shown; hides integration error
    float pushHz = 0.8f;           // Push spring frequency
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

    bool atRest() const { return stillness_.stillTime() >= settings_.restHold; }
    // Lying down: at Rest, and not picked up since (see MotionSettings::pickUpTurn).
    bool lying() const { return lying_; }
    // Seconds lying down; 0 once picked up.
    float restSeconds() const { return lying_ ? lyingTime_ : 0.f; }

    // Make the current heading the Cloud Field's reference heading, and bring
    // the Dong back to the field's origin (clearing Travel and Push).
    void relevel();

    // Travel so far, in the Cloud Field frame (m).
    Vec3 travel() const;

    // The orientation (in the Cloud Field frame) at the last Ignition or
    // Re-level: where readouts of how the Dong is turned count from.
    Quat zeroOrientation() const { return zero_; }

    // Pose now, or predicted `lookahead` seconds ahead.
    Pose pose(float lookahead = 0.f) const;

    Vec3 gyroBias() const { return bias_; }
    // Increments whenever the bias is re-learned, so callers know to save it.
    unsigned biasGeneration() const { return biasGeneration_; }

    const OrientationFilter& filter() const { return *filter_; }

    // Swaps in another orientation filter, handing over the current
    // orientation so the Cloud Field doesn't jump.
    void setFilter(OrientationFilter& filter);

private:
    void ignite(const Vec3& accel);
    void updateLying(float dt);
    void updateDisplacement(const Vec3& accel, const Vec3& rate, float dt);
    void endStroke(bool keep);
    void clearDisplacement();

    OrientationFilter* filter_;
    MotionSettings settings_;

    bool ignited_ = false;
    float elapsed_ = 0;
    float ignitionProgress_ = 0;

    Vec3 bias_;
    unsigned biasGeneration_ = 0;

    Stillness stillness_;
    float learnedThisRest_ = 0;  // s of bias learning in the current Rest
    bool lying_ = false;
    float lyingTime_ = 0;        // s
    float movingTime_ = 0;       // s moving since last at Rest
    Quat layAt_;                 // filter orientation while at Rest

    Quat reference_;     // heading reference set by Re-level
    Quat zero_;          // see zeroOrientation()
    Vec3 lastRate_;       // bias-corrected body rate, for prediction
    // Displacement, all in the filter's world frame.
    Vec3 travel_;
    bool inStroke_ = false;
    Vec3 strokeVel_, strokePos_;  // as measured, before travelGain
    float strokeTime_ = 0, calmTime_ = 0;
    float sinceRotating_ = 0;  // s since turning faster than strokeGateRate
    float linearSmooth_ = 0;   // m/s^2; smoothed size of the linear acceleration
    Vec3 pushPos_, pushVel_;
};

}  // namespace kd
