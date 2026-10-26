#include "store.h"

#include <Preferences.h>

namespace store {

namespace {
Preferences prefs;
constexpr uint8_t kVersion = 2;  // 2: Girth steps shrank; older saved settings reset to defaults
}  // namespace

void begin() { prefs.begin("kalman-dong", false); }

kd::Settings loadSettings() {
    kd::Settings s;
    if (prefs.getUChar("ver", 0) == kVersion) {
        s.palette = prefs.getUChar("palette", s.palette);
        s.brightness = prefs.getUChar("bright", s.brightness);
        s.girth = prefs.getUChar("girth", s.girth);
        s.clearSky = prefs.getUChar("clearsky", s.clearSky);
        s.motion = prefs.getUChar("motion", s.motion);
    }
    s.sanitize();
    return s;
}

void saveSettings(const kd::Settings& s) {
    prefs.putUChar("ver", kVersion);
    prefs.putUChar("palette", s.palette);
    prefs.putUChar("bright", s.brightness);
    prefs.putUChar("girth", s.girth);
    prefs.putUChar("clearsky", s.clearSky);
    prefs.putUChar("motion", s.motion);
}

kd::Vec3 loadGyroBias() {
    float b[3] = {0, 0, 0};
    if (prefs.getBytesLength("bias") == sizeof(b)) prefs.getBytes("bias", b, sizeof(b));
    for (float v : b)
        if (!(v > -0.5f && v < 0.5f)) return {};  // garbage or NaN
    return {b[0], b[1], b[2]};
}

void saveGyroBias(const kd::Vec3& bias) {
    float b[3] = {bias.x, bias.y, bias.z};
    prefs.putBytes("bias", b, sizeof(b));
}

bool loadMounting(kd::Quat& sensorToBody) {
    float q[4];
    if (prefs.getBytesLength("mounting") != sizeof(q)) return false;
    prefs.getBytes("mounting", q, sizeof(q));
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!(n > 0.9f && n < 1.1f)) return false;  // garbage or NaN
    sensorToBody = kd::Quat{q[0], q[1], q[2], q[3]}.normalized();
    return true;
}

void saveMounting(const kd::Quat& sensorToBody) {
    float q[4] = {sensorToBody.w, sensorToBody.x, sensorToBody.y, sensorToBody.z};
    prefs.putBytes("mounting", q, sizeof(q));
}

}  // namespace store
