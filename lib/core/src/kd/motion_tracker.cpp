#include "motion_tracker.h"

#include <cmath>

namespace kd {

namespace {
constexpr float kMeanTimeConstant = 0.2f;  // s; window for "recent mean" in Rest detection
constexpr float kMaxDt = 0.05f;            // s; larger gaps are treated as this
}  // namespace

MotionTracker::MotionTracker(OrientationFilter& filter, const MotionSettings& settings)
    : filter_(filter), settings_(settings) {}

void MotionTracker::begin(const Vec3& savedBias) {
    ignited_ = false;
    elapsed_ = 0;
    ignitionProgress_ = 0;
    bias_ = savedBias;
    haveMeans_ = false;
    stillTime_ = 0;
    stillGyroSum_ = stillAccelSum_ = {};
    stillWeight_ = 0;
    learnedThisRest_ = 0;
    reference_ = {};
    lastRate_ = {};
    pushPos_ = pushVel_ = {};
}

void MotionTracker::updateStillness(const ImuSample& s) {
    if (!haveMeans_) {
        gyroMean_ = s.gyro;
        accelMean_ = s.accel;
        haveMeans_ = true;
    }
    float a = s.dt / (kMeanTimeConstant + s.dt);
    gyroMean_ += (s.gyro - gyroMean_) * a;
    accelMean_ += (s.accel - accelMean_) * a;

    bool still = (s.gyro - gyroMean_).norm() < settings_.restGyroNoise &&
                 (s.accel - accelMean_).norm() < settings_.restAccelNoise;
    if (still) {
        stillTime_ += s.dt;
        stillGyroSum_ += s.gyro * s.dt;
        stillAccelSum_ += s.accel * s.dt;
        stillWeight_ += s.dt;
    } else {
        stillTime_ = 0;
        stillGyroSum_ = stillAccelSum_ = {};
        stillWeight_ = 0;
        learnedThisRest_ = 0;
    }
}

void MotionTracker::ignite(const Vec3& accel) {
    Quat q = Quat::fromTo(accel, Vec3{0, 0, 1});
    q = q.yawPart().conj() * q;  // start facing the reference heading
    filter_.reset(q);
    reference_ = {};
    pushPos_ = pushVel_ = {};
    ignited_ = true;
    ignitionProgress_ = 1.f;
}

void MotionTracker::update(const ImuSample& sample) {
    ImuSample s = sample;
    s.dt = clampf(s.dt, 0.f, kMaxDt);
    if (s.dt <= 0.f) return;
    elapsed_ += s.dt;
    updateStillness(s);

    if (!ignited_) {
        float progress = std::fmax(stillTime_ / settings_.ignitionStill, elapsed_ / settings_.ignitionTimeout);
        ignitionProgress_ = clampf(std::fmax(ignitionProgress_, progress), 0.f, 1.f);
        if (stillTime_ >= settings_.ignitionStill && stillWeight_ > 0.f) {
            bias_ = stillGyroSum_ * (1.f / stillWeight_);
            ++biasGeneration_;
            ignite(stillAccelSum_ * (1.f / stillWeight_));
        } else if (elapsed_ >= settings_.ignitionTimeout) {
            ignite(accelMean_);  // keep the saved bias
        }
        return;
    }

    Vec3 rate = s.gyro - bias_;
    filter_.update(rate, s.accel, s.dt);
    lastRate_ = rate;

    if (atRest() && (gyroMean_ - bias_).norm() < settings_.restMaxBiasStep) {
        bias_ += (gyroMean_ - bias_) * (s.dt / (settings_.biasTimeConstant + s.dt));
        learnedThisRest_ += s.dt;
        // Report a new bias once per Rest, after it has had time to settle.
        if (learnedThisRest_ >= 2.f * settings_.biasTimeConstant &&
            learnedThisRest_ - s.dt < 2.f * settings_.biasTimeConstant)
            ++biasGeneration_;
    }

    updatePush(s.accel, s.dt);
}

void MotionTracker::updatePush(const Vec3& accel, float dt) {
    Vec3 linear = filter_.orientation().rotate(accel) - Vec3{0, 0, kGravity};
    float mag = linear.norm();
    Vec3 drive = mag > settings_.pushDeadband ? linear * ((mag - settings_.pushDeadband) / mag) : Vec3{};

    float w = 2.f * kPi * settings_.pushHz;
    Vec3 acc = drive * settings_.pushGain - pushVel_ * (2.f * w) - pushPos_ * (w * w);
    pushVel_ += acc * dt;
    pushPos_ += pushVel_ * dt;

    float d = pushPos_.norm();
    if (d > settings_.pushMax) {
        pushPos_ *= settings_.pushMax / d;
        // Stop pushing further out; keep any velocity heading back in.
        Vec3 out = pushPos_ * (1.f / settings_.pushMax);
        float outward = pushVel_.dot(out);
        if (outward > 0.f) pushVel_ -= out * outward;
    }
}

void MotionTracker::relevel() {
    if (ignited_) reference_ = filter_.orientation().yawPart();
}

Pose MotionTracker::pose(float lookahead) const {
    if (!ignited_) return {};
    Quat q = filter_.orientation();
    if (lookahead > 0.f) q = (q * Quat::fromRotationVector(lastRate_ * lookahead)).normalized();
    Quat toField = reference_.conj();
    return {toField * q, toField.rotate(pushPos_)};
}

}  // namespace kd
