#include "pointerhistory.h"

#include <algorithm>

namespace PointerHistory {

namespace {
bool stateChanged(const SampleState& a, const SampleState& b)
{
    return a.pointerFlags != b.pointerFlags ||
           a.penFlags != b.penFlags ||
           a.penMask != b.penMask ||
           a.pressure != b.pressure;
}
}

Selection select(const SampleState* samples, std::size_t count)
{
    Selection result;
    if (!samples || count == 0) {
        return result;
    }

    if (count <= SendCapacity) {
        for (std::size_t i = 0; i < count; ++i) {
            result.indices[result.count++] = i;
        }
        return result;
    }

    std::array<bool, BufferCapacity> keep{};
    const std::size_t boundedCount = std::min(count, BufferCapacity);
    keep[0] = true;
    keep[boundedCount - 1] = true;
    std::size_t kept = boundedCount == 1 ? 1 : 2;

    for (std::size_t i = 1; i < boundedCount && kept < SendCapacity; ++i) {
        if (stateChanged(samples[i - 1], samples[i])) {
            if (!keep[i - 1] && kept < SendCapacity) {
                keep[i - 1] = true;
                ++kept;
            }
            if (!keep[i] && kept < SendCapacity) {
                keep[i] = true;
                ++kept;
            }
        }
    }

    const std::size_t remaining = SendCapacity - kept;
    if (remaining > 0) {
        for (std::size_t n = 1; n <= remaining; ++n) {
            const std::size_t candidate = (n * (boundedCount - 1)) / (remaining + 1);
            if (!keep[candidate]) {
                keep[candidate] = true;
                ++kept;
            }
        }
    }

    // Rounding or state-heavy input can leave gaps. Fill those deterministically.
    for (std::size_t i = 1; i + 1 < boundedCount && kept < SendCapacity; ++i) {
        if (!keep[i]) {
            keep[i] = true;
            ++kept;
        }
    }

    for (std::size_t i = 0; i < boundedCount && result.count < SendCapacity; ++i) {
        if (keep[i]) {
            result.indices[result.count++] = i;
        }
    }
    return result;
}

}
