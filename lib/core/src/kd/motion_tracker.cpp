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
    zero_ = {};
    lastRate_ = {};
    clearDisplacement();
}

void MotionTracker::ignite(const Vec3& accel) {
    Quat q = Quat::fromTo(accel, Vec3{0, 0, 1});
    q = q.yawPart().conj() * q;  // start facing the reference heading
    filter_->reset(q);
    reference_ = {};
    zero_ = filter_->orientation();
    clearDisplacement();
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

    updateDisplacement(s.accel, rate, s.dt);
}

void MotionTracker::updateDisplacement(const Vec3& accel, const Vec3& rate, float dt) {
    const Vec3 linear = filter_->orientation().rotate(accel) - Vec3{0, 0, kGravity};
    const float mag = linear.norm();
    const Vec3 drive =
        mag > settings_.strokeDeadband ? linear * ((mag - settings_.strokeDeadband) / mag) : Vec3{};
    linearSmooth_ += (mag - linearSmooth_) * (dt / (0.08f + dt));
    const float spin = rate.norm();
    sinceRotating_ = spin > settings_.strokeGateRate ? 0.f : sinceRotating_ + dt;
    const bool rotating = sinceRotating_ < settings_.strokeGateHold;

    if (inStroke_) {
        if (rotating) {
            endStroke(false);
        } else {
            strokeVel_ += drive * dt;
            strokePos_ += strokeVel_ * dt;
            strokeTime_ += dt;
            const float d = strokePos_.norm();
            if (d > settings_.strokeMax) {
                strokePos_ *= settings_.strokeMax / d;
                const Vec3 out = strokePos_ * (1.f / settings_.strokeMax);
                const float outward = strokeVel_.dot(out);
                if (outward > 0.f) strokeVel_ -= out * outward;
            }
            const bool calm = linearSmooth_ < settings_.strokeCalmAccel && spin < settings_.strokeCalmRate;
            calmTime_ = calm ? calmTime_ + dt : 0.f;
            if (calmTime_ >= settings_.strokeCalmHold)
                endStroke(true);
            else if (strokeTime_ > settings_.strokeMaxTime)
                endStroke(false);
        }
    } else if (!rotating && mag > settings_.strokeDeadband) {
        inStroke_ = true;
        strokeVel_ = drive * dt;
        strokePos_ = strokeVel_ * dt;
        strokeTime_ = dt;
        calmTime_ = 0.f;
    }

    // Push springs back to rest.
    const float w = 2.f * kPi * settings_.pushHz;
    pushVel_ += (pushVel_ * (-2.f * w) - pushPos_ * (w * w)) * dt;
    pushPos_ += pushVel_ * dt;
}

void MotionTracker::endStroke(bool keep) {
    const Vec3 shown = strokePos_ * settings_.travelGain;
    const Vec3 level{shown.x, shown.y, 0.f};
    if (keep && Vec3{strokePos_.x, strokePos_.y, 0.f}.norm() >= settings_.strokeMin) {
        travel_ += level;
        pushPos_ += shown - level;
    } else {
        pushPos_ += shown;
    }
    inStroke_ = false;
    strokeVel_ = strokePos_ = {};
    strokeTime_ = calmTime_ = 0.f;
}

void MotionTracker::clearDisplacement() {
    travel_ = {};
    inStroke_ = false;
    strokeVel_ = strokePos_ = {};
    strokeTime_ = calmTime_ = 0.f;
    sinceRotating_ = settings_.strokeGateHold;
    linearSmooth_ = 0.f;
    pushPos_ = pushVel_ = {};
}

Vec3 MotionTracker::travel() const { return reference_.conj().rotate(travel_); }

void MotionTracker::setFilter(OrientationFilter& filter) {
    if (&filter == filter_) return;
    Quat q = filter_->orientation();
    filter_ = &filter;
    filter_->reset(q);
}

void MotionTracker::relevel() {
    if (!ignited_) return;
    reference_ = filter_->orientation().yawPart();
    zero_ = reference_.conj() * filter_->orientation();
    clearDisplacement();
}

Pose MotionTracker::pose(float lookahead) const {
    if (!ignited_) return {};
    Quat q = filter_->orientation();
    if (lookahead > 0.f) q = (q * Quat::fromRotationVector(lastRate_ * lookahead)).normalized();
    Quat toField = reference_.conj();
    const Vec3 stroke = inStroke_ ? strokePos_ * settings_.travelGain : Vec3{};
    return {toField * q, toField.rotate(travel_ + stroke + pushPos_)};
}

}  // namespace kd
