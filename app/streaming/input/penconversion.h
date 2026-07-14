#pragma once

#include <cstdint>

struct PenPolarTilt
{
    uint16_t rotation;
    uint8_t tilt;
    bool valid;
};

class PenConversion
{
public:
    static PenPolarTilt toPolarTilt(int tiltX, int tiltY, bool valid);
};
