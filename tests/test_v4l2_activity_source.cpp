// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/media/v4l2_activity_source.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <sys/stat.h>
#include <unistd.h>

using namespace Ambient;
using namespace std::chrono_literals;
class V4l2ActivitySourceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void classifiesCapabilities()
    {
        QVERIFY(isV4l2CaptureDevice(":capture:streaming:"));
        QVERIFY(isV4l2CaptureDevice(":capture:mplane:"));
        QVERIFY(!isV4l2CaptureDevice(":meta_capture:streaming:"));
        QVERIFY(!isV4l2CaptureDevice(":video_output:streaming:"));
        QVERIFY(!isV4l2CaptureDevice(":radio:vbi_capture:"));
        QVERIFY(!isV4l2CaptureDevice(":capture:video_m2m:"));
        QVERIFY(!isV4l2CaptureDevice(":capture:touch:"));
        QVERIFY(!isV4l2CaptureDevice({ }));
    }
    void detectsExistingHandlesAliasesAndLastClose()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(QDir(root.path()).mkpath("123/fd"));
        QVERIFY(QDir(root.path()).mkpath("456/fd"));
        // Harmless character-device metadata substitutes for a camera; no device is opened.
        struct stat device { };
        QVERIFY(stat("/dev/null", &device) == 0);
        const QSet<quint64> cameras { quint64(device.st_rdev) };
        const auto sample = [&] { return openCameraDevices(cameras, root.path(), getuid()); };
        QCOMPARE(sample(), QVariant(false));
        QVERIFY(QFile::link("/dev/null", root.filePath("camera-alias")));
        QVERIFY(QFile::link(root.filePath("camera-alias"), root.filePath("123/fd/7")));
        QVERIFY(QFile::link("/dev/null", root.filePath("456/fd/9")));
        QCOMPARE(sample(), QVariant(true));
        QCOMPARE(openCameraDevices(cameras, root.path(), getuid() + 1), QVariant(false));
        QCOMPARE(openCameraDevices({ }, root.path(), getuid()), QVariant(false));
        QVERIFY(QFile::remove(root.filePath("123/fd/7")));
        QCOMPARE(sample(), QVariant(true));
        QVERIFY(QFile::remove(root.filePath("456/fd/9")));
        QCOMPARE(sample(), QVariant(false));
        QVERIFY(QFile::link("/dev/zero", root.filePath("123/fd/8")));
        QVERIFY(QFile::link(root.filePath("missing-device"), root.filePath("123/fd/10")));
        QCOMPARE(sample(), QVariant(false));
        QVERIFY(!openCameraDevices(cameras, root.filePath("missing-proc"), getuid()).isValid());
    }
    void pollLifecycleAndRecovery()
    {
        std::atomic<int> value = 1, calls = 0;
        V4l2ActivitySource source(
            [&](std::stop_token) {
                ++calls;
                const int current = value.load();
                return current < 0 ? QVariant() : QVariant(bool(current));
            },
            10ms);
        QSignalSpy spy(&source, &MediaActivitySource::changed);
        const auto state = [&] { return spy.isEmpty() ? QVariantMap() : spy.last()[0].toMap(); };
        QCOMPARE(calls.load(), 0);
        source.setEnabled(true);
        QTRY_COMPARE(state()["camera-active"], QVariant(true));
        value = 0;
        QTRY_COMPARE(state()["camera-active"], QVariant(false));
        value = -1;
        QTRY_VERIFY(state().isEmpty());
        value = 1;
        QTRY_COMPARE(state()["camera-active"], QVariant(true));
        source.setEnabled(false);
        const auto stoppedCalls = calls.load();
        QTest::qWait(30);
        QCOMPARE(calls.load(), stoppedCalls);
        QVERIFY(state().isEmpty());
        value = 0;
        source.setEnabled(true);
        QTRY_COMPARE(state()["camera-active"], QVariant(false));
    }
    void rejectsQueuedOldSnapshotAndCanDisableFromCallback()
    {
        std::atomic<int> calls = 0;
        V4l2ActivitySource source(
            [&](std::stop_token) {
                ++calls;
                return QVariant(true);
            },
            10ms);
        QSignalSpy spy(&source, &MediaActivitySource::changed);
        source.setEnabled(true);
        // Do not dispatch events until an old-generation snapshot has been queued.
        QElapsedTimer timeout;
        timeout.start();
        while (calls.load() == 0 && timeout.elapsed() < 1000)
            QTest::qSleep(1);
        QVERIFY(calls.load() > 0);
        source.setEnabled(false);
        QCoreApplication::processEvents();
        for (const auto& arguments : spy)
            QVERIFY(arguments[0].toMap().isEmpty());
        connect(&source, &MediaActivitySource::changed, &source, [&](const QVariantMap& values) {
            if (values.value("camera-active").toBool())
                source.setEnabled(false);
        });
        source.setEnabled(true);
        QTRY_VERIFY(spy.size() >= 5);
        QVERIFY(spy.last()[0].toMap().isEmpty());
    }
};
QTEST_GUILESS_MAIN(V4l2ActivitySourceTest)
#include "test_v4l2_activity_source.moc"
