#include "../doctest_main.h"

#include <vector>

#include "kd/sk9822.h"

using namespace kd;

namespace {

std::vector<uint8_t> encode(const std::vector<Rgb8>& px, const char* order) {
    WireOrder o;
    REQUIRE(WireOrder::parse(order, o));
    std::vector<uint8_t> out(sk9822FrameBytes(px.size()), 0xAA);
    sk9822Encode(px.data(), px.size(), o, out.data());
    return out;
}

}  // namespace

TEST_CASE("a frame is start frame, 4 bytes per Pixel, then enough clocks for every Pixel") {
    for (size_t n : {size_t(0), size_t(1), size_t(16), size_t(17), size_t(240), size_t(480)}) {
        CAPTURE(n);
        size_t bytes = sk9822FrameBytes(n);
        size_t trailingBits = (bytes - 4 - 4 * n) * 8;
        CHECK(trailingBits >= n / 2 + 32);       // data reaches the last Pixel, plus SK9822 reset
        CHECK(trailingBits <= n / 2 + 32 + 8 * 2);  // without wasting much time
    }
}

TEST_CASE("Pixels go out at full global brightness in wire order") {
    auto bgr = encode({{10, 20, 30}, {1, 2, 3}}, "BGR");
    CHECK(bgr[0] == 0);
    CHECK(bgr[3] == 0);
    CHECK(bgr[4] == 0xFF);
    CHECK(bgr[5] == 30);
    CHECK(bgr[6] == 20);
    CHECK(bgr[7] == 10);
    CHECK(bgr[8] == 0xFF);
    CHECK(bgr[9] == 3);

    auto rgb = encode({{10, 20, 30}}, "RGB");
    CHECK(rgb[5] == 10);
    CHECK(rgb[6] == 20);
    CHECK(rgb[7] == 30);
}

TEST_CASE("start and end frames are zero, whatever was in the buffer") {
    auto f = encode({{255, 255, 255}, {255, 255, 255}, {255, 255, 255}}, "BGR");
    for (size_t i = 0; i < 4; ++i) CHECK(f[i] == 0);
    for (size_t i = 4 + 12; i < f.size(); ++i) CHECK(f[i] == 0);
}

TEST_CASE("only arrangements of R, G and B are accepted as wire orders") {
    WireOrder o;
    for (const char* good : {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR"}) CHECK(WireOrder::parse(good, o));
    for (const char* bad : {"RRG", "BG", "BGRX", "rgb", "", "XYZ"}) CHECK_FALSE(WireOrder::parse(bad, o));
    CHECK_FALSE(WireOrder::parse(nullptr, o));
}
