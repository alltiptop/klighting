// SPDX-License-Identifier: GPL-3.0-or-later
#include "capture_channel.h"
#include "lighting_ipc.h"
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QtTest>
using namespace Ambient;
class SampleReceiver : public QObject
{
    Q_OBJECT
public:
    int received = 0;
public Q_SLOTS:
    void sample(const QByteArray&) { ++received; }
};
class CaptureIpcTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void onlyDaemonCanConfigureAndReceiveSamples()
    {
        CaptureChannel channel;
        QSignalSpy configured(&channel, &CaptureChannel::configured);
        auto peer = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), "daemon");
        auto observer
            = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), "observer");
        QVERIFY(peer.registerService(Ipc::Service));
        QTRY_VERIFY(!configured.isEmpty()); // Owner discovery stops stale capture first.
        configured.clear();
        const auto sender = QDBusConnection::sessionBus().baseService();
        SampleReceiver recipient, other;
        QVERIFY(peer.connect(
            sender, Ipc::CapturePath, Ipc::CaptureInterface, "Sample", &recipient, SLOT(sample(QByteArray))));
        QVERIFY(observer.connect(
            sender, Ipc::CapturePath, Ipc::CaptureInterface, "Sample", &other, SLOT(sample(QByteArray))));
        auto configure = [&](const QDBusConnection& bus) {
            auto call = QDBusMessage::createMethodCall(
                sender, Ipc::CapturePath, Ipc::CaptureInterface, "Configure");
            call << 5 << QVariantMap { };
            return bus.asyncCall(call, 1000);
        };
        auto accepted = configure(peer);
        QTRY_VERIFY(accepted.isFinished());
        QTRY_VERIFY(!configured.isEmpty() && configured.last()[0].toInt() == 5);
        configured.clear();
        auto rejected = configure(observer);
        QTRY_VERIFY(rejected.isFinished());
        QCOMPARE(configured.size(), 0);
        channel.sendSample("short");
        channel.sendSample(QByteArray(Ipc::SampleBytes, '\0'));
        QTRY_COMPARE(recipient.received, 1);
        QTest::qWait(30);
        QCOMPARE(other.received, 0); // Screen samples are not broadcast on the bus.
        configured.clear();
        QVERIFY(peer.unregisterService(Ipc::Service));
        QTRY_COMPARE(configured.size(), 1);
        QCOMPARE(configured.first()[0].toInt(), 0);
        channel.sendSample(QByteArray(Ipc::SampleBytes, '\0'));
        QTest::qWait(30);
        QCOMPARE(recipient.received, 1);
        // Restarting the daemon requires fresh configuration, without reloading KWin.
        QVERIFY(observer.registerService(Ipc::Service));
        QTRY_COMPARE(configured.size(), 2);
        auto restarted = configure(observer);
        QTRY_VERIFY(restarted.isFinished());
        QTRY_COMPARE(configured.size(), 3);
        channel.sendSample(QByteArray(Ipc::SampleBytes, '\0'));
        QTRY_COMPARE(other.received, 1);
        QCOMPARE(recipient.received, 1);
        QVERIFY(observer.unregisterService(Ipc::Service));
        QDBusConnection::disconnectFromBus("daemon");
        QDBusConnection::disconnectFromBus("observer");
    }
};
QTEST_GUILESS_MAIN(CaptureIpcTest)
#include "test_capture_ipc.moc"
