// SPDX-License-Identifier: GPL-3.0-or-later
#include "lamp_array.h"
#include <QtTest>

using namespace Ambient;
namespace
{
struct Sink {
    QList<QByteArray> reports;
    int failAt = -1;
    QString send(QByteArray report)
    {
        reports.append(report);
        return reports.size() == failAt ? QStringLiteral("short or failed USB write") : QString();
    }
};
LampArrayWriteResult write(LampArrayOutput& output, const LedFrame& frame, Sink& sink)
{
    QList<bool> layout(88, true);
    layout.append(QList<bool>(12, false));
    return output.write(frame, layout, [&](QByteArray report) { return sink.send(report); });
}
const QByteArray RedFrame = QByteArray::fromHex("050100006300ff0000ff");
}
class LampArrayOutputTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void firstFrameCommitsAndDuplicateCompletesWithoutWrite()
    {
        LampArrayOutput output;
        Sink sink;
        const LedFrame frame { QColor(Qt::red), 2, true, true };
        const auto first = write(output, frame, sink);
        QVERIFY(first.error.isEmpty());
        QVERIFY(first.written);
        QCOMPARE(sink.reports, QList<QByteArray> { RedFrame });
        const auto duplicate = write(output, frame, sink);
        QVERIFY(duplicate.error.isEmpty());
        QVERIFY(!duplicate.written);
        QCOMPARE(sink.reports.size(), 1);
    }
    void disabledZones_data()
    {
        QTest::addColumn<bool>("keyboard");
        QTest::newRow("keyboard") << false;
        QTest::newRow("strip") << true;
    }
    void disabledZones()
    {
        QFETCH(bool, keyboard);
        LampArrayOutput output;
        Sink sink;
        LedFrame frame { QColor(Qt::red), 2, keyboard, !keyboard, QList<QColor>(88, Qt::red),
            QList<QColor>(12, Qt::red) };
        QVERIFY(write(output, frame, sink).written);
        const auto original = sink.reports;
        if (keyboard)
            frame.stripColors.fill(Qt::blue);
        else
            frame.keyColors.fill(Qt::blue);
        QVERIFY(!write(output, frame, sink).written);
        QCOMPARE(sink.reports, original);
        frame.keyboard = frame.lightbar = true;
        const auto changed = write(output, frame, sink);
        QVERIFY(changed.error.isEmpty());
        QVERIFY(changed.written);
        const QList<QByteArray> expected = keyboard
            ? QList<QByteArray> { QByteArray::fromHex("050000005700ff0000ff"),
                  QByteArray::fromHex("0501580063000000ffff") }
            : QList<QByteArray> { QByteArray::fromHex("0500000057000000ffff"),
                  QByteArray::fromHex("050158006300ff0000ff") };
        QCOMPARE(sink.reports.mid(original.size()), expected);
    }
    void equivalentRgbBytesIgnoreAlphaAndColorRepresentation()
    {
        LampArrayOutput output;
        Sink sink;
        LedFrame frame { QColor(18, 52, 86), 2, true, true };
        QVERIFY(write(output, frame, sink).written);
        const QList<QColor> equivalent { QColor(18, 52, 86, 0),
            QColor::fromRgba64(0x1213, 0x3435, 0x5657, 0x7777), QColor(18, 52, 86).toHsv() };
        for (const auto& color : equivalent) {
            QCOMPARE(color.rgb(), qRgb(18, 52, 86));
            frame.color = color;
            const auto result = write(output, frame, sink);
            QVERIFY(result.error.isEmpty());
            QVERIFY(!result.written);
        }
        QCOMPARE(sink.reports, QList<QByteArray> { QByteArray::fromHex("050100006300123456ff") });
    }
    void equivalentUniformAndPerElementOutput()
    {
        LampArrayOutput output;
        Sink sink;
        LedFrame frame { QColor(Qt::red), 2, true, true };
        QVERIFY(write(output, frame, sink).written);
        frame.keyColors = QList<QColor>(88, Qt::red);
        frame.stripColors = QList<QColor>(12, Qt::red);
        for (int i = 0; i < 88; ++i)
            frame.keyColors[i].setAlpha(i + 1);
        const auto result = write(output, frame, sink);
        QVERIFY(result.error.isEmpty());
        QVERIFY(!result.written);
        QCOMPARE(sink.reports, QList<QByteArray> { RedFrame });
    }
    void hardwareBrightnessIsIndependentButOffChangesOutput()
    {
        LampArrayOutput output;
        Sink sink;
        LedFrame frame { QColor(Qt::red), 1, true, true };
        QVERIFY(write(output, frame, sink).written);
        for (const uint level : { 2u, 3u, 1u }) {
            frame.brightness = level;
            QVERIFY(!write(output, frame, sink).written);
        }
        QCOMPARE(sink.reports, QList<QByteArray> { RedFrame });
        frame.brightness = 0;
        QVERIFY(write(output, frame, sink).written);
        QCOMPARE(sink.reports.last(), QByteArray::fromHex("05010000630000000000"));
        frame.brightness = 1;
        QVERIFY(write(output, frame, sink).written);
        QCOMPARE(sink.reports.last(), RedFrame);
    }
    void blackEnabledAndDisabledHaveDifferentIntensity()
    {
        LampArrayOutput output;
        Sink sink;
        LedFrame frame { QColor(Qt::black), 2, false, false };
        QVERIFY(write(output, frame, sink).written);
        QCOMPARE(sink.reports.last(), QByteArray::fromHex("05010000630000000000"));
        frame.keyboard = frame.lightbar = true;
        QVERIFY(write(output, frame, sink).written);
        QCOMPARE(sink.reports.last(), QByteArray::fromHex("050100006300000000ff"));
    }
    void errorInvalidatesPreviousCommit_data()
    {
        QTest::addColumn<bool>("multi");
        QTest::addColumn<int>("failure");
        QTest::newRow("range-first") << false << 1;
        QTest::newRow("range-commit") << false << 2;
        QTest::newRow("multi-first") << true << 1;
        QTest::newRow("multi-middle") << true << 5;
        QTest::newRow("multi-commit") << true << 13;
    }
    void errorInvalidatesPreviousCommit()
    {
        QFETCH(bool, multi);
        QFETCH(int, failure);
        LampArrayOutput output;
        Sink sink;
        const LedFrame original { QColor(Qt::red), 2, true, true };
        QVERIFY(write(output, original, sink).written);
        LedFrame changed = original;
        changed.keyColors = QList<QColor>(88, Qt::blue);
        if (multi)
            for (int i = 0; i < 88; ++i)
                changed.keyColors[i] = i % 2 ? Qt::green : Qt::blue;
        sink.failAt = sink.reports.size() + failure;
        const auto failed = write(output, changed, sink);
        QVERIFY(!failed.error.isEmpty());
        QVERIFY(!failed.written);
        QCOMPARE(sink.reports.size(), sink.failAt);
        sink.failAt = -1;
        // Even the previous successfully committed color must be sent again: a
        // partial transfer or failed commit can leave device state unknown.
        QVERIFY(write(output, original, sink).written);
        QCOMPARE(sink.reports.last(), RedFrame);
        const int beforeRetry = sink.reports.size();
        QVERIFY(write(output, changed, sink).written);
        QCOMPARE(sink.reports.size() - beforeRetry, multi ? 13 : 2);
        const auto commit = sink.reports.last();
        QCOMPARE(uchar(commit[multi ? 2 : 1]), 1);
        QVERIFY(!write(output, changed, sink).written);
    }
    void releaseAndReopenRequireAFullFrame()
    {
        LampArrayOutput output;
        Sink sink;
        const LedFrame frame { QColor(Qt::red), 2, true, true };
        QVERIFY(write(output, frame, sink).written);
        output.reset(); // LampArrayDevice::close(), including error and release paths.
        QVERIFY(write(output, frame, sink).written);
        QCOMPARE(sink.reports, (QList<QByteArray> { RedFrame, RedFrame }));
        QVERIFY(!write(output, frame, sink).written);
    }
    void malformedInputCannotBecomeACachedSuccess()
    {
        LampArrayOutput output;
        Sink sink;
        LedFrame frame { QColor(Qt::red), 2, true, true };
        QVERIFY(write(output, frame, sink).written);
        frame.keyColors = { Qt::red };
        const auto invalid = write(output, frame, sink);
        QVERIFY(!invalid.error.isEmpty());
        QVERIFY(!invalid.written);
        QCOMPARE(sink.reports.size(), 1);
        frame.keyColors.clear();
        QVERIFY(write(output, frame, sink).written);
        QCOMPARE(sink.reports.last(), RedFrame);
    }
};
QTEST_GUILESS_MAIN(LampArrayOutputTest)
#include "test_lamp_array_output.moc"
