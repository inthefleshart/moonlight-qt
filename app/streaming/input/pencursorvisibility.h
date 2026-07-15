#pragma once

namespace PenCursorVisibility {

enum Policy
{
    Automatic = 0,
    AlwaysVisible = 1,
    HideInRange = 2,
};

constexpr bool shouldHide(int policy, bool penInRange, bool penInContact)
{
    return (policy == Automatic && penInContact) ||
           (policy == HideInRange && penInRange);
}

} // namespace PenCursorVisibility
