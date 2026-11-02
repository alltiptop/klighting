// SPDX-License-Identifier: GPL-3.0-or-later
#include "power_source.h"
#include <KConfigGroup>
#include <QDBusVirtualObject>
#include <QTemporaryDir>
#include <QtTest>
using namespace Ambient;

class PowerServer : public QDBusVirtualObject
{
public:
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "power-server-test");
    QVariantMap root { { "OnBattery", false } };
    QVariantMap display { { "IsPresent", true }, { "Percentage", 50.0 }, { "WarningLevel", 1u } };
    bool deferRoot = false;
    QDBusMessage deferred;
    QVariantMap oldRoot;
    int unexpected = 0, rootReads = 0;
    PowerServer()
    {
        if (!bus.registerService("org.freedesktop.UPower")
            || !bus.registerVirtualObject("/org/freedesktop/UPower", this, QDBusConnection::SubPath))
            qFatal("Cannot create isolated UPower fixture");
    }
    ~PowerServer() override
    {
        bus.unregisterService("org.freedesktop.UPower");
        bus.unregisterObject("/org/freedesktop/UPower", QDBusConnection::UnregisterTree);
        QDBusConnection::disconnectFromBus("power-server-test");
    }
    QString introspect(const QString&) const override { return { }; }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override
    {
        if (message.interface() != "org.freedesktop.DBus.Properties" || message.member() != "GetAll") {
            ++unexpected;
            connection.send(message.createErrorReply(QDBusError::UnknownMethod, "Read-only fixture"));
        } else if (message.path() == "/org/freedesktop/UPower"
            && message.arguments() == QVariantList { "org.freedesktop.UPower" }) {
            ++rootReads;
            if (deferRoot) {
                deferred = message;
                oldRoot = root;
            } else
                connection.send(message.createReply(root));
        } else if (message.path() == "/org/freedesktop/UPower/devices/DisplayDevice"
            && message.arguments() == QVariantList { "org.freedesktop.UPower.Device" }) {
            connection.send(message.createReply(display));
        } else {
            ++unexpected;
            connection.send(message.createErrorReply(QDBusError::UnknownObject, "Unexpected power object"));
        }
        return true;
    }
    void change(bool device, const QVariantMap& changed, const QStringList& invalidated = { })
    {
        auto& properties = device ? display : root;
        for (auto it = changed.cbegin(); it != changed.cend(); ++it)
            properties.insert(it.key(), it.value());
        auto message = QDBusMessage::createSignal(
            device ? "/org/freedesktop/UPower/devices/DisplayDevice" : "/org/freedesktop/UPower",
            "org.freedesktop.DBus.Properties", "PropertiesChanged");
        message << (device ? "org.freedesktop.UPower.Device" : "org.freedesktop.UPower") << changed
                << invalidated;
        bus.send(message);
    }
};
class PowerTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        auto config = KSharedConfig::openConfig("powerdevilrc");
        KConfigGroup(config, "BatteryManagement").writeEntry("BatteryLowLevel", 30);
        QVERIFY(config->sync());
    }
    void transitionsThresholdAndReconnect()
    {
        PowerServer server;
        PowerSource source(QDBusConnection::sessionBus());
        QSignalSpy plugged(&source, &PowerSource::powerConnected);
        QTRY_VERIFY(source.state().known);
        QVERIFY(!source.state().onBattery);
        QCOMPARE(plugged.size(), 0); // Starting while plugged in is not an event.
        server.change(false, { { "OnBattery", true } });
        QTRY_VERIFY(source.state().onBattery);
        QVERIFY(!source.state().lowBattery);
        server.change(true, { { "Percentage", 30.0 } });
        QTRY_VERIFY(source.state().lowBattery);
        server.change(true, { { "Percentage", 31.0 } });
        QTRY_VERIFY(!source.state().lowBattery);
        server.change(true, { { "Percentage", 20.0 } });
        QTRY_VERIFY(source.state().lowBattery);
        auto config = KSharedConfig::openConfig("powerdevilrc");
        KConfigGroup(config, "BatteryManagement").writeEntry("BatteryLowLevel", 10);
        QVERIFY(config->sync());
        source.reloadThreshold();
        QVERIFY(!source.state().lowBattery);
        server.change(false, { { "OnBattery", false } });
        QTRY_COMPARE(plugged.size(), 1);
        QVERIFY(!source.state().lowBattery);
        server.change(false, { { "OnBattery", false } });
        server.change(true, { { "Percentage", 5.0 } });
        QTRY_VERIFY(!source.state().onBattery);
        QVERIFY(!source.state().lowBattery); // Low charge on AC does not pause.
        QCOMPARE(plugged.size(), 1);
        server.change(false, { { "OnBattery", true } });
        QTRY_VERIFY(source.state().lowBattery);
        QVERIFY(server.bus.unregisterService("org.freedesktop.UPower"));
        QTRY_VERIFY(source.status().contains("unavailable"));
        QVERIFY(source.state().onBattery && source.state().lowBattery);
        server.root["OnBattery"] = false;
        QVERIFY(server.bus.registerService("org.freedesktop.UPower"));
        QTRY_VERIFY(!source.state().onBattery);
        QCOMPARE(plugged.size(), 1); // Reconnection establishes a new baseline.
        QCOMPARE(server.unexpected, 0);
    }
    void staleSnapshotAndInvalidatedProperties()
    {
        PowerServer server;
        server.deferRoot = true;
        PowerSource source(QDBusConnection::sessionBus());
        QSignalSpy plugged(&source, &PowerSource::powerConnected);
        QTRY_COMPARE(server.rootReads, 1);
        QVERIFY(!source.state().known);
        server.change(false, { { "OnBattery", true } });
        QTRY_VERIFY(source.state().onBattery);
        server.deferRoot = false;
        server.bus.send(server.deferred.createReply(server.oldRoot));
        QTRY_COMPARE(server.rootReads, 2);
        QTRY_VERIFY(source.state().onBattery);
        QCOMPARE(plugged.size(), 0);
        server.root["OnBattery"] = false;
        server.change(false, { }, { "OnBattery" });
        QTRY_COMPARE(plugged.size(), 1);
        QVERIFY(!source.state().onBattery);
        QCOMPARE(server.unexpected, 0);
    }
};
int main(int argc, char** argv)
{
    QTemporaryDir config;
    if (!config.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QCoreApplication app(argc, argv);
    PowerTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_power.moc"
