#include "pose_readout.h"

#include <cmath>

namespace kd {

PoseReadout readout(const Pose& pose) {
    PoseReadout r;
    const Vec3& d = pose.displacement;
    const float dm[3] = {d.x * 10.f, d.y * 10.f, d.z * 10.f};
    for (int i = 0; i < 3; ++i) r.pos[i] = ((static_cast<int>(std::lround(dm[i])) + 50) % 100 + 100) % 100 - 50;

    const Quat& q = pose.orientation;
    const float roll = std::atan2(2.f * (q.w * q.x + q.y * q.z), 1.f - 2.f * (q.x * q.x + q.y * q.y));
    const float pitch = std::asin(clampf(2.f * (q.w * q.y - q.z * q.x), -1.f, 1.f));  // positive tips the Blade down
    const float yaw = std::atan2(2.f * (q.w * q.z + q.x * q.y), 1.f - 2.f * (q.y * q.y + q.z * q.z));  // counter-clockwise
    const float turn[3] = {roll, -pitch, -yaw};
    for (int i = 0; i < 3; ++i) r.rot[i] = static_cast<int>(std::lround(turn[i] * (100.f / kPi)));
    return r;
}

}  // namespace kd
