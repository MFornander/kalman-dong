#include "noise.h"

#include <cmath>
#include <cstdint>

namespace kd {

namespace {

// Ken Perlin's reference permutation, repeated so p[i + 256] needs no mask.
const uint8_t kPerm[256] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103,
    30,  69,  142, 8,   99,  37,  240, 21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,
    197, 62,  94,  252, 219, 203, 117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174,
    20,  125, 136, 171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146, 158, 231,
    83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,  46,  245, 40,  244, 102, 143,
    54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,  18,  169, 200, 196,
    135, 130, 116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124,
    123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,  58,  17,
    182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101,
    155, 167, 43,  172, 9,   129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185,
    112, 104, 218, 246, 97,  228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,
    51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181, 199, 106, 157, 184, 84,  204, 176,
    115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,  222, 114, 67,  29,  24,  72,  243,
    141, 128, 195, 78,  66,  215, 61,  156, 180};

inline int perm(int i) { return kPerm[i & 255]; }

inline float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

inline float grad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

inline float lerp(float a, float b, float t) { return a + t * (b - a); }

}  // namespace

float noise3(float x, float y, float z) {
    float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    int X = static_cast<int>(fx) & 255, Y = static_cast<int>(fy) & 255, Z = static_cast<int>(fz) & 255;
    x -= fx;
    y -= fy;
    z -= fz;
    float u = fade(x), v = fade(y), w = fade(z);

    int A = perm(X) + Y, AA = perm(A) + Z, AB = perm(A + 1) + Z;
    int B = perm(X + 1) + Y, BA = perm(B) + Z, BB = perm(B + 1) + Z;

    float r = lerp(lerp(lerp(grad(perm(AA), x, y, z), grad(perm(BA), x - 1, y, z), u),
                        lerp(grad(perm(AB), x, y - 1, z), grad(perm(BB), x - 1, y - 1, z), u), v),
                   lerp(lerp(grad(perm(AA + 1), x, y, z - 1), grad(perm(BA + 1), x - 1, y, z - 1), u),
                        lerp(grad(perm(AB + 1), x, y - 1, z - 1), grad(perm(BB + 1), x - 1, y - 1, z - 1), u),
                        v),
                   w);
    // Improved noise peaks a little above 1; keep within [-1, 1].
    return r > 1.f ? 1.f : (r < -1.f ? -1.f : r);
}

float fbm3(float x, float y, float z, int octaves) {
    float sum = 0.f, amp = 1.f, total = 0.f;
    for (int i = 0; i < octaves; ++i) {
        // Offset each octave so lattice zeros don't line up.
        sum += amp * noise3(x + i * 31.7f, y + i * 17.3f, z + i * 47.1f);
        total += amp;
        x *= 2.f;
        y *= 2.f;
        z *= 2.f;
        amp *= 0.5f;
    }
    return total > 0.f ? sum / total : 0.f;
}

}  // namespace kd
