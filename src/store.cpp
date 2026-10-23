#include "store.h"

#include <Preferences.h>

namespace store {

namespace {
Preferences prefs;
constexpr uint8_t kVersion = 1;
}  // namespace

void begin() { prefs.begin("kalman-dong", false); }

kd::Settings loadSettings() {
    kd::Settings s;
    if (prefs.getUChar("ver", 0) == kVersion) {
        s.palette = prefs.getUChar("palette", s.palette);
        s.brightness = prefs.getUChar("bright", s.brightness);
        s.girth = prefs.getUChar("girth", s.girth);
        s.clearSky = prefs.getUChar("clearsky", s.clearSky);
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

}  // namespace store
