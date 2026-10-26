// Every orientation filter the firmware carries, in one place.
//
// To add a filter:
//   1. Implement OrientationFilter (orientation_filter.h).
//   2. Add it as a member below, list it in the constructor, bump kCount.
//   3. Add its type to FILTERS in test/test_motion/test_main.cpp, so every
//      motion test and the comparison report run against it.
// It then appears in the serial 'f' cycle and the recording replay report,
// and a build can make it the default with -DKD_FILTER=Name.
#pragma once

#include <cstddef>

#include "orientation_filter.h"

namespace kd {

// The filter a build uses unless it names another.
constexpr const char* kDefaultFilter = "Mahony";

class FilterBank {
public:
    FilterBank() : filters_{&mahony_, &kalman_} {}

    size_t count() const { return kCount; }
    OrientationFilter& at(size_t i) { return *filters_[i % kCount]; }

    // The filter called `name` (case-sensitive), or the default if there is none.
    OrientationFilter& byName(const char* name);

    OrientationFilter& defaultFilter() { return byName(kDefaultFilter); }

    // The filter after `current`, wrapping around.
    OrientationFilter& next(const OrientationFilter& current);

private:
    static constexpr size_t kCount = 2;
    MahonyFilter mahony_;
    KalmanFilter kalman_;
    OrientationFilter* filters_[kCount];
};

}  // namespace kd
