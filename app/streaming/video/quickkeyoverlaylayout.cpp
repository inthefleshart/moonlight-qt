#include "quickkeyoverlaylayout.h"

#include <algorithm>

using namespace Overlay;

namespace {
QRect insetBottom(const QRect& rect, int gap)
{
    return QRect(rect.x(), rect.y(), rect.width(), std::max(1, rect.height() - gap));
}
}

QuickKeyOverlayLayout QuickKeyOverlayLayout::calculate(int viewportWidth, int viewportHeight,
                                                        int visibleProfileCount)
{
    QuickKeyOverlayLayout layout;
    const int safeWidth = std::max(1, viewportWidth);
    const int safeHeight = std::max(1, viewportHeight);
    layout.scale = std::clamp(safeHeight / 1440.0f, 0.72f, 1.50f);
    layout.shadowSize = std::max(5, static_cast<int>(9 * layout.scale));

    const int horizontalMargin = std::max(8, static_cast<int>(16 * layout.scale));
    const int verticalMargin = std::max(8, static_cast<int>(14 * layout.scale));
    const int availableWidth = std::max(1, safeWidth - horizontalMargin * 2 - layout.shadowSize);
    const int availableHeight = std::max(1, safeHeight - verticalMargin * 2 - layout.shadowSize);
    layout.panelWidth = std::min(availableWidth,
                                 std::max(900, static_cast<int>(1540 * layout.scale)));
    layout.panelHeight = std::min(availableHeight,
                                  std::max(520, static_cast<int>(900 * layout.scale)));
    layout.padding = std::min(layout.panelWidth / 12,
                              std::max(6, static_cast<int>(20 * layout.scale)));
    layout.headerHeight = std::min(layout.panelHeight * 31 / 100,
                                   std::max(46, static_cast<int>(92 * layout.scale)));
    layout.footerHeight = std::min(layout.panelHeight * 20 / 100,
                                   std::max(30, static_cast<int>(54 * layout.scale)));

    const int columnGap = std::max(7, static_cast<int>(16 * layout.scale));
    const int contentTop = layout.headerHeight + std::max(8, static_cast<int>(12 * layout.scale));
    const int contentBottom = layout.panelHeight - layout.footerHeight -
                              std::max(8, static_cast<int>(12 * layout.scale));
    const int contentHeight = std::max(1, contentBottom - contentTop);
    const int columnsWidth = std::max(1, layout.panelWidth - layout.padding * 2 - columnGap * 2);
    const int sideWidth = columnsWidth * 24 / 100;
    const int middleWidth = columnsWidth - sideWidth * 2;
    layout.sectionHeight = std::min(contentHeight / 3,
                                    std::max(20, static_cast<int>(42 * layout.scale)));
    layout.compact = sideWidth < 285;
    layout.showActionDetails = sideWidth >= 300;

    const int leftX = layout.padding;
    const int middleX = leftX + sideWidth + columnGap;
    const int rightX = middleX + middleWidth + columnGap;
    layout.leftPanel = QRect(leftX, contentTop, sideWidth, contentHeight);
    layout.presetPanel = QRect(middleX, contentTop, middleWidth, contentHeight);
    layout.rightPanel = QRect(rightX, contentTop, sideWidth, contentHeight);

    const int profileCount = std::max(0, visibleProfileCount);
    const int profileTop = layout.presetPanel.y() + layout.sectionHeight;
    const int profileAreaHeight = std::max(1, layout.presetPanel.height() - layout.sectionHeight);
    const int minimumRowsHeight = std::min(profileAreaHeight / 2, profileCount * 2);
    const int desiredNavigationHeight = std::max(34, static_cast<int>(48 * layout.scale));
    const int navigationHeight = std::min(desiredNavigationHeight,
                                          std::max(4, (profileAreaHeight - minimumRowsHeight) / 2));
    const int navigationGap = std::min(navigationHeight - 1,
                                       std::max(2, static_cast<int>(3 * layout.scale)));
    layout.profileUpButton = QRect(layout.presetPanel.x(), profileTop,
                                   layout.presetPanel.width(), navigationHeight - navigationGap);
    layout.profileDownButton = QRect(layout.presetPanel.x(),
                                     layout.presetPanel.bottom() + 1 - navigationHeight,
                                     layout.presetPanel.width(), navigationHeight - navigationGap);
    const int profileRowsTop = profileTop + navigationHeight;
    const int profileRowsBottom = layout.profileDownButton.y();
    const int profileHeight = std::max(1, profileRowsBottom - profileRowsTop);
    const int profileGap = std::max(2, static_cast<int>(3 * layout.scale));
    for (int row = 0; row < profileCount; ++row) {
        const int y1 = profileRowsTop + profileHeight * row / profileCount;
        const int y2 = profileRowsTop + profileHeight * (row + 1) / profileCount;
        layout.profileRows.append(insetBottom(
            QRect(layout.presetPanel.x(), y1, layout.presetPanel.width(), std::max(1, y2 - y1)),
            profileGap));
    }

    const int bindingTop = layout.leftPanel.y() + layout.sectionHeight;
    const int bindingHeight = std::max(1, layout.leftPanel.bottom() + 1 - bindingTop);
    const int bindingGap = std::max(2, static_cast<int>(4 * layout.scale));
    constexpr int totalWeight = 56;
    constexpr int modeStart = 20;
    constexpr int modeEnd = 26;
    auto weightedRect = [&](const QRect& panel, int start, int end) {
        const int top = panel.y() + layout.sectionHeight;
        const int y1 = top + bindingHeight * start / totalWeight;
        const int y2 = top + bindingHeight * end / totalWeight;
        return insetBottom(QRect(panel.x(), y1, panel.width(), std::max(1, y2 - y1)), bindingGap);
    };

    layout.leftModeSwitch = weightedRect(layout.leftPanel, modeStart, modeEnd);
    layout.rightModeSwitch = weightedRect(layout.rightPanel, modeStart, modeEnd);
    for (int localSlot = 0; localSlot < BindingsPerSide; ++localSlot) {
        const int start = localSlot < 2 ? localSlot * 10 : modeEnd + (localSlot - 2) * 10;
        const int end = start + 10;
        layout.bindingRows[localSlot] = weightedRect(layout.leftPanel, start, end);
        layout.bindingRows[localSlot + BindingsPerSide] =
            weightedRect(layout.rightPanel, start, end);
    }

    return layout;
}

QuickKeyPanelSide QuickKeyOverlayLayout::panelForSlot(int slot)
{
    if (slot >= 0 && slot < BindingsPerSide) return QuickKeyPanelSide::Left;
    if (slot >= BindingsPerSide && slot < BindingCount) return QuickKeyPanelSide::Right;
    return QuickKeyPanelSide::Invalid;
}

int QuickKeyOverlayLayout::visualRowForSlot(int slot)
{
    if (panelForSlot(slot) == QuickKeyPanelSide::Invalid) return -1;
    const int localSlot = slot % BindingsPerSide;
    return localSlot < 2 ? localSlot : localSlot + 1;
}

std::array<int, QuickKeyOverlayLayout::BindingCount>
QuickKeyOverlayLayout::bindingIndexesBySlot(const QVector<int>& slotNumbers)
{
    std::array<int, BindingCount> indexes{};
    indexes.fill(-1);
    for (int index = 0; index < slotNumbers.size(); ++index) {
        const int slot = slotNumbers[index];
        if (slot >= 0 && slot < BindingCount && indexes[slot] < 0)
            indexes[slot] = index;
    }
    return indexes;
}
