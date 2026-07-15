#pragma once

#include <QRect>
#include <QVector>

#include <array>

namespace Overlay {

enum class QuickKeyPanelSide {
    Invalid,
    Left,
    Right,
};

struct QuickKeyOverlayLayout
{
    static constexpr int BindingCount = 10;
    static constexpr int BindingsPerSide = 5;

    float scale = 1.0f;
    int panelWidth = 0;
    int panelHeight = 0;
    int shadowSize = 0;
    int padding = 0;
    int headerHeight = 0;
    int footerHeight = 0;
    int sectionHeight = 0;
    bool compact = false;
    bool showActionDetails = true;

    QRect leftPanel;
    QRect presetPanel;
    QRect rightPanel;
    QRect leftModeSwitch;
    QRect rightModeSwitch;
    std::array<QRect, BindingCount> bindingRows;
    QVector<QRect> profileRows;

    static QuickKeyOverlayLayout calculate(int viewportWidth, int viewportHeight,
                                            int visibleProfileCount);
    static QuickKeyPanelSide panelForSlot(int slot);
    static int visualRowForSlot(int slot);
    static std::array<int, BindingCount> bindingIndexesBySlot(const QVector<int>& slotNumbers);
};

}
