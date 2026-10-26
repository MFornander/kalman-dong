#include "../doctest_main.h"

#include <cmath>

#include "../sim_imu.h"
#include "kd/motion_tracker.h"

using namespace kd;
using sim::angleBetween;
using sim::bladeHeading;
using sim::wrapAngle;

namespace {

// Blade tilted 30 degrees up from horizontal, heading 40 degrees.
Quat heldPose() {
    return Quat::fromAxisAngle({0, 0, 1}, degToRad(40)) * Quat::fromAxisAngle({0, 1, 0}, degToRad(-30));
}

// Tilt error: angle between true and estimated Blade axes after removing
// heading from both.
float tiltErrorDeg(const Quat& estimate, const Quat& truth) {
    Vec3 a = estimate.rotate({1, 0, 0}), b = truth.rotate({1, 0, 0});
    Vec3 c = estimate.rotate({0, 1, 0}), d = truth.rotate({0, 1, 0});
    return radToDeg(std::fmax(std::fabs(std::asin(a.z) - std::asin(b.z)), std::fabs(std::asin(c.z) - std::asin(d.z))));
}

template <typename Filter>
struct Rig {
    sim::Imu imu;
    Filter filter;
    MotionTracker tracker{filter};

    Rig() {
        imu.truth = heldPose();
        imu.gyroBias = {0.02f, -0.015f, 0.03f};
        // Matches a real LSM6DS3TR-C lying on a desk (test/data/bench-desk-20s):
        // ~0.03 rad/s and ~0.05 m/s^2 standard deviation, sample to sample.
        imu.gyroNoise = 0.05f;
        imu.accelNoise = 0.08f;
    }
};

}  // namespace

// Every motion test runs against every orientation filter in the FilterBank
// (kd/filter_bank.h); a new filter is listed here too.
#define FILTERS MahonyFilter, KalmanFilter

TEST_CASE_TEMPLATE("Ignition while held still learns gravity and gyro bias in about a second", F, FILTERS) {
    Rig<F> r;
    r.tracker.begin({});
    float lastProgress = 0.f;
    bool monotonic = true;
    double doneAt = -1;
    for (int i = 0; i < 2 * 833 && doneAt < 0; ++i) {
        r.tracker.update(r.imu.step());
        monotonic &= r.tracker.ignitionProgress() >= lastProgress;
        lastProgress = r.tracker.ignitionProgress();
        if (r.tracker.ignited()) doneAt = r.imu.time;
    }
    CHECK(monotonic);
    REQUIRE(doneAt > 0);
    CHECK(doneAt < 1.3);
    CHECK((r.tracker.gyroBias() - r.imu.gyroBias).norm() < 0.003f);
    CHECK(r.tracker.biasGeneration() == 1u);
    CHECK(tiltErrorDeg(r.tracker.pose().orientation, r.imu.truth) < 1.f);
    // Starts facing the reference heading.
    CHECK(std::fabs(bladeHeading(r.tracker.pose().orientation)) < degToRad(1));
}

TEST_CASE_TEMPLATE("Ignition while moving still finishes within two seconds, using the saved bias", F, FILTERS) {
    Rig<F> r;
    r.imu.omega = [](double t) { return Vec3{0.5f * float(std::sin(3 * t)), 0.8f, 0.3f}; };
    Vec3 saved{0.019f, -0.014f, 0.031f};
    r.tracker.begin(saved);
    r.imu.run(r.tracker, 2.01);
    CHECK(r.tracker.ignited());
    CHECK(r.tracker.ignitionProgress() == doctest::Approx(1.f));
    CHECK((r.tracker.gyroBias() - saved).norm() < 1e-6f);
    // Tilt converges once it calms down.
    r.imu.omega = [](double) { return Vec3{}; };
    r.imu.run(r.tracker, 5.0);
    CHECK(tiltErrorDeg(r.tracker.pose().orientation, r.imu.truth) < 2.f);
}

TEST_CASE_TEMPLATE("tilt stays locked to gravity through minutes of waving around", F, FILTERS) {
    Rig<F> r;
    r.tracker.begin({});
    r.imu.run(r.tracker, 1.5);
    r.imu.omega = [](double t) {
        return Vec3{2.f * float(std::sin(1.3 * t)), 1.5f * float(std::sin(0.7 * t + 1)), 3.f * float(std::cos(0.9 * t))};
    };
    float worst = 0.f;
    for (int s = 0; s < 120; ++s) {
        r.imu.run(r.tracker, 1.0);
        worst = std::fmax(worst, tiltErrorDeg(r.tracker.pose().orientation, r.imu.truth));
    }
    CHECK(worst < 3.f);
}

TEST_CASE_TEMPLATE("tilt stays locked when the gyro bias shifts mid-swing", F, FILTERS) {
    Rig<F> r;
    r.tracker.begin({});
    r.imu.run(r.tracker, 1.5);
    // Warming up while being waved around: no Rest to re-learn the bias, so
    // only gravity can hold tilt. Uncorrected, this drifts ~70 degrees.
    r.imu.gyroBias += Vec3{0.02f, -0.02f, 0.f};
    r.imu.omega = [](double t) {
        return Vec3{1.5f * float(std::sin(1.1 * t)), 1.2f * float(std::sin(0.8 * t + 2)), 2.f * float(std::cos(0.6 * t))};
    };
    float worst = 0.f;
    for (int s = 0; s < 60; ++s) {
        r.imu.run(r.tracker, 1.0);
        if (s >= 5) worst = std::fmax(worst, tiltErrorDeg(r.tracker.pose().orientation, r.imu.truth));
    }
    CHECK(worst < 3.f);
}

TEST_CASE_TEMPLATE("heading drift from a changed gyro bias is re-learned at Rest", F, FILTERS) {
    Rig<F> r;
    r.tracker.begin({});
    r.imu.run(r.tracker, 1.5);
    float h0 = bladeHeading(r.tracker.pose().orientation) - bladeHeading(r.imu.truth);
    // The sensor warms up: bias shifts by ~0.6 deg/s, enough to spin the
    // clouds 34 degrees a minute if nothing corrected it.
    r.imu.gyroBias += Vec3{0, 0, 0.01f};
    r.imu.run(r.tracker, 60.0);
    float h1 = bladeHeading(r.tracker.pose().orientation) - bladeHeading(r.imu.truth);
    CHECK(radToDeg(std::fabs(wrapAngle(h1 - h0))) < 10.f);
    CHECK(r.tracker.atRest());
    CHECK(r.tracker.restSeconds() > 55.f);
    CHECK(r.tracker.biasGeneration() >= 2u);
}

TEST_CASE_TEMPLATE("a slow deliberate turn is followed, not mistaken for drift", F, FILTERS) {
    Rig<F> r;
    r.tracker.begin({});
    r.imu.run(r.tracker, 1.5);
    float h0 = bladeHeading(r.tracker.pose().orientation) - bladeHeading(r.imu.truth);
    // Turn in place about the world vertical at ~6 deg/s for 15 s.
    r.imu.omega = [&r](double) { return r.imu.truth.conj().rotate(Vec3{0, 0, 0.1f}); };
    r.imu.run(r.tracker, 15.0);
    float h1 = bladeHeading(r.tracker.pose().orientation) - bladeHeading(r.imu.truth);
    CHECK(radToDeg(std::fabs(wrapAngle(h1 - h0))) < 5.f);
}

TEST_CASE_TEMPLATE("Re-level makes the current heading the reference without touching tilt", F, FILTERS) {
    Rig<F> r;
    r.tracker.begin({});
    r.imu.run(r.tracker, 1.5);
    r.imu.omega = [&r](double) { return r.imu.truth.conj().rotate(Vec3{0, 0, 1.0f}); };
    r.imu.run(r.tracker, 1.2);  // turn ~69 degrees
    r.imu.omega = [](double) { return Vec3{}; };
    r.imu.run(r.tracker, 0.2);
    CHECK(radToDeg(std::fabs(bladeHeading(r.tracker.pose().orientation))) > 45.f);
    r.tracker.relevel();
    CHECK(radToDeg(std::fabs(bladeHeading(r.tracker.pose().orientation))) < 1.f);
    CHECK(tiltErrorDeg(r.tracker.pose().orientation, r.imu.truth) < 1.f);
}

TEST_CASE_TEMPLATE("prediction looks ahead along the current rotation", F, FILTERS) {
    Rig<F> r;
    r.imu.gyroNoise = 0.f;
    r.tracker.begin({});
    r.imu.run(r.tracker, 1.5);
    r.imu.omega = [](double) { return Vec3{0, 0, 6.f}; };  // fast twist
    r.imu.run(r.tracker, 0.2);
    Pose now = r.tracker.pose();
    Pose ahead = r.tracker.pose(0.01f);
    CHECK(radToDeg(now.orientation.angleTo(ahead.orientation)) == doctest::Approx(radToDeg(0.06f)).epsilon(0.05));
    CHECK(r.tracker.pose(0.f).orientation.angleTo(now.orientation) < 1e-6f);
}

TEST_CASE_TEMPLATE("a forward thrust Pushes the clouds, then they settle back", F, FILTERS) {
    Rig<F> r;
    r.tracker.begin({});
    r.imu.run(r.tracker, 1.5);
    // Thrust 0.15 s accelerating along world +Y, then 0.15 s braking.
    double t0 = r.imu.time;
    r.imu.linear = [t0](double t) {
        double dt = t - t0;
        float a = dt < 0.15 ? 15.f : (dt < 0.3 ? -15.f : 0.f);
        return Vec3{0, a, 0};
    };
    float peak = 0.f;
    for (int i = 0; i < 400; ++i) {
        r.tracker.update(r.imu.step());
        peak = std::fmax(peak, r.tracker.pose().push.norm());
    }
    CHECK(peak > 0.08f);
    CHECK(peak <= 0.3f + 1e-4f);
    r.imu.run(r.tracker, 2.0);
    CHECK(r.tracker.pose().push.norm() < 0.02f);
}

TEST_CASE_TEMPLATE("held still, nothing is Pushed", F, FILTERS) {
    Rig<F> r;
    r.tracker.begin({});
    r.imu.run(r.tracker, 10.0);
    CHECK(r.tracker.pose().push.norm() < 0.005f);
}

TEST_CASE_TEMPLATE("violent swings disturb tilt only briefly", F, FILTERS) {
    Rig<F> r;
    r.tracker.begin({});
    r.imu.run(r.tracker, 1.5);
    // Swinging in big arcs: high rates plus strong swing forces at the Hilt.
    r.imu.omega = [](double t) { return Vec3{0, 8.f * float(std::sin(4 * t)), 2.f * float(std::cos(4 * t))}; };
    r.imu.linear = [](double t) { return Vec3{30.f * float(std::sin(8 * t)), 0, 20.f * float(std::cos(8 * t))}; };
    float during = 0.f;
    for (int i = 0; i < 500; ++i) {
        r.imu.run(r.tracker, 0.01);
        during = std::fmax(during, tiltErrorDeg(r.tracker.pose().orientation, r.imu.truth));
    }
    CHECK(during < 3.f);
    r.imu.omega = [](double) { return Vec3{}; };
    r.imu.linear = [](double) { return Vec3{}; };
    r.imu.run(r.tracker, 4.0);
    CHECK(tiltErrorDeg(r.tracker.pose().orientation, r.imu.truth) < 1.5f);
}

namespace {

struct TiltStats {
    float rms = 0, worst = 0;
};

// Tilt error statistics for one filter over a scenario, sampled every 10 ms
// after the first `settle` seconds.
template <typename Filter, typename Setup>
TiltStats scenario(Setup setup, double seconds, double settle = 2.0) {
    Rig<Filter> r;
    r.tracker.begin({});
    r.imu.run(r.tracker, 1.5);
    setup(r.imu);
    double sum = 0;
    int n = 0;
    TiltStats out;
    for (double t = 0; t < seconds; t += 0.01) {
        r.imu.run(r.tracker, 0.01);
        if (t < settle) continue;
        float e = tiltErrorDeg(r.tracker.pose().orientation, r.imu.truth);
        sum += e * e;
        ++n;
        out.worst = std::fmax(out.worst, e);
    }
    out.rms = n ? float(std::sqrt(sum / n)) : 0.f;
    return out;
}

template <typename Setup>
void compare(const char* name, Setup setup, double seconds) {
    TiltStats m = scenario<MahonyFilter>(setup, seconds);
    TiltStats k = scenario<KalmanFilter>(setup, seconds);
    MESSAGE(name << ": tilt error deg (rms / worst)  Mahony " << m.rms << " / " << m.worst << "   Kalman " << k.rms
                 << " / " << k.worst);
}

}  // namespace

TEST_CASE("filter comparison report") {
    compare("held still", [](sim::Imu&) {}, 30);
    compare("slow waving", [](sim::Imu& imu) {
        imu.omega = [](double t) { return Vec3{0.8f * float(std::sin(0.9 * t)), 0.6f * float(std::cos(0.7 * t)), 0.5f}; };
    }, 60);
    compare("waving + bias shift", [](sim::Imu& imu) {
        imu.gyroBias += Vec3{0.02f, -0.02f, 0.f};
        imu.omega = [](double t) {
            return Vec3{1.5f * float(std::sin(1.1 * t)), 1.2f * float(std::sin(0.8 * t + 2)), 2.f * float(std::cos(0.6 * t))};
        };
    }, 60);
    compare("big swings with swing forces", [](sim::Imu& imu) {
        imu.omega = [](double t) { return Vec3{0, 8.f * float(std::sin(4 * t)), 2.f * float(std::cos(4 * t))}; };
        imu.linear = [](double t) { return Vec3{30.f * float(std::sin(8 * t)), 0, 20.f * float(std::cos(8 * t))}; };
    }, 20);
    compare("hand jitter (1 m/s^2 shakes)", [](sim::Imu& imu) {
        imu.linear = [](double t) { return Vec3{float(std::sin(9 * t)), float(std::cos(7 * t)), 0.5f * float(std::sin(11 * t))}; };
    }, 30);
    CHECK(true);
}
