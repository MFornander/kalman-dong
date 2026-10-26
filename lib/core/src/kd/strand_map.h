// Folds the Lines onto the physical Strands described by a Dong Profile.
#pragma once

#include <cstddef>

#include "color.h"
#include "dong_profile.h"

namespace kd {

class StrandMap {
public:
    explicit StrandMap(const DongProfile& profile);

    // nullptr if the profile's Strand layout is usable, else why not.
    const char* error() const { return error_; }

    size_t strandCount() const { return profile_.strandCount; }
    size_t strandLength(size_t s) const { return profile_.strandLength(s); }
    size_t physicalPixelCount() const;

    // `lines` holds lineCount * pixelsPerLine Pixels, Line after Line, each
    // counted from the Hilt outward. `strands[s]` must hold strandLength(s)
    // Pixels; skipped Pixels are set dark.
    void scatter(const Rgb8* lines, Rgb8* const* strands) const;

private:
    const DongProfile& profile_;
    const char* error_ = nullptr;
};

}  // namespace kd
