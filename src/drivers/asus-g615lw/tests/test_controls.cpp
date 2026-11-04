// SPDX-License-Identifier: GPL-3.0-or-later
#include "application_settings.h"
#include "aura_protocol.h"
#include "lighting_ipc.h"
#include "service_control.h"
#include "settings_controller.h"
#include <KConfigGroup>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QPluginLoader>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <linux/input-event-codes.h>
using namespace Ambient;
#include "settings_services.h"
class FakeAsusd : public QDBusVirtualObject
{
public:
    QDBusConnection bus = QDBusConnection::connectToBus(
        qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), QStringLiteral("asusd-test"));
    AuraPower power { { { 1, true, true, false, false }, { 2, false, true, false, false } } };
    QList<AuraPower> writes;
    bool malformed = false, failRead = false, failWrite = false;
    int unexpected = 0, reads = 0;
    QString extraPath, writtenPath;
    FakeAsusd()
    {
        if (!bus.registerVirtualObject("/", this, QDBusConnection::SubPath)
            || !bus.registerService(AsusdService))
            qFatal("Cannot register isolated asusd fixture");
    }
    ~FakeAsusd() override
    {
        bus.unregisterService(AsusdService);
        bus.unregisterObject("/", QDBusConnection::UnregisterTree);
        QDBusConnection::disconnectFromBus(QStringLiteral("asusd-test"));
    }
    QString introspect(const QString&) const override { return { }; }
    QVariant powerValue() const
    {
        return malformed ? QVariant(QStringLiteral("invalid")) : QVariant::fromValue(power);
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override
    {
        if (message.interface() == "org.freedesktop.DBus.ObjectManager"
            && message.member() == "GetManagedObjects") {
            const QVariantMap props { { "DeviceType", 0u }, { "Brightness", 1u },
                { "SupportedBasicZones", QVariant::fromValue(QList<uint> { }) },
                { "SupportedBasicModes", QVariant::fromValue(QList<uint> { 0 }) },
                { "SupportedPowerZones", QVariant::fromValue(QList<uint> { 1, 2 }) },
                { "LedModeData", QVariant::fromValue(AuraEffect { }) }, { "LedPower", powerValue() } };
            ManagedObjects objects { { QDBusObjectPath("/xyz/ljones/aura/19b6_2_4"),
                { { AuraInterface, props } } } };
            if (!extraPath.isEmpty())
                objects.insert(QDBusObjectPath(extraPath), { { AuraInterface, props } });
            connection.send(message.createReply(QVariant::fromValue(objects)));
        } else if (message.interface() == "org.freedesktop.DBus.Properties" && message.member() == "Get"
            && message.arguments() == QVariantList { AuraInterface, "LedPower" }) {
            ++reads;
            connection.send(failRead ? message.createErrorReply(QDBusError::Failed, "power read failed")
                                     : message.createReply(QVariant::fromValue(QDBusVariant(powerValue()))));
        } else if (message.interface() == "org.freedesktop.DBus.Properties" && message.member() == "Set"
            && message.arguments().size() == 3 && message.arguments()[0] == AuraInterface
            && message.arguments()[1] == "LedPower") {
            const auto value = qvariant_cast<QDBusVariant>(message.arguments()[2]).variant();
            // Assert the wire contract independently of the C++ struct names.
            if (value.value<QDBusArgument>().currentSignature() != "(a(ubbbb))")
                qFatal("Incorrect LedPower wire signature");
            const auto update = qdbus_cast<AuraPower>(value);
            writtenPath = message.path();
            writes.append(update);
            if (failWrite) {
                connection.send(message.createErrorReply(QDBusError::AccessDenied, "power write denied"));
                return true;
            }
            for (const auto& updated : update.states)
                for (auto& saved : power.states)
                    if (saved.zone == updated.zone)
                        saved = updated;
            connection.send(message.createReply());
        } else {
            ++unexpected;
            connection.send(message.createErrorReply(QDBusError::UnknownMethod, "unexpected asusd call"));
        }
        return true;
    }
};

class ConfigTest : public QObject
{
    Q_OBJECT
    KSharedConfig::Ptr config = KSharedConfig::openConfig("kwinrc");
    std::unique_ptr<FakeAsusd> asusd;
    std::unique_ptr<FakeLighting> lighting;
    std::unique_ptr<SettingsController> openModule() { return std::make_unique<SettingsController>(); }
private Q_SLOTS:
    void init()
    {
        QVERIFY(QDir(presetDirectory()).removeRecursively());
        QVERIFY(QDir(QFileInfo(applicationSettingsPath()).absolutePath()).removeRecursively());
        asusd = std::make_unique<FakeAsusd>();
        lighting = std::make_unique<FakeLighting>();
        config->reparseConfiguration();
        config->deleteGroup("Effect-ambientled");
        config->deleteGroup("Plugins");
        QVERIFY(config->sync());
    }
    void cleanup()
    {
        QCOMPARE(asusd->unexpected, 0);
        asusd.reset();
        lighting.reset();
    }
    void lightbarControlsShareReadOnlyDriverOptions()
    {
        asusd->extraPath = "/xyz/ljones/aura/19b6_5_6";
        KConfigGroup(config, "Effect-ambientled").writeEntry("AuraObjectPath", asusd->extraPath);
        QVERIFY(config->sync());
        auto module = openModule();
        QTRY_VERIFY((module->lightbarAvailable() && !module->busy()));
        QVERIFY(!QFile::exists(driverSettingsPath("asus-g615lw")));
        module->setLightbarEnabled(false);
        module->save();
        QTRY_COMPARE(asusd->writes.size(), 1);
        QCOMPARE(asusd->writtenPath, asusd->extraPath);
        QTRY_VERIFY(!module->busy());
        module.reset();
        // The editor never owns migration, even when its fake service acknowledges Apply.
        QVERIFY(!QFile::exists(driverSettingsPath("asus-g615lw")));
        const auto path = driverSettingsPath("asus-g615lw");
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray content = R"({"version":1,"values":{"AuraObjectPath":"/xyz/ljones/aura/19b6_2_4"}})";
        QCOMPARE(file.write(content), content.size());
        file.close();
        asusd->power.states[1].awake = true;
        module = openModule();
        QTRY_VERIFY((module->lightbarAvailable() && !module->busy()));
        module->setLightbarEnabled(false);
        module->save();
        QTRY_COMPARE(asusd->writes.size(), 2);
        QCOMPARE(asusd->writtenPath, "/xyz/ljones/aura/19b6_2_4");
        QTRY_VERIFY(!module->busy());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
    }
    void lightbarCancelAndDefaultsDoNotWrite()
    {
        asusd->power.states[1].awake = false;
        auto module = openModule();
        QTRY_VERIFY((module->lightbarAvailable() && !module->busy()));
        QVERIFY(!module->lightbarEnabled());
        QVERIFY(!module->dirty());
        module->setSection("base");
        QVERIFY(!module->representsDefaults());
        module->defaults();
        QVERIFY(module->lightbarEnabled());
        QVERIFY(module->dirty());
        QVERIFY(module->representsDefaults());
        module.reset();
        QCOMPARE(asusd->writes.size(), 0);
        QCOMPARE(asusd->reads, 0);
        QVERIFY(!asusd->power.states[1].awake);
    }
    void lightbarApplyPreservesFreshFlagsAndKeyboard()
    {
        FakeKWin fake;
        auto module = openModule();
        QTRY_VERIFY((module->lightbarAvailable() && !module->busy()));
        module->setLightbarEnabled(false);
        // Changes made by another UI after this dialog loaded must survive.
        asusd->power.states[0] = { 1, false, false, true, true };
        asusd->power.states[1] = { 2, true, true, true, true };
        module->save();
        QTRY_VERIFY((module->lightbarAvailable() && !module->busy()));
        QCOMPARE(asusd->reads, 1);
        QCOMPARE(asusd->writes.size(), 1);
        QCOMPARE(asusd->writes[0].states.size(), 1);
        const auto state = asusd->writes[0].states.first();
        QCOMPARE(state.zone, 2u);
        QVERIFY(state.boot && !state.awake && state.sleep && state.shutdown);
        const auto keyboard = asusd->power.states[0];
        QVERIFY(!keyboard.boot && !keyboard.awake && keyboard.sleep && keyboard.shutdown);
        QVERIFY(!module->dirty());
        auto reopened = openModule();
        QTRY_VERIFY((reopened->lightbarAvailable() && !reopened->busy()));
        QVERIFY(!reopened->lightbarEnabled());
        QCOMPARE(asusd->writes.size(), 1);
        QTRY_COMPARE(lighting->reloads, 1);
        QVERIFY(fake.reconfigured.isEmpty());
    }
    void lightbarApplySurvivesClose()
    {
        FakeKWin fake;
        asusd->power.states[1].awake = false;
        auto module = openModule();
        QTRY_VERIFY((module->lightbarAvailable() && !module->busy()));
        module->setLightbarEnabled(true);
        module->save();
        module.reset();
        QTRY_COMPARE(asusd->writes.size(), 1);
        QVERIFY(asusd->power.states[1].awake);
        QTRY_COMPARE(lighting->reloads, 1);
        QVERIFY(fake.reconfigured.isEmpty());
    }
    void lightbarInvalidStateIsDisabled()
    {
        asusd->malformed = true;
        auto module = openModule();
        QTRY_VERIFY(module->lightbarStatus().startsWith("Lightbar unavailable:"));
        QVERIFY(!(module->lightbarAvailable() && !module->busy()));
        QVERIFY(!module->dirty());
        QCOMPARE(asusd->writes.size(), 0);
    }
    void refreshRecoversLightbarAndPreservesEdits()
    {
        asusd->malformed = true;
        auto module = openModule();
        QTRY_VERIFY(module->lightbarStatus().startsWith("Lightbar unavailable:"));
        asusd->malformed = false;
        module->refreshDevices();
        QTRY_VERIFY(module->lightbarAvailable() && !module->busy());
        module->setLightbarEnabled(false);
        module->refreshDevices();
        QTRY_VERIFY(!module->busy());
        QVERIFY(!module->lightbarEnabled());
        QVERIFY(module->dirty());
        QCOMPARE(asusd->writes.size(), 0);
        asusd->malformed = true;
        module->refreshDevices();
        QTRY_VERIFY(module->lightbarStatus().startsWith("Lightbar unavailable:"));
        QVERIFY(module->dirty());
        QVERIFY(!module->lightbarEnabled());
    }
    void lightbarApplyFailure_data()
    {
        QTest::addColumn<int>("failure");
        QTest::newRow("read-error") << 0;
        QTest::newRow("invalid-fresh-state") << 1;
        QTest::newRow("write-denied") << 2;
    }
    void lightbarApplyFailure()
    {
        QFETCH(int, failure);
        FakeKWin fake;
        auto module = openModule();
        QTRY_VERIFY((module->lightbarAvailable() && !module->busy()));
        asusd->failRead = failure == 0;
        asusd->malformed = failure == 1;
        asusd->failWrite = failure == 2;
        module->setLightbarEnabled(false);
        module->save();
        QTRY_VERIFY((module->lightbarAvailable() && !module->busy()));
        QVERIFY(module->dirty());
        QVERIFY(module->lightbarStatus().startsWith("Could not save lightbar setting:"));
        QCOMPARE(asusd->writes.size(), failure == 2 ? 1 : 0);
        QVERIFY(asusd->power.states[1].awake);
        QTRY_COMPARE(lighting->reloads, 1);
        QVERIFY(fake.reconfigured.isEmpty());
    }
};
int main(int argc, char** argv)
{
    // Never let the configuration plugin reach the real system bus in tests.
    const auto testBus = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    if (testBus.isEmpty())
        return 1;
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", testBus);
    registerAuraTypes();
    QTemporaryDir directory;
    if (!directory.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", directory.path().toUtf8());
    qputenv("XDG_CACHE_HOME", (directory.path() + "/cache").toUtf8());
    qputenv("XDG_DATA_HOME", (directory.path() + "/data").toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ConfigTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_controls.moc"
