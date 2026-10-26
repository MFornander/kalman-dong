// Replays IMU recordings captured with tools/capture_imu.py through the same
// MotionTracker that runs on the Dong. With no recordings in
// test/data yet, only the parser is checked.
#include "../doctest_main.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "kd/motion_tracker.h"

using namespace kd;

namespace {

struct Row {
    uint32_t tUs;
    Vec3 gyro, accel;
};

std::vector<Row> parseRecording(std::istream& in) {
    std::vector<Row> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] < '0' || line[0] > '9') continue;  // header, comments
        Row r{};
        unsigned long t = 0;
        if (std::sscanf(line.c_str(), "%lu,%f,%f,%f,%f,%f,%f", &t, &r.gyro.x, &r.gyro.y, &r.gyro.z, &r.accel.x,
                        &r.accel.y, &r.accel.z) == 7) {
            r.tUs = static_cast<uint32_t>(t);
            rows.push_back(r);
        }
    }
    return rows;
}

struct ReplayResult {
    bool ignited = false;
    bool learnedBias = false;  // Ignition found the Dong still (didn't time out)
    double ignitedAt = 0;
    float worstRestTiltDeg = 0;  // estimated vs measured "up" while at Rest
    bool finite = true;
    float headingDriftDeg = 0;  // heading change from 1 s after Ignition to the end
    // Tilt error at quiet moments mid-motion (slow rotation, |accel| near 1 g),
    // where the accelerometer shows true down: the only ground truth there is.
    float quietTiltMeanDeg = 0;
    int quietSamples = 0;
};

ReplayResult replay(const std::vector<Row>& rows, OrientationFilter& filter) {
    MotionTracker tracker(filter);
    tracker.begin({});
    ReplayResult result;
    double t = 0;
    bool haveHeading = false;
    float heading0 = 0;
    auto heading = [](const Quat& q) {
        Vec3 f = q.rotate({1, 0, 0});
        return std::atan2(f.y, f.x);
    };
    for (size_t i = 1; i < rows.size(); ++i) {
        ImuSample s;
        s.dt = static_cast<uint32_t>(rows[i].tUs - rows[i - 1].tUs) * 1e-6f;  // wraps safely
        s.gyro = rows[i].gyro;
        s.accel = rows[i].accel;
        tracker.update(s);
        t += s.dt;
        if (tracker.ignited() && !result.ignited) {
            result.ignited = true;
            result.ignitedAt = t;
            result.learnedBias = tracker.biasGeneration() > 0;
        }
        Pose p = tracker.pose();
        result.finite &= std::isfinite(p.orientation.w) && std::isfinite(p.push.x);
        if (result.ignited && !haveHeading && t > result.ignitedAt + 1.0) {
            heading0 = heading(p.orientation);
            haveHeading = true;
        }
        if (haveHeading) {
            float d = heading(p.orientation) - heading0;
            d = std::fabs(std::remainder(d, 2.f * kPi));
            result.headingDriftDeg = radToDeg(d);
        }
        if (tracker.ignited() && !tracker.atRest() && s.gyro.norm() < 0.35f &&
            std::fabs(s.accel.norm() - kGravity) < 0.25f) {
            Vec3 upEstimate = p.orientation.conj().rotate({0, 0, 1});
            result.quietTiltMeanDeg += radToDeg(std::acos(clampf(upEstimate.dot(s.accel.normalized()), -1.f, 1.f)));
            ++result.quietSamples;
        }
        if (tracker.ignited() && tracker.restSeconds() > 1.f) {
            Vec3 upEstimate = p.orientation.conj().rotate({0, 0, 1});
            float err = radToDeg(std::acos(clampf(upEstimate.dot(s.accel.normalized()), -1.f, 1.f)));
            result.worstRestTiltDeg = std::fmax(result.worstRestTiltDeg, err);
        }
    }
    if (result.quietSamples) result.quietTiltMeanDeg /= result.quietSamples;
    return result;
}

std::filesystem::path dataDir() {
    for (const char* d : {"test/data", "../test/data", "../../test/data"})
        if (std::filesystem::is_directory(d)) return d;
    return {};
}

}  // namespace

TEST_CASE("recordings parse, skipping the header") {
    std::istringstream in(
        "# kalman-dong imu v1 profile=proto chip=LSM6DSO rate=833 frame=sensor\n"
        "t_us,gx,gy,gz,ax,ay,az\n"
        "1000,0.01000,-0.02000,0.00300,0.1000,0.2000,9.8000\n"
        "2200,0.01100,-0.02100,0.00310,0.1100,0.2100,9.7900\n"
        "# end\n");
    auto rows = parseRecording(in);
    REQUIRE(rows.size() == 2);
    CHECK(rows[1].tUs == 2200u);
    CHECK(rows[0].accel.z == doctest::Approx(9.8f));
}

TEST_CASE("every recording in test/data replays cleanly") {
    auto dir = dataDir();
    if (dir.empty()) {
        MESSAGE("no test/data recordings yet: capture some with tools/capture_imu.py");
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".csv") continue;
        std::ifstream f(entry.path());
        auto rows = parseRecording(f);
        CAPTURE(entry.path().filename().string());
        REQUIRE(rows.size() > 833);
        MahonyFilter mahony;
        KalmanFilter kalman;
        for (OrientationFilter* filter : {static_cast<OrientationFilter*>(&mahony), static_cast<OrientationFilter*>(&kalman)}) {
            ReplayResult r = replay(rows, *filter);
            CAPTURE(std::string(filter->name()));
            CHECK(r.finite);
            CHECK(r.ignited);
            CHECK(r.ignitedAt < 2.5);
            CHECK(r.worstRestTiltDeg < 3.f);
            // Recordings that start with the Dong lying still must ignite by
            // learning the gyro bias, not by timing out.
            if (entry.path().filename().string().rfind("bench-desk", 0) == 0) CHECK(r.learnedBias);
            MESSAGE(entry.path().filename().string()
                    << ": " << std::string(filter->name()) << " ignited at " << r.ignitedAt << " s, worst tilt at Rest "
                    << r.worstRestTiltDeg << " deg, heading change " << r.headingDriftDeg
                    << " deg, tilt error at quiet moments in motion " << r.quietTiltMeanDeg << " deg ("
                    << r.quietSamples << " samples)");
        }
    }
}
