#include "encoder8.h"

namespace {

// Registers (M5Unit-8Encoder-Internal-FW). Every register is its own
// transfer: the unit answers a read with that register only.
constexpr uint8_t kRegCount = 0x00;    // 4 bytes per knob, little-endian, signed
constexpr uint8_t kRegPushes = 0x50;   // 8 bytes, one per knob: 0 pushed, 1 not
constexpr uint8_t kRegSwitch = 0x60;   // 1 byte: 1 on
constexpr uint8_t kRegChanged = 0x61;  // 1 byte: bit i set if knob i moved; cleared by reading
constexpr uint8_t kRegPushBits = 0x62; // 1 byte: bit i clear while knob i is pushed
constexpr uint8_t kRegFirmware = 0xFE;

constexpr uint8_t kFirstFirmwareWithFlags = 2;
constexpr unsigned kSwitchEvery = 10;  // the unit debounces it during the transfer, so read it less often

}  // namespace

bool Encoder8::begin(TwoWire& wire) {
    wire_ = &wire;
    haveCounts_ = false;
    return readRegs(kRegFirmware, &firmware_, 1);
}

bool Encoder8::readRegs(uint8_t reg, uint8_t* buf, size_t len) {
    // A STOP between the register and the data, as M5's own library does: the
    // unit prepares its answer once the register write has ended.
    wire_->beginTransmission(kAddress);
    wire_->write(reg);
    if (wire_->endTransmission() != 0) return false;
    if (wire_->requestFrom(kAddress, static_cast<uint8_t>(len)) != len) return false;
    for (size_t i = 0; i < len; ++i) buf[i] = wire_->read();
    return true;
}

bool Encoder8::readCount(int knob, int32_t& count) {
    uint8_t b[4];
    if (!readRegs(kRegCount + 4 * knob, b, sizeof(b))) return false;
    count = static_cast<int32_t>(uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24);
    return true;
}

bool Encoder8::read(kd::KnobReading& out) {
    if (!wire_) return false;
    kd::KnobReading r = last_;

    const bool flags = firmware_ >= kFirstFirmwareWithFlags;
    uint8_t changed = 0xFF;  // the first read takes every knob's count
    if (haveCounts_) {
        if (flags) {
            if (!readRegs(kRegChanged, &changed, 1)) return false;
        } else {
            changed = static_cast<uint8_t>(3u << nextKnob_);  // two knobs per read
            nextKnob_ = (nextKnob_ + 2) % kd::KnobReading::kKnobs;
        }
    }
    for (int k = 0; k < kd::KnobReading::kKnobs; ++k)
        if ((changed >> k) & 1 && !readCount(k, r.counts[k])) return false;

    if (flags) {
        uint8_t bits;
        if (!readRegs(kRegPushBits, &bits, 1)) return false;
        r.pushed = static_cast<uint8_t>(~bits);
    } else {
        uint8_t levels[8];
        if (!readRegs(kRegPushes, levels, sizeof(levels))) return false;
        r.pushed = 0;
        for (int k = 0; k < 8; ++k) r.pushed |= (levels[k] == 0) << k;
    }

    if (calls_++ % kSwitchEvery == 0) {
        uint8_t sw;
        if (!readRegs(kRegSwitch, &sw, 1)) return false;
        r.switchOn = sw != 0;
    }

    r.ms = millis();
    haveCounts_ = true;
    last_ = r;
    out = r;
    return true;
}
