#include <QtTest>

#include "streaming/input/inputgeometry.h"
#include "streaming/input/penconversion.h"

class WindowsInputTests : public QObject
{
    Q_OBJECT

private slots:
    void matchingResolutionMapsCorners();
    void letterboxRejectsAndClamps();
    void convertsApolloCompatibleTilt();
    void preservesUnknownTilt();
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

QTEST_MAIN(WindowsInputTests)
#include "tst_windowsinput.moc"
