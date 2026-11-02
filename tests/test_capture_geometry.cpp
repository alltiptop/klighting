// SPDX-License-Identifier: GPL-3.0-or-later
#include "capture_geometry.h"
#include <QtTest>

using namespace Ambient;
using KWin::Rect;
using KWin::RectF;

class CaptureGeometryTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void fractionalScale150()
    {
        // The user's 2560x1600 output at 150%. Reproduce the two coordinate
        // calculations in LogicalOutput::setData and Compositor::assignOutputLayers.
        constexpr double scale = 1.5;
        const RectF screen(QPointF(), QSizeF(2560, 1600) / scale);
        const RectF viewport = screen.scaled(scale).rounded().scaled(1.0 / scale);
        QVERIFY(!viewport.contains(screen)); // The old guard rejects this valid frame.
        const QRect area = captureRegion(screen, viewport, scale, false, 25);
        QCOMPARE(area, QRect(0, 0, 1706, 1066));
        QVERIFY(Rect(0, 0, 2560, 1600).contains(RectF(area).scaled(scale).rounded()));
    }
    void scalesAndPositions_data()
    {
        QTest::addColumn<double>("scale");
        QTest::addColumn<QPoint>("position");
        QTest::newRow("100-percent") << 1.0 << QPoint(0, 0);
        QTest::newRow("125-percent-offset") << 1.25 << QPoint(1919, 101);
        QTest::newRow("150-percent-negative") << 1.5 << QPoint(-1707, -41);
        QTest::newRow("175-percent-negative") << 1.75 << QPoint(-1919, 1);
        QTest::newRow("200-percent") << 2.0 << QPoint(0, 0);
    }
    void scalesAndPositions()
    {
        QFETCH(double, scale);
        QFETCH(QPoint, position);
        const RectF screen(position, QSizeF(2560, 1600) / scale);
        const Rect pixels = screen.scaled(scale).rounded();
        const RectF viewport = pixels.scaled(1.0 / scale);
        const QRect area = captureRegion(screen, viewport, scale, false, 25);
        QVERIFY(!area.isEmpty());
        QVERIFY(pixels.contains(RectF(area).scaled(scale).rounded()));
        QVERIFY(screen.contains(RectF(area)));
        QVERIFY(viewport.contains(RectF(area)));
        const QRect bottom = captureRegion(screen, viewport, scale, true, 25);
        QVERIFY(area.contains(bottom));
        QCOMPARE(bottom.bottom(), area.bottom());
        QCOMPARE(bottom.height(), area.height() / 4);
    }
    void rejectsPartialAndForeignViews()
    {
        const RectF screen(0, 0, 1920, 1080);
        QVERIFY(captureRegion(screen, { 0, 0, 960, 1080 }, 1.0, false, 25).isEmpty());
        QVERIFY(captureRegion(screen, { 1920, 0, 1920, 1080 }, 1.0, false, 25).isEmpty());
        QVERIFY(captureRegion(screen, { }, 1.0, false, 25).isEmpty());
        QVERIFY(captureRegion(screen, screen, 0.0, false, 25).isEmpty());
    }
};
QTEST_GUILESS_MAIN(CaptureGeometryTest)
#include "test_capture_geometry.moc"
