// Small float vector and quaternion math. Single precision throughout: the
// ESP32-S3 has a single-precision FPU and nothing here needs more.
#pragma once

#include <cmath>

namespace kd {

constexpr float kPi = 3.14159265358979f;
constexpr float kGravity = 9.80665f;  // m/s^2

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
// floor() for values well inside int range; std::floor is an out-of-line
// call on the ESP32-S3 and this sits in the hottest loops.
inline int fastFloor(float v) {
    int i = static_cast<int>(v);
    return v < static_cast<float>(i) ? i - 1 : i;
}
inline float degToRad(float d) { return d * (kPi / 180.f); }
inline float radToDeg(float r) { return r * (180.f / kPi); }

struct Vec3 {
    float x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float norm() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const {
        float n = norm();
        return n > 1e-9f ? *this * (1.f / n) : Vec3{};
    }
};

// Unit quaternion (w, x, y, z). As an orientation it rotates body-frame
// vectors into the world frame: v_world = q.rotate(v_body).
struct Quat {
    float w = 1, x = 0, y = 0, z = 0;

    constexpr Quat() = default;
    constexpr Quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}

    static Quat fromAxisAngle(const Vec3& axis, float angle) {
        Vec3 a = axis.normalized();
        float s = std::sin(angle * 0.5f);
        return {std::cos(angle * 0.5f), a.x * s, a.y * s, a.z * s};
    }

    // Rotation by rotation vector v (axis * angle).
    static Quat fromRotationVector(const Vec3& v) {
        float angle = v.norm();
        if (angle < 1e-9f) return Quat{1, v.x * 0.5f, v.y * 0.5f, v.z * 0.5f}.normalized();
        return fromAxisAngle(v * (1.f / angle), angle);
    }

    // Shortest rotation taking unit vector `from` onto unit vector `to`.
    static Quat fromTo(const Vec3& from, const Vec3& to) {
        Vec3 f = from.normalized(), t = to.normalized();
        float d = f.dot(t);
        if (d < -0.999999f) {
            Vec3 axis = Vec3{1, 0, 0}.cross(f);
            if (axis.norm() < 1e-6f) axis = Vec3{0, 1, 0}.cross(f);
            return fromAxisAngle(axis, kPi);
        }
        Vec3 c = f.cross(t);
        return Quat{1.f + d, c.x, c.y, c.z}.normalized();
    }

    Quat operator*(const Quat& o) const {
        return {w * o.w - x * o.x - y * o.y - z * o.z,
                w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x,
                w * o.z + x * o.y - y * o.x + z * o.w};
    }

    Quat conj() const { return {w, -x, -y, -z}; }

    Quat normalized() const {
        float n = std::sqrt(w * w + x * x + y * y + z * z);
        return n > 1e-9f ? Quat{w / n, x / n, y / n, z / n} : Quat{};
    }

    Vec3 rotate(const Vec3& v) const {
        // v' = v + 2w(u x v) + 2 u x (u x v), u = vector part
        Vec3 u{x, y, z};
        Vec3 t = u.cross(v) * 2.f;
        return v + t * w + u.cross(t);
    }

    // Angle (radians, 0..pi) of the rotation between this and o.
    float angleTo(const Quat& o) const {
        float d = std::fabs(w * o.w + x * o.x + y * o.y + z * o.z);
        return 2.f * std::acos(clampf(d, 0.f, 1.f));
    }

    // The part of this rotation that is a turn about the world vertical (Z):
    // q = yawPart() * (rotation with no vertical turn). Identity when undefined.
    Quat yawPart() const {
        float n = std::sqrt(w * w + z * z);
        if (n < 1e-6f) return Quat{};
        return {w / n, 0, 0, z / n};
    }
};

}  // namespace kd
