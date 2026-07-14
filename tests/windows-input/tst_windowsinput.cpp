#include <QtTest>
#include <QSettings>

#include "streaming/input/inputgeometry.h"
#include "streaming/input/penconversion.h"
#include "streaming/input/pointerhistory.h"
#include "settings/tabletmappingmanager.h"

class WindowsInputTests : public QObject
{
    Q_OBJECT

private slots:
    void matchingResolutionMapsCorners();
    void letterboxRejectsAndClamps();
    void convertsApolloCompatibleTilt();
    void preservesUnknownTilt();
    void pointerHistoryPreservesTransitionsAndNewest();
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

void WindowsInputTests::shippedProfilesExposeTenControls()
{
    QCoreApplication::setOrganizationName("MoonlightInputTests");
    QCoreApplication::setApplicationName("MoonlightInputTests");
    QSettings().clear();

    auto* manager = TabletMappingManager::get();
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

    for (const auto& profile : manager->profiles()) {
        manager->setActiveProfile(profile);
        QCOMPARE(manager->rowCount(), 10);
        for (int slot = 0; slot < TabletMappingManager::SlotCount; ++slot) {
            QVERIFY2(manager->actionForSlot(slot).valid(), qPrintable(profile));
        }
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
    QCOMPARE(manager->data(manager->index(0), TabletMappingManager::ActionNameRole).toString(),
             QString("Undo"));
    QVERIFY(manager->data(manager->index(0), TabletMappingManager::ActionTextRole).toString().contains("Ctrl+Z"));
}

QTEST_GUILESS_MAIN(WindowsInputTests)
#include "tst_windowsinput.moc"
