#include "../doctest_main.h"

#include <vector>

#include "kd/blade_geometry.h"
#include "kd/strand_map.h"

using namespace kd;

namespace {

DongProfile fourLinesOneStrand() {
    DongProfile p;
    p.bladeLength = 1.f;
    p.hiltToFirstPixel = 0.2f;
    p.lineCount = 4;
    p.pixelsPerLine = 5;
    p.strandCount = 1;
    p.strands[0].segmentCount = 4;
    p.strands[0].segments[0] = {0, false, 0};
    p.strands[0].segments[1] = {1, true, 2};
    p.strands[0].segments[2] = {2, false, 0};
    p.strands[0].segments[3] = {3, true, 2};
    return p;
}

std::vector<Rgb8> numberedLines(const DongProfile& p) {
    std::vector<Rgb8> lines(p.lineCount * p.pixelsPerLine);
    for (size_t l = 0; l < p.lineCount; ++l)
        for (size_t i = 0; i < p.pixelsPerLine; ++i)
            lines[l * p.pixelsPerLine + i] = {uint8_t(l + 1), uint8_t(i), 0};
    return lines;
}

}  // namespace

TEST_CASE("Pixels are spread from the first Pixel to the tip") {
    DongProfile p = fourLinesOneStrand();
    BladeGeometry g(p);
    CHECK(g.pixelDistance(0) == doctest::Approx(0.2f));
    CHECK(g.pixelDistance(4) == doctest::Approx(1.2f));
    CHECK(g.pixelDistance(2) == doctest::Approx(0.7f));
}

TEST_CASE("Lines face four sides and spread to the Girth") {
    DongProfile p = fourLinesOneStrand();
    BladeGeometry g(p);
    Vec3 a = g.bodyPoint(0, 1.f, 0.3f);
    Vec3 c = g.bodyPoint(2, 1.f, 0.3f);
    CHECK((a - c).norm() == doctest::Approx(0.3f));
    CHECK(g.lineNormal(0).dot(g.lineNormal(1)) == doctest::Approx(0.f).epsilon(1e-6));
    CHECK((g.bodyPoint(1, 1.f, 0.f) - g.bodyPoint(3, 1.f, 0.f)).norm() == doctest::Approx(0.f));
}

TEST_CASE("field points follow orientation and Push") {
    DongProfile p = fourLinesOneStrand();
    BladeGeometry g(p);
    Pose up{Quat::fromAxisAngle({0, 1, 0}, -kPi / 2), {0.1f, 0, 0}};  // Blade pointing straight up
    Vec3 tip = g.fieldPoint(up, 0, 1.2f, 0.f);
    CHECK(tip.x == doctest::Approx(0.1f));
    CHECK(tip.z == doctest::Approx(1.2f));
}

TEST_CASE("a folded Strand lights every Line, in the right direction, skipping fold Pixels") {
    DongProfile p = fourLinesOneStrand();
    StrandMap map(p);
    REQUIRE(map.error() == nullptr);
    REQUIRE(map.strandLength(0) == 4 * 5 + 2 + 2);
    auto lines = numberedLines(p);
    std::vector<Rgb8> strand(map.strandLength(0), Rgb8{9, 9, 9});
    Rgb8* strands[] = {strand.data()};
    map.scatter(lines.data(), strands);

    // Line 0 Hilt->tip, then 2 dark fold Pixels, then Line 1 tip->Hilt.
    CHECK(strand[0] == Rgb8{1, 0, 0});
    CHECK(strand[4] == Rgb8{1, 4, 0});
    CHECK(strand[5] == Rgb8{});
    CHECK(strand[6] == Rgb8{});
    CHECK(strand[7] == Rgb8{2, 4, 0});
    CHECK(strand[11] == Rgb8{2, 0, 0});
    CHECK(strand[12] == Rgb8{3, 0, 0});
    CHECK(strand.back() == Rgb8{4, 0, 0});
}

TEST_CASE("two Strands, each folded into two Lines, like the LightSwarm Dong") {
    DongProfile p = fourLinesOneStrand();
    p.strandCount = 2;
    p.strands[0].segmentCount = 2;
    p.strands[0].segments[0] = {0, true, 0};
    p.strands[0].segments[1] = {1, false, 0};
    p.strands[1].segmentCount = 2;
    p.strands[1].segments[0] = {2, true, 0};
    p.strands[1].segments[1] = {3, false, 0};
    StrandMap map(p);
    REQUIRE(map.error() == nullptr);
    CHECK(map.physicalPixelCount() == 20);
    auto lines = numberedLines(p);
    std::vector<Rgb8> s0(map.strandLength(0)), s1(map.strandLength(1));
    Rgb8* strands[] = {s0.data(), s1.data()};
    map.scatter(lines.data(), strands);
    CHECK(s0[0] == Rgb8{1, 4, 0});
    CHECK(s0[5] == Rgb8{2, 0, 0});
    CHECK(s1[0] == Rgb8{3, 4, 0});
    CHECK(s1[9] == Rgb8{4, 4, 0});
}

TEST_CASE("Strand layouts that leave a Line dark are rejected") {
    DongProfile p = fourLinesOneStrand();
    p.strands[0].segmentCount = 3;
    CHECK(StrandMap(p).error() != nullptr);
    p = fourLinesOneStrand();
    p.strands[0].segments[3].line = 7;
    CHECK(StrandMap(p).error() != nullptr);
}
