#include "../doctest_main.h"

#include "kd/kd_math.h"

using namespace kd;

static bool near(const Vec3& a, const Vec3& b, float eps = 1e-4f) { return (a - b).norm() < eps; }

TEST_CASE("quaternion rotates vectors like the axis-angle it was built from") {
    Quat q = Quat::fromAxisAngle({0, 0, 1}, kPi / 2);
    CHECK(near(q.rotate({1, 0, 0}), {0, 1, 0}));
    CHECK(near(q.conj().rotate({0, 1, 0}), {1, 0, 0}));
}

TEST_CASE("composition applies the right-hand rotation first") {
    Quat yaw = Quat::fromAxisAngle({0, 0, 1}, kPi / 2);
    Quat pitch = Quat::fromAxisAngle({0, 1, 0}, -kPi / 2);  // tips +X up to +Z
    CHECK(near((yaw * pitch).rotate({1, 0, 0}), {0, 0, 1}));
    CHECK(near((pitch * yaw).rotate({1, 0, 0}), {0, 1, 0}));
}

TEST_CASE("fromTo maps one direction onto another, including opposite ones") {
    for (Vec3 from : {Vec3{1, 0, 0}, Vec3{0.3f, -0.5f, 0.8f}, Vec3{0, 0, -1}}) {
        Quat q = Quat::fromTo(from, {0, 0, 1});
        CHECK(near(q.rotate(from.normalized()), {0, 0, 1}));
    }
}

TEST_CASE("fromRotationVector agrees with fromAxisAngle and handles zero") {
    Quat a = Quat::fromRotationVector({0, 0.5f, 0});
    Quat b = Quat::fromAxisAngle({0, 1, 0}, 0.5f);
    CHECK(a.angleTo(b) < 1e-5f);
    CHECK(Quat::fromRotationVector({}).angleTo(Quat{}) < 1e-6f);
}

TEST_CASE("yawPart isolates the turn about vertical") {
    Quat yaw = Quat::fromAxisAngle({0, 0, 1}, 1.2f);
    Quat tilt = Quat::fromAxisAngle({1, 0, 0}, 0.7f);
    Quat q = yaw * tilt;
    CHECK(q.yawPart().angleTo(yaw) < 1e-5f);
    // Removing the yaw leaves no heading turn.
    Quat rest = q.yawPart().conj() * q;
    CHECK(rest.yawPart().angleTo(Quat{}) < 1e-5f);
}
