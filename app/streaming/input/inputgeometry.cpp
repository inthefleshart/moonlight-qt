#include "inputgeometry.h"

#include <QtGlobal>

#include <cmath>

NormalizedInputPoint InputGeometry::mapClientPoint(int clientX, int clientY,
                                                   int clientWidth, int clientHeight,
                                                   int streamWidth, int streamHeight,
                                                   bool clampToVideoRegion)
{
    if (clientWidth <= 0 || clientHeight <= 0 || streamWidth <= 0 || streamHeight <= 0) {
        return {0.0f, 0.0f, false};
    }

    int videoX = 0;
    int videoY = 0;
    int videoWidth = clientWidth;
    int videoHeight = static_cast<int>(std::ceil(
        static_cast<float>(clientWidth) * streamHeight / streamWidth));
    if (videoHeight > clientHeight) {
        videoHeight = clientHeight;
        videoWidth = static_cast<int>(std::ceil(
            static_cast<float>(clientHeight) * streamWidth / streamHeight));
        videoX = (clientWidth - videoWidth) / 2;
    } else {
        videoY = (clientHeight - videoHeight) / 2;
    }

    const bool inside = clientX >= videoX && clientX < videoX + videoWidth &&
                        clientY >= videoY && clientY < videoY + videoHeight;

    if (!inside && !clampToVideoRegion) {
        return {0.0f, 0.0f, false};
    }

    const int mappedX = qBound(videoX, clientX, videoX + videoWidth);
    const int mappedY = qBound(videoY, clientY, videoY + videoHeight);
    return {
        qBound(0.0f, static_cast<float>(mappedX - videoX) / videoWidth, 1.0f),
        qBound(0.0f, static_cast<float>(mappedY - videoY) / videoHeight, 1.0f),
        inside,
    };
}
