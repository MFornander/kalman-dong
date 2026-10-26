#include "filter_bank.h"

#include <cstring>

namespace kd {

OrientationFilter& FilterBank::byName(const char* name) {
    for (OrientationFilter* f : filters_)
        if (name && std::strcmp(f->name(), name) == 0) return *f;
    for (OrientationFilter* f : filters_)
        if (std::strcmp(f->name(), kDefaultFilter) == 0) return *f;
    return *filters_[0];
}

OrientationFilter& FilterBank::next(const OrientationFilter& current) {
    for (size_t i = 0; i < kCount; ++i)
        if (filters_[i] == &current) return *filters_[(i + 1) % kCount];
    return defaultFilter();
}

}  // namespace kd
