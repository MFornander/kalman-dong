// Driver for the M5Stack 8-Encoder unit (U153) on the Grove port, which
// shares its I2C bus with the IMU. An STM32 on the unit counts the knobs'
// turns, so nothing is missed between reads. Its LEDs are left off.
//
// Runs fine from the Grove port's 3.3 V: with the LEDs off the unit's own
// 3.3 V regulator loses only a few tens of mV.
#pragma once

#include <Wire.h>

#include "kd/knob_panel.h"

class Encoder8 {
public:
    static constexpr uint8_t kAddress = 0x41;

    // Looks for the unit; true if it answered.
    bool begin(TwoWire& wire);
    uint8_t firmware() const { return firmware_; }

    // Reads the pushes and the counts of the knobs that moved, and every few
    // calls the switch. False if the unit didn't answer; `out` is then unchanged.
    bool read(kd::KnobReading& out);

private:
    bool readRegs(uint8_t reg, uint8_t* buf, size_t len);
    bool readCount(int knob, int32_t& count);

    TwoWire* wire_ = nullptr;
    uint8_t firmware_ = 0;
    kd::KnobReading last_;
    bool haveCounts_ = false;
    unsigned calls_ = 0;
    int nextKnob_ = 0;  // firmware without change flags: knobs read in turn
};
