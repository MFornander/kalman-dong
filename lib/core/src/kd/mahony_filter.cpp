#include <cmath>

#include "orientation_filter.h"

namespace kd {

void MahonyFilter::reset(const Quat& orientation) {
    q_ = orientation.normalized();
    integral_ = {};
}

void MahonyFilter::update(const Vec3& gyro, const Vec3& accel, float dt) {
    Vec3 omega = gyro;

    float aNorm = accel.norm();
    float trust = 1.f - smoothstep(gains_.trustBand, gains_.distrustBand, std::fabs(aNorm - kGravity));
    if (trust > 0.f && aNorm > 1e-3f) {
        // Where the filter thinks "up" is, seen from the body.
        Vec3 upEstimate = q_.conj().rotate(Vec3{0, 0, 1});
        Vec3 upMeasured = accel * (1.f / aNorm);
        Vec3 error = upMeasured.cross(upEstimate) * trust;
        integral_ += error * (gains_.ki * dt);
        omega += error * gains_.kp + integral_;
    }

    q_ = (q_ * Quat::fromRotationVector(omega * dt)).normalized();
}

}  // namespace kd
