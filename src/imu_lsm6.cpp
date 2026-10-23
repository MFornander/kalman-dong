#include "imu_lsm6.h"

namespace {

constexpr uint8_t kWhoAmI = 0x0F;
constexpr uint8_t kInt1Ctrl = 0x0D;
constexpr uint8_t kCtrl1Xl = 0x10;
constexpr uint8_t kCtrl2G = 0x11;
constexpr uint8_t kCtrl3C = 0x12;
constexpr uint8_t kStatus = 0x1E;
constexpr uint8_t kOutGyro = 0x22;  // gyro X,Y,Z then accel X,Y,Z, 16-bit little-endian

constexpr uint8_t kOdr833 = 0x7 << 4;
constexpr uint8_t kAccel16g = 0x1 << 2;
constexpr uint8_t kGyro2000dps = 0x3 << 2;
constexpr uint8_t kBdu = 1 << 6;
constexpr uint8_t kIfInc = 1 << 2;
constexpr uint8_t kSwReset = 1 << 0;
constexpr uint8_t kInt1DrdyG = 1 << 1;

constexpr float kGyroScale = 0.070f * kd::kPi / 180.f;  // 70 mdps/LSB -> rad/s
constexpr float kAccelScale = 0.000488f * kd::kGravity;  // 0.488 mg/LSB -> m/s^2

const char* chipFor(uint8_t id) {
    switch (id) {
        case 0x69: return "LSM6DS3";
        case 0x6A: return "LSM6DS3TR-C";
        case 0x6C: return "LSM6DSO";
        default: return nullptr;
    }
}

}  // namespace

bool ImuLsm6::writeReg(uint8_t reg, uint8_t value) {
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    wire_->write(value);
    return wire_->endTransmission() == 0;
}

bool ImuLsm6::readRegs(uint8_t reg, uint8_t* buf, size_t len) {
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0) return false;
    if (wire_->requestFrom(addr_, static_cast<uint8_t>(len)) != len) return false;
    for (size_t i = 0; i < len; ++i) buf[i] = wire_->read();
    return true;
}

bool ImuLsm6::begin(TwoWire& wire) {
    wire_ = &wire;
    for (uint8_t addr : {uint8_t(0x6A), uint8_t(0x6B)}) {
        addr_ = addr;
        uint8_t id = 0;
        if (readRegs(kWhoAmI, &id, 1) && chipFor(id)) {
            chip_ = chipFor(id);
            break;
        }
        chip_ = "none";
    }
    if (chip_[0] == 'n') return false;

    writeReg(kCtrl3C, kSwReset);
    delay(20);
    return writeReg(kCtrl3C, kBdu | kIfInc) &&
           writeReg(kCtrl1Xl, kOdr833 | kAccel16g) &&
           writeReg(kCtrl2G, kOdr833 | kGyro2000dps) &&
           writeReg(kInt1Ctrl, kInt1DrdyG);
}

bool ImuLsm6::dataReady() {
    uint8_t status = 0;
    return readRegs(kStatus, &status, 1) && (status & 0x02);
}

bool ImuLsm6::read(Reading& out) {
    uint8_t b[12];
    if (!readRegs(kOutGyro, b, sizeof(b))) return false;
    auto v = [&b](int i) { return static_cast<int16_t>(b[i] | (b[i + 1] << 8)); };
    out.gyro = {v(0) * kGyroScale, v(2) * kGyroScale, v(4) * kGyroScale};
    out.accel = {v(6) * kAccelScale, v(8) * kAccelScale, v(10) * kAccelScale};
    return true;
}
