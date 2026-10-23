#include "strand_map.h"

namespace kd {

StrandMap::StrandMap(const DongProfile& profile) : profile_(profile) {
    if (profile.lineCount < 1 || profile.lineCount > kMaxLines) {
        error_ = "lineCount must be 1..4";
        return;
    }
    if (profile.strandCount < 1 || profile.strandCount > kMaxStrands) {
        error_ = "strandCount must be 1..2";
        return;
    }
    if (profile.pixelsPerLine < 1) {
        error_ = "pixelsPerLine must be at least 1";
        return;
    }
    bool shown[kMaxLines] = {};
    for (size_t s = 0; s < profile.strandCount; ++s) {
        const StrandLayout& layout = profile.strands[s];
        if (layout.segmentCount < 1 || layout.segmentCount > kMaxSegmentsPerStrand) {
            error_ = "each Strand needs 1..4 segments";
            return;
        }
        for (size_t i = 0; i < layout.segmentCount; ++i) {
            if (layout.segments[i].line >= profile.lineCount) {
                error_ = "segment refers to a Line that does not exist";
                return;
            }
            shown[layout.segments[i].line] = true;
        }
    }
    for (size_t l = 0; l < profile.lineCount; ++l) {
        if (!shown[l]) {
            error_ = "a Line is not on any Strand";
            return;
        }
    }
}

size_t StrandMap::physicalPixelCount() const {
    size_t n = 0;
    for (size_t s = 0; s < profile_.strandCount; ++s) n += strandLength(s);
    return n;
}

void StrandMap::scatter(const Rgb8* lines, Rgb8* const* strands) const {
    if (error_) return;
    const size_t ppl = profile_.pixelsPerLine;
    for (size_t s = 0; s < profile_.strandCount; ++s) {
        Rgb8* out = strands[s];
        const StrandLayout& layout = profile_.strands[s];
        for (size_t i = 0; i < layout.segmentCount; ++i) {
            const StrandSegment& seg = layout.segments[i];
            for (size_t k = 0; k < seg.skipBefore; ++k) *out++ = Rgb8{};
            const Rgb8* line = lines + seg.line * ppl;
            for (size_t k = 0; k < ppl; ++k) *out++ = line[seg.fromTip ? ppl - 1 - k : k];
        }
    }
}

}  // namespace kd
