#include "penconversion.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr uint16_t kUnknownRotation = 0xFFFF;
constexpr uint8_t kUnknownTilt = 0xFF;
}

PenPolarTilt PenConversion::toPolarTilt(int tiltX, int tiltY, bool valid)
{
    if (!valid) {
        return {kUnknownRotation, kUnknownTilt, false};
    }

    // Apollo reconstructs X/Y tilt as:
    // tan(x) = sin(-rotation) * tan(tilt)
    // tan(y) = cos(-rotation) * tan(tilt)
    // This is the corresponding inverse transform.
    const double x = std::tan(tiltX * kPi / 180.0);
    const double y = std::tan(tiltY * kPi / 180.0);
    const double magnitude = std::atan(std::sqrt(x * x + y * y));
    double rotation = std::atan2(-x, y) * 180.0 / kPi;
    if (rotation < 0.0) {
        rotation += 360.0;
    }

    return {
        static_cast<uint16_t>(std::lround(rotation) % 360),
        static_cast<uint8_t>(std::clamp(std::lround(magnitude * 180.0 / kPi), 0l, 90l)),
        true,
    };
}
