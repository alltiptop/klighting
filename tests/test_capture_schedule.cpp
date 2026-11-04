// SPDX-License-Identifier: GPL-3.0-or-later
#include "capture_schedule.h"
#include <QtTest>

using namespace Ambient;
class CaptureScheduleTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initialFrameAndIndependentDeadline()
    {
        CaptureSchedule schedule;
        schedule.reset(334);
        QVERIFY(schedule.requestRepaint(0, false));
        QVERIFY(schedule.captureOnPaint(0, false));
        schedule.captured(0.5);
        QVERIFY(!schedule.captureOnPaint(334, false));
        QVERIFY(!schedule.requestRepaint(334, false));
        QVERIFY(schedule.captureOnPaint(334.5, false));
        QVERIFY(!schedule.requestRepaint(334.5, false));
        QVERIFY(schedule.requestRepaint(350.5, false));
    }
    void pendingReadbackDoesNotStartAnotherCapture()
    {
        CaptureSchedule schedule;
        schedule.reset(50);
        schedule.captured(0);
        QVERIFY(!schedule.captureOnPaint(80, true));
        QVERIFY(!schedule.requestRepaint(80, true));
        QVERIFY(schedule.captureOnPaint(80, false));
    }
    void resetRequestsAFreshFrame()
    {
        CaptureSchedule schedule;
        schedule.reset(1000);
        schedule.captured(12345);
        schedule.reset(50);
        QVERIFY(schedule.requestRepaint(0, false));
        QVERIFY(schedule.captureOnPaint(0, false));
    }
    void idleAfterReadback()
    {
        CaptureSchedule schedule;
        schedule.reset(50);
        QVERIFY(schedule.requestRepaint(0, false));
        QVERIFY(schedule.captureOnPaint(1, false));
        schedule.captured(1);
        QCOMPARE(schedule.nextUpdateMs(1, true), 16);
        // A completed readback alone does not require another frame.
        QCOMPARE(schedule.nextUpdateMs(17, false), -1);
        QVERIFY(!schedule.requestRepaint(10000, false));
        QVERIFY(schedule.captureOnPaint(10000, false));
    }
    void finalPaintBeforeDeadline()
    {
        CaptureSchedule schedule;
        schedule.reset(334);
        schedule.captured(0.5);
        QVERIFY(!schedule.captureOnPaint(10, false));
        QVERIFY(!schedule.captureOnPaint(20, false));
        QCOMPARE(schedule.nextUpdateMs(20, false), 331);
        QCOMPARE(schedule.nextUpdateMs(350.49, false), 1);
        QVERIFY(!schedule.requestRepaint(350.49, false));
        QVERIFY(schedule.requestRepaint(350.5, false));
        QVERIFY(schedule.captureOnPaint(351, false));
        schedule.captured(351);
        QCOMPARE(schedule.nextUpdateMs(367, false), -1);
        QVERIFY(!schedule.requestRepaint(1000, false));
    }
    void finalPaintWhileReadbackIsPending()
    {
        CaptureSchedule schedule;
        schedule.reset(50);
        schedule.captured(0);
        QVERIFY(!schedule.captureOnPaint(25, true));
        QVERIFY(!schedule.captureOnPaint(80, true));
        QCOMPARE(schedule.nextUpdateMs(80, true), 16);
        QVERIFY(!schedule.requestRepaint(80, true));
        // Readback completes after the final external paint; request that last frame.
        QVERIFY(schedule.requestRepaint(96, false));
        QVERIFY(schedule.captureOnPaint(97, false));
        schedule.captured(97);
        QCOMPARE(schedule.nextUpdateMs(113, false), -1);
        QVERIFY(!schedule.requestRepaint(1000, false));
    }
    void repeatedPaintsDoNotPostponeDeadline()
    {
        CaptureSchedule schedule;
        schedule.reset(50);
        schedule.captured(0);
        for (int time = 10; time < 50; time += 10) {
            QVERIFY(!schedule.captureOnPaint(time, false));
            QCOMPARE(time + schedule.nextUpdateMs(time, false), 66);
        }
        QVERIFY(schedule.requestRepaint(66, false));
    }
    void naturalPaintAvoidsAnExtraForcedFrame()
    {
        CaptureSchedule schedule;
        schedule.reset(50);
        schedule.captured(0);
        QVERIFY(!schedule.captureOnPaint(34, false));
        QVERIFY(!schedule.requestRepaint(50, false));
        QCOMPARE(schedule.nextUpdateMs(50, false), 16);
        // The next animation frame arrives just after the 20 FPS sample deadline.
        QVERIFY(schedule.captureOnPaint(51, false));
        schedule.captured(51);
        QCOMPARE(schedule.nextUpdateMs(67, false), -1);
        QVERIFY(!schedule.requestRepaint(67, false));
    }
    void readbackCompletionKeepsTheSamplingDeadline()
    {
        CaptureSchedule schedule;
        schedule.reset(334);
        schedule.captured(0);
        QVERIFY(!schedule.captureOnPaint(10, true));
        QCOMPARE(schedule.nextUpdateMs(10, true), 16);
        // Readback completes quickly, but the next capture is still rate limited.
        QVERIFY(!schedule.requestRepaint(16, false));
        QCOMPARE(schedule.nextUpdateMs(16, false), 334);
        QVERIFY(!schedule.requestRepaint(334, false));
        QVERIFY(schedule.requestRepaint(350, false));
    }
    void lateRequestedPaintDoesNotCreateARepaintLoop()
    {
        CaptureSchedule schedule;
        schedule.reset(50);
        QVERIFY(schedule.requestRepaint(0, false));
        // A natural paint arrives before the requested repaint is delivered.
        QVERIFY(schedule.captureOnPaint(1, false));
        schedule.captured(1);
        QVERIFY(!schedule.captureOnPaint(2, true));
        QCOMPARE(schedule.nextUpdateMs(17, false), 50);
        QVERIFY(schedule.requestRepaint(67, false));
        QVERIFY(schedule.captureOnPaint(68, false));
        schedule.captured(68);
        QCOMPARE(schedule.nextUpdateMs(84, false), -1);
        QVERIFY(!schedule.requestRepaint(1000, false));
    }
    void repaintWaitIsBoundedWithoutBusyPolling()
    {
        CaptureSchedule schedule;
        schedule.reset(50);
        QVERIFY(schedule.requestRepaint(0, false));
        QCOMPARE(schedule.nextUpdateMs(0, false), 250);
        for (int time = 1; time < 250; ++time)
            QVERIFY(!schedule.requestRepaint(time, false));
        // No usable target arrived. Retry at low frequency until painting resumes.
        QVERIFY(schedule.requestRepaint(250, false));
        QCOMPARE(schedule.nextUpdateMs(250, false), 250);
        QVERIFY(schedule.captureOnPaint(300, false));
        schedule.captured(300);
        QCOMPARE(schedule.nextUpdateMs(316, false), -1);
        QVERIFY(!schedule.requestRepaint(500, false));
        schedule.reset(50);
        QVERIFY(schedule.requestRepaint(0, false));
    }
};
QTEST_GUILESS_MAIN(CaptureScheduleTest)
#include "test_capture_schedule.moc"
