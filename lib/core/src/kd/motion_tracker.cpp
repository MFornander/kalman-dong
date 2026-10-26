#include "motion_tracker.h"

#include <cmath>

namespace kd {

namespace {
constexpr float kMaxDt = 0.05f;  // s; larger gaps are treated as this
}  // namespace

MotionTracker::MotionTracker(OrientationFilter& filter, const MotionSettings& settings)
    : filter_(&filter), settings_(settings), stillness_({settings.restGyroNoise, settings.restAccelNoise}) {}

void MotionTracker::begin(const Vec3& savedBias) {
    ignited_ = false;
    elapsed_ = 0;
    ignitionProgress_ = 0;
    bias_ = savedBias;
    stillness_.reset();
    learnedThisRest_ = 0;
    reference_ = {};
    lastRate_ = {};
    pushPos_ = pushVel_ = {};
}

void MotionTracker::ignite(const Vec3& accel) {
    Quat q = Quat::fromTo(accel, Vec3{0, 0, 1});
    q = q.yawPart().conj() * q;  // start facing the reference heading
    filter_->reset(q);
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
    stillness_.update(s.gyro, s.accel, s.dt);
    if (stillness_.stillTime() <= 0.f) learnedThisRest_ = 0;

    if (!ignited_) {
        float stillTime = stillness_.stillTime();
        float progress = std::fmax(stillTime / settings_.ignitionStill, elapsed_ / settings_.ignitionTimeout);
        ignitionProgress_ = clampf(std::fmax(ignitionProgress_, progress), 0.f, 1.f);
        if (stillTime >= settings_.ignitionStill) {
            bias_ = stillness_.stillGyro();
            ++biasGeneration_;
            ignite(stillness_.stillAccel());
        } else if (elapsed_ >= settings_.ignitionTimeout) {
            ignite(stillness_.recentAccel());  // keep the saved bias
        }
        return;
    }

    Vec3 rate = s.gyro - bias_;
    filter_->update(rate, s.accel, s.dt);
    lastRate_ = rate;

    const Vec3 recentGyro = stillness_.recentGyro();
    if (atRest() && (recentGyro - bias_).norm() < settings_.restMaxBiasStep) {
        bias_ += (recentGyro - bias_) * (s.dt / (settings_.biasTimeConstant + s.dt));
        learnedThisRest_ += s.dt;
        // Report a new bias once per Rest, after it has had time to settle.
        if (learnedThisRest_ >= 2.f * settings_.biasTimeConstant &&
            learnedThisRest_ - s.dt < 2.f * settings_.biasTimeConstant)
            ++biasGeneration_;
    }

    updatePush(s.accel, s.dt);
}

void MotionTracker::updatePush(const Vec3& accel, float dt) {
    Vec3 linear = filter_->orientation().rotate(accel) - Vec3{0, 0, kGravity};
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

void MotionTracker::setFilter(OrientationFilter& filter) {
    if (&filter == filter_) return;
    Quat q = filter_->orientation();
    filter_ = &filter;
    filter_->reset(q);
}

void MotionTracker::relevel() {
    if (ignited_) reference_ = filter_->orientation().yawPart();
}

Pose MotionTracker::pose(float lookahead) const {
    if (!ignited_) return {};
    Quat q = filter_->orientation();
    if (lookahead > 0.f) q = (q * Quat::fromRotationVector(lastRate_ * lookahead)).normalized();
    Quat toField = reference_.conj();
    return {toField * q, toField.rotate(pushPos_)};
}

}  // namespace kd
