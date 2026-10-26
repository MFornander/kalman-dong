// Driver for the carrier board's ST LSM6DS3-family IMU (LSM6DS3, LSM6DS3TR-C
// or LSM6DSO) on I2C. Only registers that are identical across the three are
// used: 833 Hz, +-2000 dps, +-16 g, data-ready on INT1.
#pragma once

#include <Wire.h>

#include "kd/kd_math.h"

class ImuLsm6 {
public:
    struct Reading {
        kd::Vec3 gyro;   // rad/s, sensor axes
        kd::Vec3 accel;  // m/s^2, sensor axes
    };

    static constexpr float kRateHz = 833.f;

    // Probes 0x6A then 0x6B. Returns false if no supported chip answers.
    bool begin(TwoWire& wire);
    const char* chipName() const { return chip_; }

    bool dataReady();
    bool read(Reading& out);

private:
    bool writeReg(uint8_t reg, uint8_t value);
    bool readRegs(uint8_t reg, uint8_t* buf, size_t len);

    TwoWire* wire_ = nullptr;
    uint8_t addr_ = 0;
    const char* chip_ = "none";
};
