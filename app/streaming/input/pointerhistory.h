#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace PointerHistory {

constexpr std::size_t BufferCapacity = 128;
constexpr std::size_t SendCapacity = 32;

struct SampleState
{
    uint32_t pointerFlags = 0;
    uint32_t penFlags = 0;
    uint32_t penMask = 0;
    uint32_t pressure = 0;
};

struct Selection
{
    std::array<std::size_t, SendCapacity> indices{};
    std::size_t count = 0;
};

// Input samples are ordered oldest to newest. State changes are always kept;
// remaining capacity is filled with evenly spaced positional samples.
Selection select(const SampleState* samples, std::size_t count);

}
