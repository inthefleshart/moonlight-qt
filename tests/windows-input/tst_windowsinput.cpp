#include <QtTest>
#include <QJsonDocument>
#include <QSettings>

#include "streaming/input/inputgeometry.h"
#include "streaming/input/penconversion.h"
#include "streaming/input/pencursorvisibility.h"
#include "streaming/input/pointerhistory.h"
#include "streaming/input/tabletprofileselector.h"
#include "streaming/video/quickkeyoverlaylayout.h"
#include "settings/tabletmappingmanager.h"

class WindowsInputTests : public QObject
{
    Q_OBJECT

private slots:
    void matchingResolutionMapsCorners();
    void letterboxRejectsAndClamps();
    void convertsApolloCompatibleTilt();
    void preservesUnknownTilt();
    void penCursorPoliciesPreserveNavigationCursor();
    void pointerHistoryPreservesTransitionsAndNewest();
    void profileSelectorPaginatesAndHitTests();
    void quickKeyOverlayMatchesPhysicalButtonLayout();
    void quickKeyOverlayNormalizesMalformedSlots();
    void quickKeyOverlayStaysInsideViewport();
    void shippedProfilesExposeTenControls();
};

void WindowsInputTests::matchingResolutionMapsCorners()
{
    auto topLeft = InputGeometry::mapClientPoint(0, 0, 2560, 1440, 2560, 1440, false);
    QVERIFY(topLeft.inside);
    QCOMPARE(topLeft.x, 0.0f);
    QCOMPARE(topLeft.y, 0.0f);

    auto center = InputGeometry::mapClientPoint(1280, 720, 2560, 1440, 3840, 2160, false);
    QVERIFY(center.inside);
    QVERIFY(qAbs(center.x - 0.5f) < 0.0001f);
    QVERIFY(qAbs(center.y - 0.5f) < 0.0001f);
}

void WindowsInputTests::letterboxRejectsAndClamps()
{
    auto outside = InputGeometry::mapClientPoint(800, 20, 1600, 1200, 2560, 1440, false);
    QVERIFY(!outside.inside);

    auto edge = InputGeometry::mapClientPoint(800, 20, 1600, 1200, 2560, 1440, true);
    QVERIFY(!edge.inside);
    QCOMPARE(edge.y, 0.0f);

    auto center = InputGeometry::mapClientPoint(800, 600, 1600, 1200, 2560, 1440, false);
    QVERIFY(center.inside);
    QVERIFY(qAbs(center.x - 0.5f) < 0.001f);
    QVERIFY(qAbs(center.y - 0.5f) < 0.001f);
}

void WindowsInputTests::convertsApolloCompatibleTilt()
{
    auto yPositive = PenConversion::toPolarTilt(0, 30, true);
    QVERIFY(yPositive.valid);
    QCOMPARE(yPositive.rotation, static_cast<uint16_t>(0));
    QCOMPARE(yPositive.tilt, static_cast<uint8_t>(30));

    auto xPositive = PenConversion::toPolarTilt(30, 0, true);
    QCOMPARE(xPositive.rotation, static_cast<uint16_t>(270));
    QCOMPARE(xPositive.tilt, static_cast<uint8_t>(30));

    auto xNegative = PenConversion::toPolarTilt(-30, 0, true);
    QCOMPARE(xNegative.rotation, static_cast<uint16_t>(90));
    QCOMPARE(xNegative.tilt, static_cast<uint8_t>(30));
}

void WindowsInputTests::preservesUnknownTilt()
{
    auto unknown = PenConversion::toPolarTilt(45, 45, false);
    QVERIFY(!unknown.valid);
    QCOMPARE(unknown.rotation, static_cast<uint16_t>(0xFFFF));
    QCOMPARE(unknown.tilt, static_cast<uint8_t>(0xFF));
}

void WindowsInputTests::penCursorPoliciesPreserveNavigationCursor()
{
    using namespace PenCursorVisibility;

    QVERIFY(!shouldHide(Automatic, false, false));
    QVERIFY(!shouldHide(Automatic, true, false));
    QVERIFY(shouldHide(Automatic, true, true));

    QVERIFY(!shouldHide(AlwaysVisible, false, false));
    QVERIFY(!shouldHide(AlwaysVisible, true, false));
    QVERIFY(!shouldHide(AlwaysVisible, true, true));

    QVERIFY(!shouldHide(HideInRange, false, false));
    QVERIFY(shouldHide(HideInRange, true, false));
    QVERIFY(shouldHide(HideInRange, true, true));
}

void WindowsInputTests::pointerHistoryPreservesTransitionsAndNewest()
{
    std::array<PointerHistory::SampleState, PointerHistory::BufferCapacity> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i].pressure = 512;
    }
    samples[47].penFlags = 1;
    samples[48].penFlags = 1;
    samples[96].pointerFlags = 2;

    const auto selected = PointerHistory::select(samples.data(), samples.size());
    QCOMPARE(selected.count, PointerHistory::SendCapacity);
    QCOMPARE(selected.indices.front(), static_cast<std::size_t>(0));
    QCOMPARE(selected.indices[selected.count - 1], samples.size() - 1);

    bool keptTransition47 = false;
    bool keptTransition48 = false;
    bool keptTransition96 = false;
    for (std::size_t i = 0; i < selected.count; ++i) {
        keptTransition47 |= selected.indices[i] == 47;
        keptTransition48 |= selected.indices[i] == 48;
        keptTransition96 |= selected.indices[i] == 96;
        if (i > 0) {
            QVERIFY(selected.indices[i] > selected.indices[i - 1]);
        }
    }
    QVERIFY(keptTransition47);
    QVERIFY(keptTransition48);
    QVERIFY(keptTransition96);
}

void WindowsInputTests::profileSelectorPaginatesAndHitTests()
{
    QStringList profiles;
    for (int i = 1; i <= 15; ++i) profiles << QString("Profile %1").arg(i, 2, 10, QChar('0'));
    TabletProfileSelector selector;
    selector.open(profiles, "Profile 12", {"Profile 01", "Profile 12"});
    QVERIFY(selector.isOpen());
    QCOMPARE(selector.totalCount(), 15);
    QCOMPARE(selector.selectedProfile(), QString("Profile 12"));
    QCOMPARE(selector.firstVisibleIndex(), 2);
    QCOMPARE(selector.profileAtVisibleRow(9), QString("Profile 12"));
    QVERIFY(selector.isFavorite("Profile 12"));
    QVERIFY(selector.isActive("Profile 12"));
    QVERIFY(selector.renderText().contains("> * Profile 12"));

    selector.moveSelection(1);
    QCOMPARE(selector.selectedProfile(), QString("Profile 13"));
    selector.pageSelection(-1);
    QCOMPARE(selector.selectedProfile(), QString("Profile 03"));
    selector.selectLast();
    QCOMPARE(selector.selectedProfile(), QString("Profile 15"));
    selector.moveSelection(1);
    QCOMPARE(selector.selectedProfile(), QString("Profile 01"));

    selector.selectFirst();
    const int overlayWidth = 600;
    const int lineHeight = 30;
    const int overlayHeight = (TabletProfileSelector::HeaderLines +
                               TabletProfileSelector::PageSize + 2) * lineHeight;
    const int left = (1200 - overlayWidth) / 2;
    const int top = (800 - overlayHeight) / 2;
    QCOMPARE(selector.rowForPoint(left + 20,
                                  top + TabletProfileSelector::HeaderLines * lineHeight + 5,
                                  1200, 800, overlayWidth, overlayHeight, lineHeight), 0);
    QCOMPARE(selector.rowForPoint(0, 0, 1200, 800, overlayWidth, overlayHeight, lineHeight), -1);
    selector.close();
    QVERIFY(!selector.isOpen());
}

void WindowsInputTests::quickKeyOverlayMatchesPhysicalButtonLayout()
{
    const auto layout = Overlay::QuickKeyOverlayLayout::calculate(2560, 1440, 10);
    QCOMPARE(layout.profileRows.size(), 10);
    QVERIFY(layout.leftPanel.width() > 0);
    QVERIFY(layout.presetPanel.width() > layout.leftPanel.width() * 2);
    QCOMPARE(layout.leftPanel.width(), layout.rightPanel.width());
    QCOMPARE(layout.profileUpButton.width(), layout.presetPanel.width());
    QCOMPARE(layout.profileDownButton.width(), layout.presetPanel.width());
    QVERIFY(layout.profileUpButton.bottom() < layout.profileRows.first().top());
    QVERIFY(layout.profileDownButton.top() > layout.profileRows.last().bottom());

    for (int slot = 0; slot < 5; ++slot) {
        QCOMPARE(Overlay::QuickKeyOverlayLayout::panelForSlot(slot),
                 Overlay::QuickKeyPanelSide::Left);
        QVERIFY(layout.leftPanel.contains(layout.bindingRows[slot]));
    }
    for (int slot = 5; slot < 10; ++slot) {
        QCOMPARE(Overlay::QuickKeyOverlayLayout::panelForSlot(slot),
                 Overlay::QuickKeyPanelSide::Right);
        QVERIFY(layout.rightPanel.contains(layout.bindingRows[slot]));
    }
    QCOMPARE(Overlay::QuickKeyOverlayLayout::panelForSlot(-1),
             Overlay::QuickKeyPanelSide::Invalid);
    QCOMPARE(Overlay::QuickKeyOverlayLayout::panelForSlot(10),
             Overlay::QuickKeyPanelSide::Invalid);

    QCOMPARE(Overlay::QuickKeyOverlayLayout::visualRowForSlot(0), 0);
    QCOMPARE(Overlay::QuickKeyOverlayLayout::visualRowForSlot(1), 1);
    QCOMPARE(Overlay::QuickKeyOverlayLayout::visualRowForSlot(2), 3);
    QCOMPARE(Overlay::QuickKeyOverlayLayout::visualRowForSlot(6), 1);
    QCOMPARE(Overlay::QuickKeyOverlayLayout::visualRowForSlot(7), 3);
    QVERIFY(layout.leftModeSwitch.y() > layout.bindingRows[1].y());
    QVERIFY(layout.leftModeSwitch.bottom() < layout.bindingRows[2].bottom());
    QVERIFY(layout.rightModeSwitch.y() > layout.bindingRows[6].y());
    QVERIFY(layout.rightModeSwitch.bottom() < layout.bindingRows[7].bottom());
}

void WindowsInputTests::quickKeyOverlayStaysInsideViewport()
{
    for (const QSize viewport : {QSize(2560, 1440), QSize(3840, 2160),
                                 QSize(1280, 720), QSize(720, 1280),
                                 QSize(640, 480), QSize(320, 240)}) {
        const auto layout = Overlay::QuickKeyOverlayLayout::calculate(
            viewport.width(), viewport.height(), 10);
        QVERIFY(layout.panelWidth + layout.shadowSize <= viewport.width());
        QVERIFY(layout.panelHeight + layout.shadowSize <= viewport.height());
        QVERIFY(layout.leftPanel.right() < layout.panelWidth);
        QVERIFY(layout.presetPanel.right() < layout.panelWidth);
        QVERIFY(layout.rightPanel.right() < layout.panelWidth);
        for (const QRect& rect : layout.profileRows)
            QVERIFY(layout.presetPanel.contains(rect));
        QVERIFY(layout.presetPanel.contains(layout.profileUpButton));
        QVERIFY(layout.presetPanel.contains(layout.profileDownButton));
        if (!layout.profileRows.isEmpty()) {
            QVERIFY(layout.profileUpButton.bottom() < layout.profileRows.first().top());
            QVERIFY(layout.profileRows.last().bottom() < layout.profileDownButton.top());
            for (int row = 1; row < layout.profileRows.size(); ++row)
                QVERIFY(layout.profileRows[row - 1].bottom() < layout.profileRows[row].top());
        }
        for (const QRect& rect : layout.bindingRows)
            QVERIFY(rect.right() < layout.panelWidth && rect.bottom() < layout.panelHeight);
    }
}

void WindowsInputTests::quickKeyOverlayNormalizesMalformedSlots()
{
    const QVector<int> shuffledSlots{7, -1, 2, 7, 10, 0, 9};
    const auto indexes = Overlay::QuickKeyOverlayLayout::bindingIndexesBySlot(shuffledSlots);
    QCOMPARE(indexes[0], 5);
    QCOMPARE(indexes[2], 2);
    QCOMPARE(indexes[7], 0);
    QCOMPARE(indexes[9], 6);
    QCOMPARE(indexes[1], -1);
    QCOMPARE(indexes[8], -1);
}

void WindowsInputTests::shippedProfilesExposeTenControls()
{
    QCoreApplication::setOrganizationName("MoonlightInputTests");
    QCoreApplication::setApplicationName("MoonlightInputTests");
    QSettings settings;
    settings.clear();
    const QByteArray oldV2Action = R"({"schema":2,"kind":2,"activation":0,"name":"Migrated Undo","chord":{"key":90,"control":true,"alt":false,"shift":false,"meta":false},"sequence":[],"mouseButton":0,"wheelDelta":0,"localAction":0})";
    settings.setValue("tabletMappings/v2/activeProfile", "Krita");
    settings.setValue("tabletMappings/v2/profiles/krita/slot1", oldV2Action);

    auto* manager = TabletMappingManager::get();
    QCOMPARE(manager->activeProfile(), QString("Krita"));
    QCOMPARE(settings.value("tabletMappings/v2/profiles/krita/slot1").toByteArray(), oldV2Action);
    const auto migratedJson = QJsonDocument::fromJson(
        settings.value("tabletMappings/v3/profiles/krita/slot1").toByteArray()).object();
    QCOMPARE(migratedJson.value("schema").toInt(), 3);
    QCOMPARE(migratedJson.value("name").toString(), QString("Migrated Undo"));
    QCOMPARE(manager->rowCount(), TabletMappingManager::SlotCount);
    const QStringList requiredProfiles = {
        "Krita", "Photoshop", "Substance 3D Painter", "ZBrush — Right-Click Navigation",
        "Mudbox", "Mari", "Daz Studio — Keyboard Navigation", "3DCoat — Default Navigation",
        "Maya", "3ds Max — Standard Interaction", "Blender — Default Keymap",
        "Marmoset Toolbag", "Windows 10 Remote", "Windows 11 Remote",
        "Maya-Style 3D Navigation", "3ds Max-Style 3D Navigation", "Generic Sculpting",
        "Generic Texture Painting", "Blank / Pass-Through"
    };
    for (const auto& profile : requiredProfiles) {
        QVERIFY2(manager->profiles().contains(profile), qPrintable(profile));
    }
    QStringList sortedProfiles = manager->profiles();
    sortedProfiles.sort(Qt::CaseInsensitive);
    QCOMPARE(manager->profiles(), sortedProfiles);
    QCOMPARE(manager->favoriteProfileCount(), manager->profiles().size());

    for (const auto& profile : manager->profiles()) {
        manager->setActiveProfile(profile);
        QCOMPARE(manager->rowCount(), 10);
        for (int slot = 0; slot < TabletMappingManager::SlotCount; ++slot) {
            QVERIFY2(manager->actionForSlot(slot).valid(), qPrintable(profile));
        }
    }

    manager->setActiveProfile("Default");
    QCOMPARE(manager->actionForSlot(0).kind, TabletActionKind::KeyChord);
    QCOMPARE(manager->actionForSlot(0).name, QString("Undo"));
    QCOMPARE(manager->actionForSlot(9).name, QString("Show desktop"));
    manager->setActiveProfile("Blank / Pass-Through");
    for (int slot = 0; slot < TabletMappingManager::SlotCount; ++slot) {
        QCOMPARE(manager->actionForSlot(slot).kind, TabletActionKind::PassThrough);
    }

    manager->setActiveProfile("ZBrush — Right-Click Navigation");
    QCOMPARE(manager->actionForSlot(4).activation, TabletActivation::Hold);
    QCOMPARE(manager->actionForSlot(6).kind, TabletActionKind::PenGesture);
    QCOMPARE(manager->actionForSlot(7).kind, TabletActionKind::PenGesture);
    manager->setActiveProfile("Blender — Default Keymap");
    QCOMPARE(manager->actionForSlot(5).kind, TabletActionKind::PenGesture);
    QCOMPARE(manager->actionForSlot(7).kind, TabletActionKind::PenGesture);

    for (int slot = 0; slot < TabletMappingManager::SlotCount; ++slot) {
        const auto source = manager->sourceForSlot(slot);
        QVERIFY(source.valid);
        QCOMPARE(source.chord.virtualKey, static_cast<quint16>(0x70 + slot));
    }

    QSignalSpy resetSpy(manager, &QAbstractItemModel::modelReset);
    manager->setActiveProfile("Krita");
    QCOMPARE(resetSpy.count(), 1);
    manager->resetBinding(0);
    QCOMPARE(manager->data(manager->index(0), TabletMappingManager::ActionNameRole).toString(),
             QString("Undo"));
    QVERIFY(manager->data(manager->index(0), TabletMappingManager::ActionTextRole).toString().contains("Ctrl+Z"));

    for (const QString& profile : manager->profiles()) {
        if (profile != "Default" && profile != "Krita")
            QVERIFY(manager->setProfileFavorite(profile, false));
    }
    QCOMPARE(manager->favoriteProfileCount(), 2);
    manager->setActiveProfile("Photoshop");
    QCOMPARE(manager->cycleFavoriteProfile(1), QString("Default"));
    QCOMPARE(manager->cycleFavoriteProfile(1), QString("Krita"));
    QCOMPARE(manager->cycleFavoriteProfile(1), QString("Default"));
    manager->setActiveProfile("Photoshop");
    QCOMPARE(manager->cycleFavoriteProfile(-1), QString("Krita"));
    QVERIFY(manager->moveFavoriteProfile("Krita", -1));
    manager->setActiveProfile("Photoshop");
    QCOMPARE(manager->cycleFavoriteProfile(1), QString("Krita"));
    QVERIFY(manager->setProfileFavorite("Default", false));
    QVERIFY(!manager->setProfileFavorite("Krita", false));
    manager->resetFavoriteProfiles();
    QCOMPARE(manager->favoriteProfileCount(), manager->profiles().size());

    manager->setActiveProfile("Default");
    QVERIFY(manager->setActionOption(0, "profileSelector"));
    QCOMPARE(manager->actionForSlot(0).localAction, TabletLocalAction::OpenProfileSelector);
    QVERIFY(manager->setActionOption(1, "nextProfile"));
    QCOMPARE(manager->actionForSlot(1).localAction, TabletLocalAction::NextFavoriteProfile);
    QVERIFY(manager->setActionOption(2, "previousProfile"));
    QCOMPARE(manager->actionForSlot(2).localAction, TabletLocalAction::PreviousFavoriteProfile);

    manager->setActiveProfile("Krita");
    QVERIFY(manager->setActionOption(0, "diagnostics"));
    manager->setActiveProfile("Default");
    const auto kritaReference = manager->actionsForProfile("Krita");
    QCOMPARE(kritaReference.size(), TabletMappingManager::SlotCount);
    QCOMPARE(kritaReference[0].localAction, TabletLocalAction::ToggleDiagnostics);
    QVERIFY(kritaReference[0].modified);
    QCOMPARE(manager->activeProfile(), QString("Default"));
    QVERIFY(manager->actionsForProfile("Not a profile").isEmpty());
}

QTEST_GUILESS_MAIN(WindowsInputTests)
#include "tst_windowsinput.moc"
