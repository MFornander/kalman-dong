#include "../doctest_main.h"

#include "kd/pose_readout.h"

using namespace kd;

namespace {

Quat turn(Vec3 axis, float deg) { return Quat::fromAxisAngle(axis, degToRad(deg)); }

Pose turned(Vec3 axis, float deg) {
    Pose p;
    p.orientation = turn(axis, deg);
    return p;
}

}  // namespace

TEST_CASE("at the origin, as turned as at the start, reads all zeros") {
    PoseReadout r = readout(Pose{});
    for (int i = 0; i < 3; ++i) {
        CHECK(r.pos[i] == 0);
        CHECK(r.rot[i] == 0);
    }
    // However the Dong was held at Ignition or Re-level, that pose reads 0.
    const Quat start = turn({1, 0, 0}, 90.f) * turn({0, 1, 0}, 20.f);
    Pose p;
    p.orientation = start;
    r = readout(p, start);
    for (int i = 0; i < 3; ++i) CHECK(r.rot[i] == 0);
}

TEST_CASE("POS is in centimetres and wraps every 10 m") {
    Pose p;
    p.displacement = {0.034f, -1.2f, 0.004f};
    PoseReadout r = readout(p);
    CHECK(r.pos[0] == 3);
    CHECK(r.pos[1] == -120);
    CHECK(r.pos[2] == 0);

    p.displacement = {4.99f, 5.0f, 7.3f};
    r = readout(p);
    CHECK(r.pos[0] == 499);
    CHECK(r.pos[1] == -500);  // 5 m wraps to the far side
    CHECK(r.pos[2] == -270);  // 7.3 m is 2.7 m short of the next wrap

    p.displacement = {-5.01f, -10.f, 12.f};
    r = readout(p);
    CHECK(r.pos[0] == 499);
    CHECK(r.pos[1] == 0);
    CHECK(r.pos[2] == 200);
}

TEST_CASE("ROT is percent of a half turn: tip up, turned clockwise and rolled clockwise read positive") {
    // Tip raised 45 degrees: rotating about +Y lowers +X, so raising is -45.
    PoseReadout r = readout(turned({0, 1, 0}, -45.f));
    CHECK(r.rot[0] == 0);
    CHECK(r.rot[1] == 25);
    CHECK(r.rot[2] == 0);

    // Turned 90 degrees clockwise seen from above (Z up): -90 about +Z.
    r = readout(turned({0, 0, 1}, -90.f));
    CHECK(r.rot[2] == 50);
    CHECK(r.rot[1] == 0);

    // Rolled 36 degrees clockwise looking out along the Blade (+X).
    r = readout(turned({1, 0, 0}, 36.f));
    CHECK(r.rot[0] == 20);
}

TEST_CASE("ROT counts from the start pose, about its own axes") {
    // Started rolled a quarter turn; rolling a further 18 degrees reads 10.
    const Quat start = turn({1, 0, 0}, 90.f);
    Pose p;
    p.orientation = start * turn({1, 0, 0}, 18.f);
    PoseReadout r = readout(p, start);
    CHECK(r.rot[0] == 10);
    CHECK(r.rot[1] == 0);
    CHECK(r.rot[2] == 0);
    // Rolling back a quarter turn past the start reads -50.
    p.orientation = start * turn({1, 0, 0}, -90.f);
    CHECK(readout(p, start).rot[0] == -50);
}

TEST_CASE("a half turn reads 100 either way") {
    PoseReadout r = readout(turned({0, 0, 1}, 180.f));
    CHECK(std::abs(r.rot[2]) == 100);
    r = readout(turned({1, 0, 0}, -179.f));
    CHECK(r.rot[0] == -99);
}
