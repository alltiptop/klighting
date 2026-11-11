// SPDX-License-Identifier: GPL-3.0-or-later
#include "application_settings.h"
#include "component_info.h"
#include "kwin_capture_source.h"
#include "kwin_desktop_source.h"
#include "kwin_sources.h"
#include "lighting_ipc.h"
#include "lighting_service.h"
#include "microphone_source.h"
#include <KConfig>
#include <KConfigGroup>
#include <QDBusConnectionInterface>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <linux/input-event-codes.h>
#include <utility>
using namespace Ambient;
namespace
{
PresetCollection collection()
{
    auto c = defaultCollection("test-strip");
    c.devices = QList<DeviceConfiguration> { { "test-strips", "one", "test-strip", "Strip", true } };
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
    using DesktopSource::publish;
    void update(const QString& window, const ModifierState& keys, bool locked)
    {
        publish({ ProviderState::Available, window, keys, locked });
    }
    void unavailable() { publish({ ProviderState::Unavailable }); }
};
class Capture : public CaptureSource
{
public:
    CaptureState current;
    int fps = -1;
    QVariantMap options;
    CaptureState state() const override { return current; }
    void requestCapture(int requested, const QVariantMap& settings) override
    {
        fps = requested;
        options = settings;
    }
    void update(const CaptureState& state)
    {
        current = state;
        Q_EMIT changed();
    }
};
class Microphone : public MicrophoneSource
{
public:
    bool enabled = false;
    QList<bool> transitions;
    MicrophoneState current;
    MicrophoneState state() const override { return current; }
    void setEnabled(bool requested) override
    {
        if (enabled == requested)
            return;
        enabled = requested;
        transitions.append(requested);
        current = { };
        Q_EMIT changed();
    }
    void update(const MicrophoneState& snapshot)
    {
        current = snapshot;
        Q_EMIT changed();
    }
};
class KWin : public QDBusVirtualObject
{
public:
    QString connectionName;
    QDBusConnection bus;
    QStringList effects;
    bool legacy = false;
    bool desktopPresent = true, loadSucceeds = false;
    int loads = 0;
    int fps = -1;
    QVariantMap options;
    bool holdDesktop = false;
    QList<QDBusMessage> desktopRequests;
    int desktopSnapshots = 0, captureSnapshots = 0;
    int desktopConfigurations = 0, captureConfigurations = 0;
    int failDesktopConfigurations = 0, failCaptureConfigurations = 0;
    bool holdConfigurations = false;
    QList<QDBusMessage> configurationRequests;
    QByteArray tracked;
    QVariantMap desktop { { "protocol", Ipc::Version }, { "locked", false }, { "windowClass", "editor" },
        { "pressed", QByteArray((KEY_MAX + 8) / 8, '\0') }, { "modifiers", 0u }, { "capsLock", false } };
    QVariantMap capture { { "protocol", Ipc::Version }, { "available", true }, { "status", "Available" } };
    explicit KWin(QString name = QStringLiteral("fake-kwin"))
        : connectionName(std::move(name))
        , bus(QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), connectionName))
    {
        if (!bus.registerVirtualObject("/", this, QDBusConnection::SubPath)
            || !bus.registerService("org.kde.KWin"))
            qFatal("Cannot start fake KWin");
    }
    ~KWin() override
    {
        bus.unregisterService("org.kde.KWin");
        bus.unregisterObject("/", QDBusConnection::UnregisterTree);
        QDBusConnection::disconnectFromBus(connectionName);
    }
    QString introspect(const QString&) const override { return { }; }
    bool handleMessage(const QDBusMessage& m, const QDBusConnection& b) override
    {
        if (m.member() == "Get" && m.path() == "/Effects")
            b.send(m.createReply(QVariant::fromValue(QDBusVariant(effects))));
        else if (desktopPresent && m.path() == Ipc::DesktopPath && m.member() == "Snapshot") {
            ++desktopSnapshots;
            if (holdDesktop)
                desktopRequests.append(m);
            else
                b.send(m.createReply(QVariant::fromValue(desktop)));
        } else if (m.path() == "/Plugins" && m.member() == "LoadPlugin") {
            ++loads;
            const bool allowed = IntegrationStartupGuard().consume(bus.baseService()).isEmpty();
            desktopPresent = allowed && loadSucceeds;
            b.send(m.createReply(desktopPresent));
        } else if (desktopPresent && m.path() == Ipc::DesktopPath && m.member() == "Configure") {
            ++desktopConfigurations;
            if (failDesktopConfigurations > 0) {
                --failDesktopConfigurations;
                b.send(m.createErrorReply(QDBusError::Failed, "Temporary desktop configuration failure"));
                return true;
            }
            tracked = m.arguments()[0].toByteArray();
            if (holdConfigurations)
                configurationRequests.append(m);
            else
                b.send(m.createReply());
        } else if (!legacy && m.path() == Ipc::CapturePath && m.member() == "Snapshot") {
            ++captureSnapshots;
            b.send(m.createReply(QVariant::fromValue(capture)));
        } else if (!legacy && m.path() == Ipc::CapturePath && m.member() == "Configure") {
            ++captureConfigurations;
            if (failCaptureConfigurations > 0) {
                --failCaptureConfigurations;
                b.send(m.createErrorReply(QDBusError::Failed, "Temporary capture configuration failure"));
                return true;
            }
            fps = m.arguments()[0].toInt();
            options = qdbus_cast<QVariantMap>(m.arguments()[1]);
            if (holdConfigurations)
                configurationRequests.append(m);
            else
                b.send(m.createReply());
        } else
            b.send(m.createErrorReply(QDBusError::UnknownMethod, "Not implemented"));
        return true;
    }
    void currentProviders()
    {
        effects = { "ambientled" };
        desktop.insert(componentInfo(Component::Desktop));
        desktop["instanceId"] = "desktop-first";
        capture.insert(componentInfo(Component::Capture));
        capture["instanceId"] = "capture-first";
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
        const auto root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QVERIFY(QDir(root).removeRecursively());
        QVERIFY(QDir().mkpath(root));
        qApp->setProperty("test.devices", QStringList { "one" });
        for (const auto* key : { "uniform", "colors", "frames", "enabled" })
            qApp->setProperty((QString("test.one.") + key).toUtf8(), QVariant());
    }
    void microphoneObservationIsOnDemandAndFailureDoesNotStopLighting()
    {
        auto c = collection();
        QVERIFY(persist(c));
        auto microphone = std::make_unique<Microphone>();
        auto* input = microphone.get();
        auto capture = std::make_unique<Capture>();
        auto* screen = capture.get();
        LightingService service({ }, { MULTI_DRIVER_DIR },
            { .capture = std::move(capture), .microphone = std::move(microphone) });
        QVERIFY(service.Status()["running"].toBool());
        QVERIFY(!input->enabled);
        QCOMPARE(screen->fps, 0);
        EffectPreset effect;
        effect.id = "microphone";
        effect.type = "microphone";
        effect.microphone.color = Qt::green;
        c.effects.append(effect);
        c.presets[0].effects[DefaultAssignment].append({ effect.id });
        QVERIFY(persist(c));
        QVERIFY(service.Reload().isEmpty());
        QVERIFY(input->enabled);
        QCOMPARE(color(), "#ff0000");
        const MicrophoneState active { true, true, false, "fixture", "synthetic-default", "Test input", { } };
        input->update(active);
        QCOMPARE(color(), "#00ff00");
        const auto status = service.Status();
        QCOMPARE(status["microphoneBackend"].toString(), "fixture");
        QVERIFY(status["microphoneAvailable"].toBool());
        QVERIFY(status["microphoneMuteKnown"].toBool());
        QVERIFY(!status["microphoneMuted"].toBool());
        QCOMPARE(status["microphoneSourceName"].toString(), "synthetic-default");
        QCOMPARE(status["microphoneDescription"].toString(), "Test input");
        input->update({ false, false, false, "fixture", { }, { }, "Test server disconnected" });
        QCOMPARE(color(), "#ff0000");
        QVERIFY(service.Status()["running"].toBool());
        QVERIFY(!service.Status()["microphoneAvailable"].toBool());
        QVERIFY(!service.Status()["microphoneMuteKnown"].toBool());
        QCOMPARE(service.Status()["microphoneReason"].toString(), "Test server disconnected");
        QCOMPARE(service.Status()["error"].toString(), status["error"].toString());
        QCOMPARE(screen->fps, 0);
        input->update(active);
        QCOMPARE(color(), "#00ff00");
        c.presets[0].effects[DefaultAssignment].removeLast();
        QVERIFY(persist(c));
        QVERIFY(service.Reload().isEmpty());
        QVERIFY(!input->enabled);
        QCOMPARE(color(), "#ff0000");
        c.presets[0].effects[DefaultAssignment].append({ effect.id });
        QVERIFY(persist(c));
        QVERIFY(service.Reload().isEmpty());
        QVERIFY(input->enabled);
        QCOMPARE(color(), "#ff0000"); // Reactivation waits for a fresh source snapshot.
        QCOMPARE(input->transitions, (QList<bool> { true, false, true }));
        input->update(active);
        QCOMPARE(color(), "#00ff00");
    }
    void microphoneFollowsSelectedProfileAndLockLifecycle()
    {
        auto c = collection();
        auto editor = c.presets[0];
        editor.id = "editor";
        EffectPreset effect;
        effect.id = "microphone";
        effect.type = "microphone";
        effect.microphone.color = Qt::green;
        c.effects.append(effect);
        editor.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::blue) }, { effect.id } };
        c.presets.append(editor);
        c.rules = { { "Editor", "editor", "editor", true } };
        QVERIFY(persist(c));
        auto desktop = std::make_unique<Desktop>();
        auto* session = desktop.get();
        auto microphone = std::make_unique<Microphone>();
        auto* input = microphone.get();
        LightingService service({ }, { MULTI_DRIVER_DIR },
            { .desktop = std::move(desktop), .microphone = std::move(microphone) });
        QVERIFY(!input->enabled); // Unknown lock state cannot activate observation.
        session->update("terminal", { }, false);
        QTRY_COMPARE(color(), "#ff0000");
        QVERIFY(!input->enabled);
        session->update("editor", { }, false);
        QVERIFY(input->enabled);
        QCOMPARE(color(), "#0000ff");
        input->update({ true, true, false });
        QCOMPARE(color(), "#00ff00");
        session->update("terminal", { }, false);
        QVERIFY(!input->enabled);
        QCOMPARE(color(), "#ff0000");
        session->update("editor", { }, false);
        QVERIFY(input->enabled);
        QCOMPARE(color(), "#0000ff");
        input->update({ true, true, false });
        QCOMPARE(color(), "#00ff00");
        session->update("editor", { }, true);
        QVERIFY(!input->enabled);
        QVERIFY(!service.Status()["running"].toBool());
        session->update("editor", { }, false);
        QVERIFY(input->enabled);
        QTRY_COMPARE(color(), "#0000ff");
        input->update({ true, true, true });
        QCOMPARE(color(), "#0000ff");
        input->update({ true, true, false });
        QCOMPARE(color(), "#00ff00");
    }
    void serviceMigratesCaptureSettingsAndKeepsWorkingAfterInvalidReload()
    {
        auto c = collection();
        QVERIFY(persist(c));
        KConfig legacy(QStringLiteral("kwinrc"), KConfig::NoGlobals);
        KConfigGroup old(&legacy, "Effect-ambientled");
        old.writeEntry("ScreenName", "test-display");
        old.writeEntry("Region", "bottom");
        old.writeEntry("BottomPercent", 40);
        old.writeEntry("Brightness", 0.4);
        old.writeEntry("Saturation", 0.5);
        QVERIFY(legacy.sync());
        auto capture = std::make_unique<Capture>();
        auto* source = capture.get();
        LightingService service({ }, { MULTI_DRIVER_DIR }, { .capture = std::move(capture) });
        const QVariantMap expected { { "screen", "test-display" }, { "bottomOnly", true },
            { "bottomPercent", 40 } };
        QCOMPARE(source->options, expected);
        QCOMPARE(color(), "#ff0000");
        QFile file(applicationSettingsPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved = file.readAll();
        file.close();
        const auto root = QJsonDocument::fromJson(saved).object();
        QVERIFY(root.value("legacyImported").toBool());
        QCOMPARE(root.value("values").toObject().value("Brightness").toDouble(), 0.4);
        QCOMPARE(root.value("values").toObject().value("Saturation").toDouble(), 0.5);
        old.writeEntry("ScreenName", "changed-legacy");
        QVERIFY(legacy.sync());
        QVERIFY(service.Reload().isEmpty());
        QCOMPARE(source->options, expected);
        c.presets[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::green) } };
        QVERIFY(persist(c));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{");
        file.close();
        QVERIFY(!service.Reload().isEmpty());
        QVERIFY(service.Status()["running"].toBool());
        QCOMPARE(source->options, expected);
        QCOMPARE(color(), "#ff0000");
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(saved), saved.size());
        file.close();
        QVERIFY(service.Reload().isEmpty());
        QCOMPARE(source->options, expected);
        QCOMPARE(color(), "#00ff00");
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
    void sharedLayersReloadAndControlBrightnessOnlyKeyboard()
    {
        const auto reset = qScopeGuard([] { qApp->setProperty("test.one.brightnessSteps", QVariant()); });
        qApp->setProperty("test.one.brightnessSteps", 3);
        auto c = collection();
        EffectPreset base, dim;
        base.id = "full";
        base.type = "brightness";
        base.brightness = 100;
        dim = base;
        dim.id = "dim";
        dim.brightness = 33;
        c.effects = { base, dim };
        auto& scene = c.presets[0];
        DeviceProfile profile = scene;
        profile.id = "mono-profile";
        profile.targetDevice = "test-strips:one";
        profile.defaultEffectId.clear();
        profile.effects[DefaultAssignment] = { { "full" } };
        profile.layers = { { "Shift", { "shift" }, { } }, { "Shift+A", { "shift", "key:30" }, { } } };
        profile.layers[0].effects[DefaultAssignment] = { { "dim" } };
        scene.multiDevice = true;
        scene.devices = { profile };
        QVERIFY(mergePresetLayers(scene));
        QVERIFY(persist(c)); // The service reads the same version 7 files saved by the editor.
        auto desktop = std::make_unique<Desktop>();
        auto* input = desktop.get();
        LightingService service({ }, { MULTI_DRIVER_DIR }, { .desktop = std::move(desktop) });
        QVERIFY(service.Status()["error"].toString().isEmpty());
        input->update("editor", { }, false);
        QTRY_VERIFY(service.Status()["running"].toBool());
        QCOMPARE(color(), "#ffffff");
        QCOMPARE(service.Status()["captureFps"].toInt(), 0);
        QVERIFY(input->tracked.contains(KEY_A));
        input->update("editor", { { KEY_LEFTSHIFT }, Qt::ShiftModifier, false }, false);
        QCOMPARE(color(), "#555555");
        input->update("editor", { { KEY_LEFTSHIFT, KEY_A }, Qt::ShiftModifier, false }, false);
        QCOMPARE(color(), "#ffffff"); // An empty, more specific layer inherits Base.
        input->update("editor", { { KEY_LEFTSHIFT }, Qt::ShiftModifier, false }, false);
        c.effects[1].brightness = 67;
        QVERIFY(persist(c));
        QVERIFY(service.Reload().isEmpty());
        QCOMPARE(color(), "#aaaaaa");
        input->update("editor", { }, false);
        QCOMPARE(color(), "#ffffff");
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
        LightingService service({ }, { MULTI_DRIVER_DIR }, { .desktop = std::move(desktop) });
        QVERIFY(!service.Status()["running"].toBool()); // Initial lock state is not known yet.
        input->update("editor", { }, true);
        QTest::qWait(30);
        QVERIFY(!service.Status()["running"].toBool());
        input->update("editor", { }, false);
        QTRY_VERIFY(service.Status()["running"].toBool());
        QCOMPARE(color(), "#0000ff");
        QVERIFY(input->tracked.contains(KEY_RIGHTSHIFT));
        input->unavailable();
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
    void desktopFunctionsFailIndependentlyAndUnknownLockNeverUnlocks()
    {
        auto c = collection();
        KeyboardLayer shift;
        shift.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::yellow) } };
        c.presets[0].layers = { shift };
        auto editor = c.presets[0];
        editor.id = "editor";
        editor.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::blue) } };
        editor.layers[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::green) } };
        c.presets.append(editor);
        c.rules = { { "Editor", "editor", "editor", true } };
        QVERIFY(persist(c));
        auto desktop = std::make_unique<Desktop>();
        auto* input = desktop.get();
        LightingService service({ }, { MULTI_DRIVER_DIR }, { .desktop = std::move(desktop) });
        DesktopState state { ProviderState::Available, QString("editor"),
            ModifierState { { KEY_LEFTSHIFT }, Qt::ShiftModifier, false }, std::nullopt };
        input->publish(state);
        QVERIFY(!service.Status()["running"].toBool()); // Initial unknown lock is not unlocked.
        QCOMPARE(qApp->property("test.one.frames").toInt(), 0);
        state.locked = false;
        input->publish(state);
        QCOMPARE(color(), "#00ff00");
        state.modifiers.reset();
        input->publish(state);
        QCOMPARE(color(), "#0000ff"); // Application binding survives lost keys.
        QVERIFY(service.Status()["windowAvailable"].toBool());
        QVERIFY(!service.Status()["keysAvailable"].toBool());
        state.modifiers = ModifierState { { KEY_LEFTSHIFT }, Qt::ShiftModifier, false };
        state.windowClass.reset();
        input->publish(state);
        QCOMPARE(color(), "#ffff00"); // Base preset's layer survives lost window data.
        state.locked.reset();
        input->publish(state);
        QVERIFY(service.Status()["running"].toBool()); // Last known unlocked policy.
        QVERIFY(!service.Status()["lockAvailable"].toBool());
        state.locked = true;
        input->publish(state);
        QVERIFY(!service.Status()["running"].toBool());
        state.locked.reset();
        input->publish(state);
        QVERIFY(!service.Status()["running"].toBool());
        QVERIFY(service.Status()["locked"].toBool());
        input->unavailable();
        QVERIFY(!service.Status()["running"].toBool());
        state.windowClass = "editor";
        state.locked = false;
        input->publish(state);
        QCOMPARE(color(), "#00ff00");
        QVERIFY(service.Status()["running"].toBool());
    }
    void serviceUsesInjectedCaptureWithoutDependingOnKWin()
    {
        KWin kwin;
        kwin.effects = { "ambientled_dev2" }; // Only the explicit KWin factory installs its guard.
        auto c = collection();
        c.presets[0].effects[DefaultAssignment] = { { DefaultEffectId } };
        c.effects[0].ambient = { 20, 60, 0, 0, 100, 0 };
        QVERIFY(persist(c));
        auto capture = std::make_unique<Capture>();
        auto* source = capture.get();
        CaptureState state { true, true, false, true, "Ready", { } };
        source->current = state;
        LightingService service({ }, { MULTI_DRIVER_DIR }, { .capture = std::move(capture) });
        QVERIFY(service.Status()["running"].toBool());
        QCOMPARE(source->fps, 20);
        QCOMPARE(kwin.loads, 0);
        QByteArray screen(Ipc::SampleBytes, '\0');
        for (int i = 0; i < screen.size(); i += 4) {
            screen[i] = char(255);
            screen[i + 3] = char(255);
        }
        Q_EMIT source->sample(screen);
        QTRY_COMPARE(color(), "#ff0000");
        state.screenAvailable = state.ready = false;
        state.enabled = true;
        source->update(state);
        QVERIFY(service.Status()["running"].toBool());
        QVERIFY(service.Status()["captureProviderAvailable"].toBool());
        QVERIFY(!service.Status()["screenAvailable"].toBool());
        QCOMPARE(source->fps, 20); // Keep the request so the selected screen can return.
        QCOMPARE(color(), "#ff0000");
        state.screenAvailable = state.ready = true;
        source->update(state);
        for (int i = 0; i < screen.size(); i += 4) {
            screen[i] = 0;
            screen[i + 2] = char(255);
        }
        Q_EMIT source->sample(screen);
        QTRY_COMPARE(color(), "#0000ff");
    }
    void kwinDesktopSurvivesCaptureUnloadAndRejectsMalformedFields()
    {
        KWin kwin;
        KWinDesktopSource desktop;
        QString windowClass;
        int changes = 0;
        connect(&desktop, &DesktopSource::changed, this, [&] {
            windowClass = desktop.state().windowClass.value_or(QString());
            ++changes;
        });
        desktop.setTrackedKeys({ KEY_LEFTSHIFT });
        QTRY_COMPARE(windowClass, "editor");
        QTRY_VERIFY(!kwin.tracked.isEmpty());
        QVERIFY(quint8(kwin.tracked[KEY_LEFTSHIFT / 8]) & (1u << (KEY_LEFTSHIFT % 8)));
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource capture(guard);
        QTRY_VERIFY(guard->allowed());
        QVERIFY(!capture.state().ready);
        kwin.effects = { "ambientled" };
        capture.requestCapture(5, { });
        QTRY_VERIFY_WITH_TIMEOUT(capture.state().ready, 4000);
        QTRY_COMPARE(kwin.fps, 5);
        kwin.effects.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!capture.state().ready, 4000);
        QVERIFY(guard->allowed());
        kwin.desktop["windowClass"] = "game";
        kwin.publish(Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", kwin.desktop);
        QTRY_COMPARE(windowClass, "game");
        const int count = changes;
        auto invalid = kwin.desktop;
        invalid["pressed"] = QByteArray(1, '\0');
        kwin.publish(Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", invalid);
        QTest::qWait(30);
        QCOMPARE(changes, count + 1);
        QVERIFY(!desktop.state().modifiers.has_value());
        QCOMPARE(desktop.state().windowClass.value(), "game");
        QCOMPARE(desktop.state().locked, std::optional(false));
    }
    void desktopMetadataFlagsAndMalformedFieldsAreIndependent()
    {
        KWin kwin;
        kwin.desktop.insert(componentInfo(Component::Desktop));
        KWinDesktopSource desktop;
        QTRY_COMPARE(desktop.state().windowClass, std::optional(QString("editor")));
        auto send
            = [&] { kwin.publish(Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", kwin.desktop); };
        kwin.desktop["keysAvailable"] = false;
        kwin.desktop["pressed"] = QByteArray("bad");
        kwin.desktop["locked"] = true;
        send();
        QTRY_VERIFY(!desktop.state().modifiers.has_value());
        QCOMPARE(desktop.state().windowClass, std::optional(QString("editor")));
        QCOMPARE(desktop.state().locked, std::optional(true));
        kwin.desktop["windowClass"] = QString(257, 'a');
        kwin.desktop["lockAvailable"] = false;
        kwin.desktop["locked"] = false; // An unavailable field cannot unlock.
        kwin.desktop["keysAvailable"] = true;
        kwin.desktop["pressed"] = QByteArray((KEY_MAX + 8) / 8, '\0');
        send();
        QTRY_VERIFY(desktop.state().modifiers.has_value());
        QVERIFY(!desktop.state().windowClass.has_value());
        QVERIFY(!desktop.state().locked.has_value());
        kwin.desktop["windowClass"] = "editor";
        kwin.desktop["lockAvailable"] = true;
        kwin.desktop["locked"] = "false"; // Malformed data is unknown, not a false boolean.
        send();
        QTRY_VERIFY(desktop.state().windowClass.has_value());
        QVERIFY(!desktop.state().locked.has_value());
        kwin.desktop["locked"] = false;
        kwin.desktop["capabilities"] = QStringList { "desktop-state", "screen-lock" };
        send();
        QTRY_VERIFY(desktop.state().locked.has_value());
        QVERIFY(!desktop.state().windowClass.has_value());
        QVERIFY(!desktop.state().modifiers.has_value());
        QCOMPARE(desktop.state().provider, ProviderState::Available);
        QCOMPARE(kwin.loads, 0);
    }
    void desktopSnapshotCannotOverwriteNewerLockSignal()
    {
        KWin kwin;
        kwin.holdDesktop = true;
        KWinDesktopSource desktop;
        QTRY_COMPARE(kwin.desktopRequests.size(), 1);
        auto latest = kwin.desktop;
        latest["locked"] = true;
        kwin.publish(Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", latest);
        QTRY_COMPARE(desktop.state().locked, std::optional(true));
        const auto request = kwin.desktopRequests.takeFirst();
        kwin.bus.send(request.createReply(QVariant::fromValue(kwin.desktop))); // Older unlocked snapshot.
        QTest::qWait(50);
        QCOMPARE(desktop.state().locked, std::optional(true));
        QCOMPARE(desktop.state().provider, ProviderState::Available);
        QCOMPARE(kwin.loads, 0);
    }
    void captureCanBeConfiguredWhenStoppedMissingScreenOrFailed()
    {
        KWin kwin;
        kwin.effects = { "ambientled" };
        kwin.capture.insert(componentInfo(Component::Capture));
        kwin.capture.insert({ { "providerAvailable", true }, { "screenAvailable", false },
            { "enabled", false }, { "ready", false }, { "status", "Selected screen is unavailable" } });
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource source(guard);
        source.requestCapture(20, { { "screen", "missing" } });
        QTRY_VERIFY(source.state().providerAvailable);
        QVERIFY(guard->allowed());
        QVERIFY(!source.state().screenAvailable);
        QVERIFY(!source.state().ready);
        QTRY_COMPARE(kwin.fps, 20);
        QCOMPARE(kwin.options.value("screen").toString(), "missing");
        source.requestCapture(0, { { "screen", "another" } });
        QTRY_COMPARE(kwin.fps, 0);
        QCOMPARE(kwin.options.value("screen").toString(), "another");
        kwin.capture["screenAvailable"] = true;
        kwin.capture["available"] = false;
        kwin.capture["status"] = "GPU readback failed";
        QTRY_COMPARE_WITH_TIMEOUT(source.state().status, "GPU readback failed", 4000);
        QVERIFY(source.state().providerAvailable);
        QVERIFY(!source.state().ready);
        QVERIFY(guard->allowed());
        source.requestCapture(10, { { "screen", "returned" } });
        QTRY_COMPARE(kwin.fps, 10);
        QCOMPARE(kwin.options.value("screen").toString(), "returned");
        kwin.capture["ready"] = true;
        kwin.capture["enabled"] = true;
        kwin.capture["available"] = true;
        kwin.capture["status"] = "Available";
        QTRY_VERIFY_WITH_TIMEOUT(source.state().ready, 4000);
        QVERIFY(source.state().enabled);
        source.requestCapture(0, { });
        QTRY_COMPARE(kwin.fps, 0);
    }
    void runningProviderInfoReachesStatusWithoutRequiringMatchingBuilds()
    {
        KWin kwin;
        kwin.effects = { "ambientled" };
        kwin.desktop.insert(componentInfo(Component::Desktop));
        kwin.desktop["buildId"] = "desktop-other-build";
        kwin.desktop["instanceId"] = "desktop-instance";
        kwin.capture.insert(componentInfo(Component::Capture));
        kwin.capture["buildId"] = "capture-other-build";
        kwin.capture["instanceId"] = "capture-instance";
        QVERIFY(persist(collection()));
        LightingService service({ }, { MULTI_DRIVER_DIR }, kwinSources());
        QTRY_COMPARE(
            service.Status().value("desktopInfo").toMap().value("buildId").toString(), "desktop-other-build");
        QTRY_COMPARE(
            service.Status().value("captureInfo").toMap().value("instanceId").toString(), "capture-instance");
        QVERIFY(compatibleComponent(Component::Service, service.Status()));
        QTRY_VERIFY(service.Status().value("running").toBool());
        QCOMPARE(color(), "#ff0000");
        QVERIFY(!service.Status().value("desktopInfo").toMap().contains("pressed"));
        kwin.capture["capabilities"] = QStringList { "screen-sample" }; // No capture-only promise.
        QTRY_VERIFY_WITH_TIMEOUT(!service.Status().value("safeToControl").toBool(), 4000);
        QVERIFY(!service.Status().value("running").toBool());
        QCOMPARE(service.Status().value("captureInfo").toMap().value("buildId").toString(),
            "capture-other-build"); // Incompatible providers remain diagnosable.
        const int configurations = kwin.captureConfigurations;
        kwin.capture["capabilities"] = componentInfo(Component::Capture).value("capabilities");
        kwin.fps = -1;
        QTRY_VERIFY_WITH_TIMEOUT(service.Status().value("safeToControl").toBool(), 4000);
        QTRY_COMPARE(kwin.fps, 0); // Same identity after unavailability still needs configuration.
        QVERIFY(kwin.captureConfigurations > configurations);
    }
    void legacyEffectBlocksDaemonUntilUnloaded()
    {
        auto kwin = std::make_unique<KWin>();
        kwin->effects = { "ambientled" };
        kwin->legacy = true;
        QVERIFY(persist(collection()));
        LightingService service({ }, { MULTI_DRIVER_DIR }, kwinSources());
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
    void unchangedProvidersDoNotRepeatConfiguration()
    {
        KWin kwin;
        kwin.currentProviders();
        KWinDesktopSource desktop;
        desktop.setTrackedKeys({ KEY_A });
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource capture(guard);
        capture.requestCapture(20, { { "screen", "test" } });
        QTRY_COMPARE(kwin.desktopConfigurations, 1);
        QTRY_COMPARE(kwin.captureConfigurations, 1);
        QSignalSpy changes(&capture, &CaptureSource::changed);
        const auto info = capture.state().info;
        QTRY_VERIFY_WITH_TIMEOUT(kwin.desktopSnapshots >= 3 && kwin.captureSnapshots >= 3, 5000);
        QCOMPARE(kwin.desktopConfigurations, 1);
        QCOMPARE(kwin.captureConfigurations, 1);
        QCOMPARE(capture.state().info, info);
        QCOMPARE(changes.size(), 0);
        desktop.setTrackedKeys({ KEY_A });
        capture.requestCapture(20, { { "screen", "test" } });
        QTest::qWait(50);
        QCOMPARE(kwin.desktopConfigurations, 1);
        QCOMPARE(kwin.captureConfigurations, 1);
    }
    void providerMetadataIsDecodedBeforeComparison()
    {
        KWin kwin;
        kwin.currentProviders();
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource capture(guard);
        QTRY_VERIFY(capture.state().providerAvailable);
        QCOMPARE(capture.state().info.value("capabilities").metaType().id(), QMetaType::QStringList);
        QCOMPARE(capture.state().info.value("capabilities").toStringList(),
            componentInfo(Component::Capture).value("capabilities").toStringList());
    }
    void providerReplacementAndConfigurationErrorsRecover()
    {
        KWin kwin;
        kwin.currentProviders();
        kwin.failDesktopConfigurations = kwin.failCaptureConfigurations = 1;
        KWinDesktopSource desktop;
        desktop.setTrackedKeys({ KEY_A });
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource capture(guard);
        capture.requestCapture(20, { { "screen", "test" } });
        QTRY_COMPARE(kwin.desktopConfigurations, 1);
        QTRY_COMPARE(kwin.captureConfigurations, 1);
        QVERIFY(kwin.tracked.isEmpty());
        QCOMPARE(kwin.fps, -1);
        QTRY_VERIFY_WITH_TIMEOUT(kwin.desktopConfigurations == 2 && kwin.captureConfigurations == 2, 4000);
        QVERIFY(quint8(kwin.tracked[KEY_A / 8]) & (1u << (KEY_A % 8)));
        QCOMPARE(kwin.fps, 20);
        const auto owner = kwin.bus.baseService();
        kwin.tracked.clear();
        kwin.fps = -1;
        kwin.desktop["instanceId"] = "desktop-reloaded";
        kwin.capture["instanceId"] = "capture-reloaded";
        kwin.publish(Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", kwin.desktop);
        QTRY_COMPARE(kwin.desktopConfigurations, 3); // Reload signal without owner or availability change.
        QTRY_COMPARE_WITH_TIMEOUT(kwin.captureConfigurations, 3, 4000);
        QCOMPARE(kwin.bus.baseService(), owner);
        QCOMPARE(kwin.fps, 20);
        QVERIFY(!kwin.tracked.isEmpty());
    }
    void configurationChangesCoalesceWhileReplyIsPending()
    {
        KWin kwin;
        kwin.currentProviders();
        kwin.holdConfigurations = true;
        KWinDesktopSource desktop;
        desktop.setTrackedKeys({ KEY_A });
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource capture(guard);
        capture.requestCapture(20, { });
        QTRY_COMPARE(kwin.configurationRequests.size(), 2);
        for (int i = 1; i <= 10; ++i) {
            desktop.setTrackedKeys({ quint32(KEY_A + i) });
            capture.requestCapture(i, { { "screen", QString::number(i) } });
        }
        QTest::qWait(50);
        QCOMPARE(kwin.desktopConfigurations, 1);
        QCOMPARE(kwin.captureConfigurations, 1);
        kwin.holdConfigurations = false;
        for (const auto& request : std::exchange(kwin.configurationRequests, { }))
            kwin.bus.send(request.createReply());
        QTRY_COMPARE(kwin.desktopConfigurations, 2);
        QTRY_COMPARE(kwin.captureConfigurations, 2);
        QCOMPARE(kwin.fps, 10);
        QCOMPARE(kwin.options.value("screen").toString(), "10");
        QVERIFY(quint8(kwin.tracked[(KEY_A + 10) / 8]) & (1u << ((KEY_A + 10) % 8)));
    }
    void ownerReplacementIgnoresOldConfigurationReply()
    {
        KWin old;
        old.currentProviders();
        old.holdConfigurations = true;
        KWinDesktopSource desktop;
        desktop.setTrackedKeys({ KEY_A });
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource capture(guard);
        capture.requestCapture(20, { });
        QTRY_COMPARE(old.configurationRequests.size(), 2);
        QVERIFY(old.bus.unregisterService("org.kde.KWin"));
        QTRY_COMPARE(desktop.state().provider, ProviderState::Unavailable);
        KWin replacement("replacement-kwin");
        replacement.currentProviders(); // Same instance IDs, different owner.
        replacement.holdConfigurations = true;
        QVERIFY(replacement.bus.baseService() != old.bus.baseService());
        QTRY_COMPARE(replacement.configurationRequests.size(), 2);
        desktop.setTrackedKeys({ KEY_B });
        capture.requestCapture(10, { });
        for (const auto& request : old.configurationRequests)
            old.bus.send(request.createReply());
        QTest::qWait(50);
        QCOMPARE(replacement.desktopConfigurations, 1);
        QCOMPARE(replacement.captureConfigurations, 1);
        replacement.holdConfigurations = false;
        for (const auto& request : std::exchange(replacement.configurationRequests, { }))
            replacement.bus.send(request.createReply());
        QTRY_COMPARE(replacement.desktopConfigurations, 2);
        QTRY_COMPARE(replacement.captureConfigurations, 2);
        QCOMPARE(replacement.fps, 10);
        QVERIFY(quint8(replacement.tracked[KEY_B / 8]) & (1u << (KEY_B % 8)));
    }
    void oldProvidersWithoutIdentityKeepRecoveryPolling()
    {
        KWin kwin;
        kwin.effects = { "ambientled" };
        KWinDesktopSource desktop;
        desktop.setTrackedKeys({ KEY_A });
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource capture(guard);
        capture.requestCapture(20, { });
        QTRY_COMPARE(kwin.desktopConfigurations, 1);
        QTRY_COMPARE(kwin.captureConfigurations, 1);
        kwin.tracked.clear();
        kwin.fps = -1;
        QTRY_VERIFY_WITH_TIMEOUT(kwin.desktopConfigurations >= 2 && kwin.captureConfigurations >= 2, 4000);
        QCOMPARE(kwin.fps, 20);
        QVERIFY(!kwin.tracked.isEmpty());
    }
    void timedOutConfigurationIsRetriedWithoutAnInputChange()
    {
        KWin kwin;
        kwin.currentProviders();
        kwin.holdConfigurations = true;
        KWinDesktopSource desktop;
        desktop.setTrackedKeys({ KEY_A });
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource capture(guard);
        capture.requestCapture(20, { });
        QTRY_COMPARE(kwin.configurationRequests.size(), 2);
        // Leave the first calls unanswered. Their 1500 ms timeout must not mark
        // the desired state as delivered or prevent the next 2-second poll.
        kwin.holdConfigurations = false;
        QTRY_VERIFY_WITH_TIMEOUT(kwin.desktopConfigurations == 2 && kwin.captureConfigurations == 2, 4000);
        const int snapshots = kwin.captureSnapshots;
        QTRY_VERIFY_WITH_TIMEOUT(kwin.captureSnapshots > snapshots, 4000);
        QCOMPARE(kwin.desktopConfigurations, 2);
        QCOMPARE(kwin.captureConfigurations, 2);
    }
    void captureFailureDoesNotImplicitlyRetryTheGpu()
    {
        KWin kwin;
        kwin.currentProviders();
        auto guard = std::make_shared<HardwareControlGuard>();
        KWinCaptureSource capture(guard);
        capture.requestCapture(20, { });
        QTRY_COMPARE(kwin.captureConfigurations, 1);
        kwin.capture["available"] = false;
        kwin.capture["ready"] = false;
        kwin.capture["status"] = "GPU readback failed";
        QTRY_COMPARE_WITH_TIMEOUT(capture.state().status, "GPU readback failed", 4000);
        QVERIFY(guard->allowed());
        QVERIFY(capture.state().providerAvailable);
        QVERIFY(!capture.state().ready);
        QCOMPARE(kwin.captureConfigurations, 1);
        capture.requestCapture(20, { });
        QTest::qWait(50);
        QCOMPARE(kwin.captureConfigurations, 1);
    }
    void captureDestructionStopsEvenWithPendingConfiguration()
    {
        KWin kwin;
        kwin.currentProviders();
        kwin.holdConfigurations = true;
        auto guard = std::make_shared<HardwareControlGuard>();
        auto source = std::make_unique<KWinCaptureSource>(guard);
        source->requestCapture(20, { });
        QTRY_COMPARE(kwin.captureConfigurations, 1);
        source.reset();
        QTRY_COMPARE(kwin.captureConfigurations, 2);
        QCOMPARE(kwin.fps, 0);
    }
    void desktopStartupBudgetSurvivesOwnerChanges()
    {
        KWinDesktopSource source;
        QString previousOwner;
        for (int i = 0; i < IntegrationStartupGuard::MaximumAttempts; ++i) {
            auto kwin = std::make_unique<KWin>();
            kwin->desktopPresent = false;
            QVERIFY(kwin->bus.baseService() != previousOwner);
            previousOwner = kwin->bus.baseService();
            QTRY_COMPARE(kwin->loads, 1);
            QTRY_VERIFY(source.canRetry());
            kwin.reset();
            QTRY_VERIFY(!source.canRetry());
        }
        KWin kwin;
        kwin.desktopPresent = false;
        QTRY_VERIFY(source.canRetry());
        QVERIFY(source.status().contains("paused"));
        QCOMPARE(kwin.loads, 0);
        kwin.loadSucceeds = true;
        QVERIFY(source.retry().isEmpty());
        QTRY_COMPARE(kwin.loads, 1);
        QTRY_VERIFY(source.status().contains("Connected"));
        QVERIFY(!source.canRetry());
    }
    void desktopStartupBudgetSurvivesServiceRestarts()
    {
        KWin kwin;
        kwin.desktopPresent = false;
        for (int i = 0; i < IntegrationStartupGuard::MaximumAttempts; ++i) {
            KWinDesktopSource source;
            QTRY_COMPARE(kwin.loads, i + 1);
            QTRY_VERIFY(source.canRetry());
        }
        KWinDesktopSource source;
        QTRY_VERIFY(source.canRetry());
        QCOMPARE(kwin.loads, IntegrationStartupGuard::MaximumAttempts);
        QVERIFY(source.status().contains("paused"));
        kwin.loadSucceeds = true;
        QVERIFY(source.retry().isEmpty());
        QTRY_VERIFY(source.status().contains("Connected"));
        // The first healthy snapshot must not replenish the persistent budget.
        IntegrationStartupGuard guard;
        QVERIFY(guard.prepare(kwin.bus.baseService()).isEmpty());
        QVERIFY(guard.prepare(kwin.bus.baseService()).isEmpty());
        QVERIFY(!guard.prepare(kwin.bus.baseService()).isEmpty());
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
    if (!settings.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", settings.path().toUtf8());
    qputenv("XDG_STATE_HOME", settings.filePath("state").toUtf8());
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    qputenv("AMBIENT_LED_DRIVER_PATH", MULTI_DRIVER_DIR);
    QCoreApplication app(argc, argv);
    LightingServiceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_lighting_service.moc"
