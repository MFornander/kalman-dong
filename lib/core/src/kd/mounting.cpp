#include "mounting.h"

#include <cmath>

namespace kd {

namespace {

constexpr float kGravityTolerance = 0.1f;  // fraction of 1 g a held pose may read off by

float component(const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

// Snaps a unit vector to the nearest signed sensor axis if within `maxDeg`.
bool snapAxis(const Vec3& v, float maxDeg, Vec3& out) {
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (std::fabs(component(v, i)) > std::fabs(component(v, best))) best = i;
    float c = component(v, best);
    if (std::fabs(c) < std::cos(degToRad(maxDeg))) return false;
    float sign = c > 0.f ? 1.f : -1.f;
    out = best == 0 ? Vec3{sign, 0, 0} : (best == 1 ? Vec3{0, sign, 0} : Vec3{0, 0, sign});
    return true;
}

// Rotation whose rows are the body axes expressed in sensor coordinates.
Quat fromBodyAxes(const Vec3& x, const Vec3& y, const Vec3& z) {
    const float m[3][3] = {{x.x, x.y, x.z}, {y.x, y.y, y.z}, {z.x, z.y, z.z}};
    return Quat::fromRotationMatrix(m);
}

}  // namespace

bool mountingFromAxisMap(const BodyAxisMap& map, Quat& sensorToBody) {
    Vec3 rows[3];
    for (int i = 0; i < 3; ++i) {
        if (map.source[i] > 2 || (map.sign[i] != 1 && map.sign[i] != -1)) return false;
        float s = static_cast<float>(map.sign[i]);
        rows[i] = map.source[i] == 0 ? Vec3{s, 0, 0} : (map.source[i] == 1 ? Vec3{0, s, 0} : Vec3{0, 0, s});
    }
    // Proper rotation: distinct axes forming a right-handed set.
    if (rows[0].cross(rows[1]).dot(rows[2]) < 0.5f) return false;
    sensorToBody = fromBodyAxes(rows[0], rows[1], rows[2]);
    return true;
}

void MountingCalibration::begin() {
    step_ = Step::TipUp;
    stillness_.reset();
    havePrevious_ = false;
    turned_ = false;
    failure_ = "";
}

float MountingCalibration::holdProgress() const {
    if (!active() || !turned_) return 0.f;
    return clampf(stillness_.stillTime() / kHoldSeconds, 0.f, 1.f);
}

const char* MountingCalibration::instruction() const {
    switch (step_) {
        case Step::TipUp: return turned_ ? "Blade at the ceiling: hold" : "Point the Blade at the ceiling";
        case Step::LineZeroUp: return turned_ ? "Blade level, Line 0 on top: hold" : "Blade level, Line 0 on top";
        case Step::Done: return "Mounting saved";
        case Step::Failed: return failure_;
    }
    return "";
}

void MountingCalibration::update(const Vec3& sensorGyro, const Vec3& sensorAccel, float dt) {
    if (!active()) return;
    stillness_.update(sensorGyro, sensorAccel, dt);

    // A pose only counts once the Dong has been turned into it, so the pose
    // it happened to be in when calibration began (or the previous pose) is
    // never recorded by accident.
    if (!havePrevious_) {
        previousUp_ = sensorAccel.normalized();
        havePrevious_ = true;
    }
    if (!turned_) {
        float turn = radToDeg(std::acos(clampf(stillness_.recentAccel().normalized().dot(previousUp_), -1.f, 1.f)));
        if (turn < kMinTurnDeg) return;
        turned_ = true;
        stillness_.reset();
        return;
    }
    if (stillness_.stillTime() < kHoldSeconds) return;

    Vec3 up = stillness_.stillAccel();
    if (std::fabs(up.norm() - kGravity) > kGravityTolerance * kGravity) {
        stillness_.reset();  // not a clean hold (still accelerating); try again
        return;
    }
    if (step_ == Step::TipUp) {
        tipUp_ = previousUp_ = up.normalized();
        step_ = Step::LineZeroUp;
        turned_ = false;
        stillness_.reset();
    } else {
        finish(tipUp_, up.normalized());
    }
}

void MountingCalibration::finish(const Vec3& up1, const Vec3& up2) {
    float angle = radToDeg(std::acos(clampf(up1.dot(up2), -1.f, 1.f)));
    if (std::fabs(angle - 90.f) > kMaxSkewDeg) {
        step_ = Step::Failed;
        failure_ = "Poses not 90 deg apart";
        return;
    }
    // Body axes in sensor coordinates: +X was up in pose 1, +Y in pose 2.
    Vec3 x = up1;
    Vec3 y = (up2 - x * x.dot(up2)).normalized();
    Vec3 sx, sy;
    if (snapAxis(x, kSnapDeg, sx) && snapAxis(y, kSnapDeg, sy) && std::fabs(sx.dot(sy)) < 0.5f) {
        x = sx;
        y = sy;
    }
    result_ = fromBodyAxes(x, y, x.cross(y));
    step_ = Step::Done;
}

}  // namespace kd
