#include "../doctest_main.h"

#include <cmath>
#include <string>

#include "../sim_imu.h"
#include "kd/mounting.h"

using namespace kd;

namespace {

// A Dong whose IMU sits at `mount` (sensorToBody), held through poses by a
// simulated hand. Feeds sensor-frame readings to the calibration.
struct Bench {
    sim::Imu imu;
    Quat mount;
    MountingCalibration cal;

    explicit Bench(const Quat& sensorToBody) : mount(sensorToBody) {
        imu.gyroBias = {0.01f, -0.008f, 0.004f};
        imu.gyroNoise = 0.05f;
        imu.accelNoise = 0.08f;
        cal.begin();
    }

    void step() {
        ImuSample s = imu.step();
        cal.update(mount.conj().rotate(s.gyro), mount.conj().rotate(s.accel), s.dt);
    }
    void hold(double seconds) {
        imu.omega = [](double) { return Vec3{}; };
        for (double t = 0; t < seconds; t += 1.0 / imu.rate) step();
    }
    // Turns smoothly to `target` (body -> world) over `seconds`.
    void turnTo(const Quat& target, double seconds) {
        Quat delta = imu.truth.conj() * target;  // body-frame rotation
        if (delta.w < 0) delta = {-delta.w, -delta.x, -delta.y, -delta.z};
        float angle = 2.f * std::acos(clampf(delta.w, -1.f, 1.f));
        Vec3 axis = Vec3{delta.x, delta.y, delta.z}.normalized();
        Vec3 rate = axis * (angle / float(seconds));
        imu.omega = [rate](double) { return rate; };
        for (double t = 0; t < seconds; t += 1.0 / imu.rate) step();
    }
};

// Body orientations (body -> world) for the two poses, optionally sloppy.
Quat tipUp(float sloppyDeg = 0, float heading = 0.7f) {
    return Quat::fromAxisAngle({0, 0, 1}, heading) * Quat::fromAxisAngle({1, 1, 0}, degToRad(sloppyDeg)) *
           Quat::fromTo({1, 0, 0}, {0, 0, 1});
}
Quat lineZeroUp(float sloppyDeg = 0, float heading = -0.4f) {
    return Quat::fromAxisAngle({0, 0, 1}, heading) * Quat::fromAxisAngle({1, 0, 1}, degToRad(sloppyDeg)) *
           Quat::fromTo({0, 1, 0}, {0, 0, 1});
}

// A pose registers ~3 s after the turn ends: Stillness needs ~1.3 s to
// settle after a big turn, then the pose is held for kHoldSeconds.
constexpr double kPoseSeconds = 3.5;

void calibrate(Bench& b, float sloppyDeg = 0) {
    b.imu.truth = lineZeroUp(0, 2.0f);  // start somewhere else
    b.turnTo(tipUp(sloppyDeg), 1.0);
    b.hold(kPoseSeconds);
    b.turnTo(lineZeroUp(sloppyDeg), 1.0);
    b.hold(kPoseSeconds);
}

// The screen board, if its IMU's axes came out as body X = sensor -Y,
// Y = sensor +X, Z = sensor +Z.
Quat axisAlignedMount() {
    BodyAxisMap map;
    map.source[0] = 1; map.sign[0] = -1;
    map.source[1] = 0; map.sign[1] = 1;
    map.source[2] = 2; map.sign[2] = 1;
    Quat q;
    REQUIRE(mountingFromAxisMap(map, q));
    return q;
}

}  // namespace

TEST_CASE("rotation matrices convert to the same rotation") {
    for (Quat q : {Quat{}, Quat::fromAxisAngle({1, 2, 3}, 2.5f), Quat::fromAxisAngle({0, 0, 1}, kPi),
                   Quat::fromAxisAngle({1, 0, 0}, kPi), Quat::fromAxisAngle({0, 1, 0}, -3.0f)}) {
        Vec3 c0 = q.rotate({1, 0, 0}), c1 = q.rotate({0, 1, 0}), c2 = q.rotate({0, 0, 1});
        const float m[3][3] = {{c0.x, c1.x, c2.x}, {c0.y, c1.y, c2.y}, {c0.z, c1.z, c2.z}};
        CHECK(Quat::fromRotationMatrix(m).angleTo(q) < 1e-4f);
    }
}

TEST_CASE("a Dong Profile's axis map becomes a Mounting, mirrors are rejected") {
    Quat q;
    REQUIRE(mountingFromAxisMap(BodyAxisMap{}, q));
    CHECK(q.angleTo(Quat{}) < 1e-6f);

    Quat m = axisAlignedMount();
    CHECK(m.rotate({0, -1, 0}).x == doctest::Approx(1.f));  // sensor -Y is body +X
    CHECK(m.rotate({1, 0, 0}).y == doctest::Approx(1.f));

    BodyAxisMap mirror;
    mirror.sign[2] = -1;
    CHECK_FALSE(mountingFromAxisMap(mirror, q));
    BodyAxisMap repeated;
    repeated.source[1] = 0;
    CHECK_FALSE(mountingFromAxisMap(repeated, q));
}

TEST_CASE("two held poses recover an axis-aligned Mounting exactly, even held sloppily") {
    Bench b(axisAlignedMount());
    calibrate(b, 12.f);
    REQUIRE(b.cal.step() == MountingCalibration::Step::Done);
    CHECK(radToDeg(b.cal.sensorToBody().angleTo(b.mount)) < 0.01f);
}

TEST_CASE("an angled Mounting is measured rather than snapped") {
    Quat angled = Quat::fromAxisAngle({1, 0.3f, -0.5f}, degToRad(35)) * axisAlignedMount();
    Bench b(angled);
    calibrate(b);
    REQUIRE(b.cal.step() == MountingCalibration::Step::Done);
    CHECK(radToDeg(b.cal.sensorToBody().angleTo(angled)) < 2.f);
}

TEST_CASE("the first pose waits until the Dong has been turned into it") {
    Bench b(axisAlignedMount());
    b.imu.truth = tipUp();  // already lying in the right pose when calibration begins
    b.hold(5.0);
    CHECK(b.cal.step() == MountingCalibration::Step::TipUp);
    CHECK(b.cal.holdProgress() == 0.f);
    CHECK(std::string(b.cal.instruction()) == "Point the Blade at the ceiling");
}

TEST_CASE("the second pose waits until the Dong has actually turned") {
    Bench b(axisAlignedMount());
    b.imu.truth = lineZeroUp(0, 2.0f);
    b.turnTo(tipUp(), 1.0);
    b.hold(kPoseSeconds);
    CHECK(b.cal.step() == MountingCalibration::Step::LineZeroUp);
    b.hold(5.0);  // still in the first pose
    CHECK(b.cal.step() == MountingCalibration::Step::LineZeroUp);
    CHECK(b.cal.holdProgress() == 0.f);
    b.turnTo(lineZeroUp(), 1.0);
    CHECK(b.cal.step() == MountingCalibration::Step::LineZeroUp);  // not mid-turn
    b.hold(kPoseSeconds);
    CHECK(b.cal.step() == MountingCalibration::Step::Done);
}

TEST_CASE("poses that are not about 90 degrees apart fail with a reason") {
    Bench b(axisAlignedMount());
    b.imu.truth = lineZeroUp(0, 2.0f);
    b.turnTo(tipUp(), 1.0);
    b.hold(kPoseSeconds);
    b.turnTo(Quat::fromTo({1, 0, 0}, {0, 0, -1}), 1.5);  // tip straight down
    b.hold(kPoseSeconds);
    CHECK(b.cal.step() == MountingCalibration::Step::Failed);
    CHECK(std::string(b.cal.failure()).size() > 0);
}

TEST_CASE("moving never completes a pose") {
    Bench b(axisAlignedMount());
    b.imu.truth = tipUp();
    b.imu.omega = [](double t) { return Vec3{0.6f * float(std::sin(3 * t)), 0.4f, 0.f}; };
    for (int i = 0; i < 833 * 5; ++i) b.step();
    CHECK(b.cal.step() == MountingCalibration::Step::TipUp);
}

TEST_CASE("a pose held in a trembling hand still registers") {
    Bench b(axisAlignedMount());
    b.imu.truth = lineZeroUp(0, 2.0f);
    b.turnTo(tipUp(5.f), 1.0);
    // Hand tremor: ~8 Hz wobble of a few degrees per second plus small jolts.
    b.imu.omega = [](double t) {
        return Vec3{0.12f * float(std::sin(51 * t)), 0.1f * float(std::sin(43 * t + 1)), 0.08f * float(std::sin(37 * t))};
    };
    b.imu.linear = [](double t) { return Vec3{0.5f * float(std::sin(47 * t)), 0.4f * float(std::sin(53 * t)), 0.3f * float(std::sin(41 * t))}; };
    for (double t = 0; t < kPoseSeconds; t += 1.0 / b.imu.rate) b.step();
    CHECK(b.cal.step() == MountingCalibration::Step::LineZeroUp);
}
