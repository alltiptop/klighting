// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_desktop_source.h"
#include "lighting_ipc.h"
#include "lighting_service.h"
#include <QDBusConnectionInterface>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <linux/input-event-codes.h>
using namespace Ambient;
namespace
{
PresetCollection collection()
{
    auto c = defaultCollection("test-strip");
    c.devices
        = QList<DeviceConfiguration> { { "test-strips", "one", "test-strip", "Strip", "default", true } };
    c.presets[0].layers.clear();
    c.presets[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::red) } };
    return c;
}
QString color()
{
    const auto strip = qApp->property("test.one.colors").toList();
    return strip.isEmpty() ? qApp->property("test.one.uniform").toString() : strip.first().toString();
}
bool persist(const PresetCollection& c)
{
    QString error;
    const bool saved = savePresets(c, &error);
    if (!saved)
        qWarning() << error;
    return saved;
}
class Desktop : public DesktopSource
{
public:
    void setTrackedKeys(const QSet<quint32>& keys) override { tracked = keys; }
    QString status() const override { return "Test desktop"; }
    QSet<quint32> tracked;
};
class KWin : public QDBusVirtualObject
{
public:
    QDBusConnection bus
        = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), "fake-kwin");
    QStringList effects;
    bool legacy = false;
    int fps = -1;
    QByteArray tracked;
    QVariantMap desktop { { "protocol", Ipc::Version }, { "locked", false }, { "windowClass", "editor" },
        { "pressed", QByteArray((KEY_MAX + 8) / 8, '\0') }, { "modifiers", 0u }, { "capsLock", false } };
    KWin()
    {
        if (!bus.registerVirtualObject("/", this, QDBusConnection::SubPath)
            || !bus.registerService("org.kde.KWin"))
            qFatal("Cannot start fake KWin");
    }
    ~KWin() override
    {
        bus.unregisterService("org.kde.KWin");
        bus.unregisterObject("/", QDBusConnection::UnregisterTree);
        QDBusConnection::disconnectFromBus("fake-kwin");
    }
    QString introspect(const QString&) const override { return { }; }
    bool handleMessage(const QDBusMessage& m, const QDBusConnection& b) override
    {
        if (m.member() == "Get" && m.path() == "/Effects")
            b.send(m.createReply(QVariant::fromValue(QDBusVariant(effects))));
        else if (m.path() == Ipc::DesktopPath && m.member() == "Snapshot")
            b.send(m.createReply(QVariant::fromValue(desktop)));
        else if (m.path() == Ipc::DesktopPath && m.member() == "Configure") {
            tracked = m.arguments()[0].toByteArray();
            b.send(m.createReply());
        } else if (!legacy && m.path() == Ipc::CapturePath && m.member() == "Snapshot")
            b.send(m.createReply(QVariant::fromValue(QVariantMap {
                { "protocol", Ipc::Version }, { "available", true }, { "status", "Available" } })));
        else if (!legacy && m.path() == Ipc::CapturePath && m.member() == "Configure") {
            fps = m.arguments()[0].toInt();
            b.send(m.createReply());
        } else
            b.send(m.createErrorReply(QDBusError::UnknownMethod, "Not implemented"));
        return true;
    }
    void publish(const char* path, const char* interface, const char* member, const QVariant& value)
    {
        auto m = QDBusMessage::createSignal(path, interface, member);
        m << value;
        bus.send(m);
    }
};
}
class LightingServiceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        QVERIFY(QDir(presetDirectory()).removeRecursively());
        qApp->setProperty("test.devices", QStringList { "one" });
        for (const auto* key : { "uniform", "colors", "frames", "enabled" })
            qApp->setProperty((QString("test.one.") + key).toUtf8(), QVariant());
    }
    void staticLayersAndApplicationRulesWithoutCapture()
    {
        auto c = collection();
        auto editor = c.presets[0];
        editor.id = "editor";
        editor.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::blue) } };
        KeyboardLayer shift;
        shift.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::green) } };
        editor.layers = { shift };
        c.presets.append(editor);
        c.rules = { { "Editor", "editor", "editor", true } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        runtime.configure(c);
        runtime.setEnabled(true);
        QCOMPARE(color(), "#ff0000");
        QCOMPARE(runtime.captureFps(), 0);
        runtime.setDesktop("editor", { });
        QCOMPARE(color(), "#0000ff");
        runtime.setDesktop("editor", { { KEY_LEFTSHIFT }, Qt::ShiftModifier, false });
        QCOMPARE(color(), "#00ff00");
        runtime.setLocked(true);
        QVERIFY(!qApp->property("test.one.enabled").toBool());
        runtime.setLocked(false);
        QVERIFY(qApp->property("test.one.enabled").toBool());
        runtime.setDesktop({ }, { });
        QCOMPARE(color(), "#ff0000");
    }
    void ambientLossKeepsOutputAndPowerPolicyStopsCapture()
    {
        auto c = collection();
        c.presets[0].effects[DefaultAssignment] = { { DefaultEffectId } };
        c.effects[0].ambient = { 20, 60, 0, 0, 100, 0 };
        c.effects[0].pauseOnBattery = true;
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        runtime.configure(c);
        runtime.setEnabled(true);
        QCOMPARE(runtime.captureFps(), 20);
        QByteArray screen(Ipc::SampleBytes, '\0');
        for (int i = 0; i < screen.size(); i += 4) {
            screen[i] = char(255);
            screen[i + 3] = char(255);
        }
        runtime.sample(screen);
        QCOMPARE(runtime.status()["samples"].toInt(), 0);
        runtime.setCaptureAvailable(true);
        runtime.sample("short");
        QCOMPARE(runtime.status()["samples"].toInt(), 0);
        runtime.sample(screen);
        QTRY_COMPARE(color(), "#ff0000");
        runtime.setCaptureAvailable(false);
        QVERIFY(runtime.running());
        QCOMPARE(color(), "#ff0000");
        runtime.setPowerState({ true, true, false });
        QCOMPARE(runtime.captureFps(), 0);
        QCOMPARE(color(), "#ff0000");
        runtime.setPowerState({ });
        QCOMPARE(runtime.captureFps(), 20);
    }
    void notificationAnimationWorksWithoutScreenCapture()
    {
        auto c = collection();
        EffectPreset notification;
        notification.id = "notify";
        notification.type = "notifications";
        notification.notification.color = Qt::green;
        notification.notification.pattern = "blink";
        notification.notification.period = 100;
        notification.notification.cycles = 1;
        c.effects.append(notification);
        c.presets[0].effects[DefaultAssignment].append({ "notify" });
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        runtime.configure(c);
        runtime.setEnabled(true);
        QCOMPARE(runtime.captureFps(), 0);
        QVERIFY(runtime.notificationsEnabled());
        QTest::qWait(150); // Idle time must not instantly expire the new pulse.
        runtime.notify({ });
        QCOMPARE(color(), "#00ff00");
        QTRY_COMPARE(color(), "#ff0000");
    }
    void serviceUsesGenericDesktopAndReloadsWithoutKWin()
    {
        auto c = collection();
        auto editor = c.presets[0];
        editor.id = "editor";
        editor.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::blue) } };
        c.presets.append(editor);
        c.rules = { { "Editor", "editor", "editor", true } };
        QVERIFY(persist(c));
        auto desktop = std::make_unique<Desktop>();
        auto* input = desktop.get();
        LightingService service({ }, { MULTI_DRIVER_DIR }, std::move(desktop));
        QVERIFY(!service.Status()["running"].toBool()); // Initial lock state is not known yet.
        Q_EMIT input->stateChanged("editor", { }, true);
        QTest::qWait(30);
        QVERIFY(!service.Status()["running"].toBool());
        Q_EMIT input->stateChanged("editor", { }, false);
        QTRY_VERIFY(service.Status()["running"].toBool());
        QCOMPARE(color(), "#0000ff");
        QVERIFY(input->tracked.contains(KEY_RIGHTSHIFT));
        Q_EMIT input->unavailable();
        QCOMPARE(color(), "#ff0000");
        c.presets[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::green) } };
        QVERIFY(persist(c));
        QVERIFY(service.Reload().isEmpty());
        QCOMPARE(color(), "#00ff00");
        QFile broken(presetDirectory() + "/settings-v2.json");
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{");
        broken.close();
        QVERIFY(!service.Reload().isEmpty());
        QVERIFY(service.Status()["running"].toBool());
        QCOMPARE(color(), "#00ff00");
    }
    void kwinDesktopSurvivesCaptureUnloadAndRejectsMalformedState()
    {
        KWin kwin;
        KWinDesktopSource desktop;
        QString windowClass;
        int changes = 0;
        connect(&desktop, &DesktopSource::stateChanged, this, [&](const QString& name) {
            windowClass = name;
            ++changes;
        });
        desktop.setTrackedKeys({ KEY_LEFTSHIFT });
        QTRY_COMPARE(windowClass, "editor");
        QTRY_VERIFY(!kwin.tracked.isEmpty());
        QVERIFY(quint8(kwin.tracked[KEY_LEFTSHIFT / 8]) & (1u << (KEY_LEFTSHIFT % 8)));
        KWinCaptureSource capture;
        QTRY_VERIFY(capture.safeToControl());
        QVERIFY(!capture.captureAvailable());
        kwin.effects = { "ambientled" };
        capture.requestCapture(5, { });
        QTRY_VERIFY_WITH_TIMEOUT(capture.captureAvailable(), 4000);
        QTRY_COMPARE(kwin.fps, 5);
        kwin.effects.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!capture.captureAvailable(), 4000);
        QVERIFY(capture.safeToControl());
        kwin.desktop["windowClass"] = "game";
        kwin.publish(Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", kwin.desktop);
        QTRY_COMPARE(windowClass, "game");
        const int count = changes;
        auto invalid = kwin.desktop;
        invalid["pressed"] = QByteArray(1, '\0');
        kwin.publish(Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", invalid);
        QTest::qWait(30);
        QCOMPARE(changes, count);
    }
    void legacyEffectBlocksDaemonUntilUnloaded()
    {
        auto kwin = std::make_unique<KWin>();
        kwin->effects = { "ambientled" };
        kwin->legacy = true;
        QVERIFY(persist(collection()));
        LightingService service({ }, { MULTI_DRIVER_DIR });
        QTRY_VERIFY(service.Status()["capture"].toString().contains("Old ambientled"));
        QVERIFY(!service.Status()["running"].toBool());
        QCOMPARE(qApp->property("test.one.frames").toInt(), 0);
        kwin->effects.clear();
        QTRY_VERIFY_WITH_TIMEOUT(service.Status()["running"].toBool(), 4000);
        QCOMPARE(color(), "#ff0000");
        kwin.reset();
        QTRY_VERIFY(service.Status()["desktop"].toString().contains("unavailable"));
        QVERIFY(service.Status()["running"].toBool());
    }
    void daemonLifecycleWithoutKWin()
    {
        QVERIFY(persist(collection()));
        QProcess daemon;
        daemon.start(DAEMON_PATH, { });
        QVERIFY(daemon.waitForStarted());
        QTRY_VERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered(Ipc::Service).value());
        QProcess duplicate;
        duplicate.start(DAEMON_PATH, { });
        QVERIFY(duplicate.waitForFinished());
        QCOMPARE(duplicate.exitCode(), 1);
        QVERIFY(duplicate.readAllStandardError().contains("Cannot own"));
        QProcess status;
        status.start(DAEMON_PATH, { "--status" });
        QVERIFY(status.waitForFinished());
        QCOMPARE(status.exitCode(), 0);
        QVERIFY(status.readAllStandardOutput().contains("safeToControl"));
        daemon.terminate();
        QVERIFY(daemon.waitForFinished());
        QCOMPARE(daemon.exitStatus(), QProcess::NormalExit);
        QCOMPARE(daemon.exitCode(), 0);
    }
};
int main(int argc, char** argv)
{
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    qputenv("AMBIENT_LED_DRIVER_PATH", MULTI_DRIVER_DIR);
    QCoreApplication app(argc, argv);
    LightingServiceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_lighting_service.moc"
