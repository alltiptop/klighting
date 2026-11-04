// SPDX-License-Identifier: GPL-3.0-or-later
#include "capture_channel.h"
#include "component_info.h"
#include "lighting_ipc.h"
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
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
    void providerIdentityChangesOnlyOnRecreation()
    {
        QString previous;
        {
            CaptureChannel channel;
            const auto info = channel.Snapshot();
            QCOMPARE(info.value("buildId"), componentInfo(Component::Capture).value("buildId"));
            QVERIFY(compatibleComponent(Component::Capture, info));
            previous = info.value("instanceId").toString();
            QVERIFY(!previous.isEmpty());
            channel.setError("No screen");
            QCOMPARE(channel.Snapshot().value("instanceId").toString(), previous);
        }
        CaptureChannel replacement;
        QVERIFY(replacement.Snapshot().value("instanceId").toString() != previous);
    }
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
        QVERIFY(!QDBusPendingReply<>(accepted).isError());
        QTRY_VERIFY(!configured.isEmpty() && configured.last()[0].toInt() == 5);
        channel.setState(false, false, false);
        channel.setError("GPU readback failed");
        const auto failed = channel.Snapshot();
        QVERIFY(failed.value("providerAvailable").toBool());
        QVERIFY(!failed.value("screenAvailable").toBool());
        QVERIFY(!failed.value("enabled").toBool());
        QVERIFY(!failed.value("ready").toBool());
        const int beforeRetry = configured.size();
        auto retry = configure(peer);
        QTRY_VERIFY(retry.isFinished());
        QTRY_COMPARE(configured.size(), beforeRetry + 1); // Failed/stopped providers still accept Configure.
        channel.setError({ });
        QCOMPARE(channel.Snapshot().value("status").toString(), "Selected screen is unavailable");
        channel.setState(true, false, false);
        QVERIFY(channel.Snapshot().value("ready").toBool());
        QVERIFY(!channel.Snapshot().value("enabled").toBool());
        channel.setState(true, true, true);
        QVERIFY(!channel.Snapshot().value("ready").toBool());
        channel.setState(true, true, false);
        QVERIFY(channel.Snapshot().value("ready").toBool());
        configured.clear();
        auto rejected = configure(observer);
        QTRY_VERIFY(rejected.isFinished());
        QCOMPARE(QDBusPendingReply<>(rejected).error().type(), QDBusError::AccessDenied);
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
