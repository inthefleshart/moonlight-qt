#pragma once

struct NormalizedInputPoint
{
    float x;
    float y;
    bool inside;
};

class InputGeometry
{
public:
    static NormalizedInputPoint mapClientPoint(int clientX, int clientY,
                                               int clientWidth, int clientHeight,
                                               int streamWidth, int streamHeight,
                                               bool clampToVideoRegion);
};
