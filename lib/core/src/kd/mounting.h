// Mounting: how the IMU sits relative to the Blade, as the rotation taking
// sensor-frame readings into the body frame (+X Hilt to tip, +Y the way
// Line 0 faces). Learned once per build by Mounting Calibration.
#pragma once

#include <cstdint>

#include "dong_profile.h"
#include "kd_math.h"
#include "stillness.h"

namespace kd {

// The Mounting a Dong Profile's axis map describes. Returns false and
// leaves `sensorToBody` untouched if the map is not a proper rotation (a
// repeated axis, or a mirror image).
bool mountingFromAxisMap(const BodyAxisMap& map, Quat& sensorToBody);

// Two held poses: tip straight up, then Line 0 facing the sky. Each must be
// held still for kHoldSeconds; the second only counts once the Dong has
// turned away from the first.
class MountingCalibration {
public:
    enum class Step : uint8_t { TipUp, LineZeroUp, Done, Failed };

    static constexpr float kHoldSeconds = 1.5f;
    static constexpr float kMinTurnDeg = 45.f;   // pose 2 must differ this much from pose 1
    static constexpr float kMaxSkewDeg = 30.f;   // ... and be within this of perpendicular
    static constexpr float kSnapDeg = 20.f;      // snap to an axis-aligned Mounting within this

    void begin();

    // Raw sensor-frame readings (rad/s, m/s^2).
    void update(const Vec3& sensorGyro, const Vec3& sensorAccel, float dt);

    Step step() const { return step_; }
    bool active() const { return step_ == Step::TipUp || step_ == Step::LineZeroUp; }

    // 0..1 progress through holding the current pose.
    float holdProgress() const;

    // Short instruction for the display.
    const char* instruction() const;

    // Why calibration failed, when step() == Failed.
    const char* failure() const { return failure_; }

    // The learned Mounting, once step() == Done.
    Quat sensorToBody() const { return result_; }

private:
    void finish(const Vec3& up1, const Vec3& up2);

    Step step_ = Step::Done;
    Stillness stillness_;
    Vec3 tipUp_;          // sensor-frame "up" in pose 1 (body +X)
    bool turned_ = false;  // pose 2 only: has the Dong left pose 1 yet?
    const char* failure_ = "";
    Quat result_;
};

}  // namespace kd
