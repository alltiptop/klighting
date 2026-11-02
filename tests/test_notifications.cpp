// SPDX-License-Identifier: GPL-3.0-or-later
#include "effect_palette.h"
#include "notification_source.h"
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVirtualObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <linux/input-event-codes.h>
using namespace Ambient;
class NotificationServer : public QDBusVirtualObject
{
public:
    QDBusConnection bus
        = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "notification-server-test");
    QString client;
    int registrations = 0, removals = 0;
    bool supported = true;
    NotificationServer()
    {
        if (!bus.registerService("org.freedesktop.Notifications")
            || !bus.registerVirtualObject("/org/freedesktop/Notifications", this))
            qFatal("Cannot create notification fixture");
    }
    ~NotificationServer() override
    {
        bus.unregisterService("org.freedesktop.Notifications");
        bus.unregisterObject("/org/freedesktop/Notifications");
        QDBusConnection::disconnectFromBus("notification-server-test");
    }
    QString introspect(const QString&) const override { return { }; }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override
    {
        if (!supported)
            connection.send(message.createErrorReply(QDBusError::UnknownMethod, "Unsupported"));
        else if (message.interface() == "org.kde.NotificationManager"
            && message.member() == "RegisterWatcher") {
            client = message.service();
            ++registrations;
            connection.send(message.createReply());
        } else if (message.interface() == "org.kde.NotificationManager"
            && message.member() == "UnRegisterWatcher") {
            ++removals;
            connection.send(message.createReply());
        } else
            connection.send(message.createErrorReply(QDBusError::UnknownMethod, "Unexpected"));
        return true;
    }
    QDBusMessage event(const QVariantMap& hints = { }) const
    {
        auto call = QDBusMessage::createMethodCall(
            client, "/NotificationWatcher", "org.kde.NotificationWatcher", "Notify");
        call.setArguments({ 1u, "Kate", 0u, "kate", "This title must not select a filter",
            "warning critical error", QStringList { }, hints, 5000 });
        return call;
    }
};
class NotificationsTest : public QObject
{
    Q_OBJECT
    static EffectPreset effect(const QString& pattern = "pulse")
    {
        EffectPreset p;
        p.id = "notify";
        p.name = "Notifications";
        p.type = "notifications";
        p.notification.pattern = pattern;
        p.notification.color = Qt::red;
        return p;
    }
private Q_SLOTS:
    void powerPolicyStopsOnlySelectedNotifications()
    {
        EffectPreset first, second;
        first.id = "battery-off";
        second.id = "low-off";
        first.type = second.type = "notifications";
        first.pauseOnBattery = true;
        second.pauseOnLowBattery = true;
        NotificationEffects effects;
        effects.configure({ first, second }, { first.id, second.id });
        effects.trigger({ });
        QVERIFY(effects.active());
        effects.configure({ first, second }, { first.id, second.id }, { true, true, false });
        effects.trigger({ });
        effects.advance(100);
        QVERIFY(!effects.colors().contains(first.id));
        QVERIFY(effects.colors().contains(second.id));
        effects.configure({ first, second }, { first.id, second.id }, { true, true, true });
        QVERIFY(effects.empty());
        QVERIFY(!effects.active());
        effects.configure({ first, second }, { first.id, second.id }, { true, false, false });
        QVERIFY(!effects.active());
        effects.trigger({ });
        effects.advance(100);
        QVERIFY(effects.colors().contains(first.id));
        QVERIFY(effects.colors().contains(second.id));
    }

    void metadataOnlyAndConjunctiveFilters()
    {
        NotificationOptions n;
        NotificationEvent e { "Kate", "org.kde.kate", "transfer.error", "file-error", 1 };
        QVERIFY(notificationMatches(n, e));
        n.application = "ORG.KDE.KATE";
        n.category = "transfer.error";
        n.urgency = 1;
        n.eventId = "file-error";
        QVERIFY(notificationMatches(n, e));
        n.application = "kate";
        QVERIFY(notificationMatches(n, e));
        n.application = "kat";
        QVERIFY(!notificationMatches(n, e));
        n.application.clear();
        n.urgency = 2;
        QVERIFY(!notificationMatches(n, e));
        n.urgency = -1;
        n.category = "error";
        QVERIFY(!notificationMatches(n, e));
        n.category.clear();
        n.eventId = "missing";
        QVERIFY(!notificationMatches(n, e));
        n.eventId.clear();
        QVERIFY(notificationMatches(n, e));
    }
    void powerTriggerIsSeparateFromDesktopNotifications()
    {
        auto power = effect("blink");
        power.notification.trigger = "power-connected";
        power.notification.application = "ambient-led-power";
        power.notification.eventId = "power-connected";
        power.notification.color = Qt::green;
        power.notification.cycles = 1;
        power.notification.period = 600;
        const NotificationEvent connected { { }, { }, { }, { }, 1, "power-connected" };
        QVERIFY(!notificationMatches(NotificationOptions(), connected));
        QVERIFY(!notificationMatches(power.notification, NotificationEvent()));
        QVERIFY(notificationMatches(power.notification, connected));
        QString error;
        const auto parsed = parseEffectPreset(QJsonDocument(effectPresetJson(power)).toJson(), &error);
        QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(*parsed, power);
        NotificationEffects engine;
        engine.configure({ power }, { "notify" });
        engine.trigger(connected);
        QCOMPARE(engine.colors()["notify"], QColor(Qt::green));
        engine.advance(301);
        QCOMPARE(engine.colors()["notify"].alpha(), 0);
        engine.advance(300);
        QVERIFY(engine.colors().isEmpty());
    }
    void boundedPatternsCoalesceAndRestore()
    {
        NotificationEffects engine;
        for (const auto& pattern : { "pulse", "blink", "double" }) {
            auto p = effect(pattern);
            p.notification.period = 1000;
            p.notification.cycles = 2;
            engine.configure({ p }, { p.id });
            engine.reset();
            QVERIFY(engine.colors().isEmpty());
            engine.trigger({ });
            if (p.notification.pattern == "pulse") {
                QCOMPARE(engine.colors()[p.id].alpha(), 0);
                engine.advance(500);
                QCOMPARE(engine.colors()[p.id], QColor(Qt::red));
                engine.advance(500);
                QCOMPARE(engine.colors()[p.id].alpha(), 0);
            } else if (p.notification.pattern == "blink") {
                QCOMPARE(engine.colors()[p.id], QColor(Qt::red));
                engine.advance(500);
                QCOMPARE(engine.colors()[p.id].alpha(), 0);
                engine.advance(500);
                QCOMPARE(engine.colors()[p.id], QColor(Qt::red));
            } else {
                QCOMPARE(engine.colors()[p.id], QColor(Qt::red));
                engine.advance(200);
                QCOMPARE(engine.colors()[p.id].alpha(), 0);
                engine.advance(100);
                QCOMPARE(engine.colors()[p.id], QColor(Qt::red));
                engine.advance(200);
                QCOMPARE(engine.colors()[p.id].alpha(), 0);
                engine.advance(500);
            }
            engine.trigger({ }); // Does not restart or extend an existing pattern.
            engine.advance(1000);
            QVERIFY(engine.colors().isEmpty());
            engine.trigger({ });
            QVERIFY(!engine.colors().isEmpty());
            engine.reset();
            QVERIFY(engine.colors().isEmpty());
        }
        engine.configure({ effect() }, { });
        QVERIFY(engine.empty());
        engine.trigger({ });
        QVERIFY(engine.colors().isEmpty());
        EffectPalette ambient;
        ambient.configure({ effect() }, { "notify" });
        QCOMPARE(ambient.samplingFps(), 0); // Notifications need no GPU readback.
    }
    void layersLightbarAndLiveUnderlay()
    {
        LightingPreset p;
        DeviceDescription d { p.keyboardDeviceId, "Test",
            DeviceCapability::PerKeyColor | DeviceCapability::Lightbar,
            { { 9, "F9", { } }, { 10, "F10", { } } }, { } };
        p.effects = { { 9, { { "" }, { "notify" } } }, { 10, { { "" }, { "notify" } } },
            { LightbarId, { { "" }, { "notify" } } } };
        p.layers[0].styles[9] = { KeyMode::Static, Qt::green };
        p.layers[0].styles[10] = { KeyMode::Off };
        p.effectOverrides[10] = { { "notify" } };
        ModifierState shift;
        shift.pressed = { KEY_LEFTSHIFT };
        auto assignments = deviceEffects(p, d, shift);
        QVERIFY(assignments.value(9).isEmpty());
        QCOMPARE(assignments[10], (EffectList { { "notify" } }));
        QCOMPARE(assignments[LightbarId], (EffectList { { "" }, { "notify" } }));
        auto styles = deviceKeyStyles(p, d, shift);
        auto frame = lightingColors(
            p, styles, d, { { DefaultEffectId, Qt::blue } }, assignments, { { "notify", Qt::red } });
        QCOMPARE(frame.keys, (QList<QColor> { Qt::green, Qt::red }));
        QCOMPARE(frame.uniform, QColor(Qt::red));
        frame = lightingColors(p, styles, d, { { DefaultEffectId, Qt::cyan } }, assignments, { });
        QCOMPARE(frame.keys, (QList<QColor> { Qt::green, Qt::black }));
        QCOMPARE(frame.uniform, QColor(Qt::cyan));
        p.layers[0].effects[9] = { { "notify" } };
        QVERIFY(deviceEffects(p, d, shift).contains(9));
        p.layers[0].effects[LightbarId] = { };
        QVERIFY(deviceEffects(p, d, shift).value(LightbarId).isEmpty());
        d.id = "other";
        QVERIFY(deviceEffects(p, d, shift).isEmpty());
        d.id = p.keyboardDeviceId;
        d.capabilities = DeviceCapability::Color;
        QVERIFY(deviceEffects(p, d, shift).isEmpty());
    }
    void persistenceBundlesAndInvalidReferences()
    {
        auto c = defaultCollection();
        auto n = effect();
        n.notification.application = "org.kde.kate";
        n.notification.category = "transfer.error";
        n.notification.urgency = 2;
        c.effects << n;
        auto& p = c.presets[0];
        p.effects[14] = { { "" }, { n.id } };
        p.effectOverrides[LightbarId] = { { n.id } };
        p.layers[0].effects[15] = { { n.id } };
        p.layers[0].effects[14] = { };
        QString error;
        QCOMPARE(*parseEffectPreset(QJsonDocument(effectPresetJson(n)).toJson(), &error), n);
        QVERIFY2(savePresets(c, &error), qPrintable(error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c);
        QTemporaryDir dir;
        QVERIFY(exportPreset(p, c.effects, dir.filePath("bundle.json"), &error));
        const auto bundle = importPreset(dir.filePath("bundle.json"), &error);
        QVERIFY2(bundle, qPrintable(error));
        QCOMPARE(bundle->keyboard, p);
        QCOMPARE(bundle->effects, c.effects);
        auto bad = c;
        bad.presets[0].effectOverrides[14] = { { DefaultEffectId } };
        QVERIFY(!savePresets(bad, &error));
        bad = c;
        bad.presets[0].defaultEffectId = n.id;
        QVERIFY(!savePresets(bad, &error));
        bad = c;
        bad.effects[1].notification.period = 1;
        QVERIFY(!savePresets(bad, &error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c);
        auto old = presetJson(LightingPreset());
        old["version"] = 3;
        auto keyboard = old["keyboard"].toObject();
        keyboard.remove("effects");
        keyboard.remove("effectOverrides");
        keyboard["notifications"] = QJsonObject { { "14", "notify" }, { "15", "" } };
        keyboard["notificationOverrides"] = QJsonObject { { "16", "notify" } };
        old["keyboard"] = keyboard;
        const auto migrated = parsePreset(QJsonDocument(old).toJson(), &error);
        QVERIFY2(migrated, qPrintable(error));
        QCOMPARE(migrated->effects[14], (EffectList { { "" }, { "notify" } }));
        QCOMPARE(migrated->effects[15], (EffectList { { "" } }));
        QCOMPARE(migrated->effectOverrides[16], (EffectList { { "notify" } }));
        QCOMPARE(*parsePreset(QJsonDocument(presetJson(*migrated)).toJson(), &error), *migrated);
        auto malformed = presetJson(p);
        auto k = malformed["keyboard"].toObject();
        k["effects"]
            = QJsonObject { { "14", QJsonArray { QJsonObject { { "id", "notify" }, { "opacity", 101 } } } } };
        malformed["keyboard"] = k;
        QVERIFY(!parsePreset(QJsonDocument(malformed).toJson(), &error));
    }
    void orderedAmbientNormalAndCriticalOnSameKeys()
    {
        auto normal = effect();
        normal.id = "normal";
        normal.notification.urgency = 1;
        normal.notification.color = Qt::green;
        normal.notification.pattern = "blink";
        auto critical = normal;
        critical.id = "critical";
        critical.notification.urgency = 2;
        critical.notification.color = Qt::red;
        NotificationEffects engine;
        engine.configure({ normal, critical }, { "normal", "critical" });
        LightingPreset p;
        DeviceDescription d { p.keyboardDeviceId, "Test", DeviceCapability::PerKeyColor,
            { { 9, "F9", { } }, { 10, "F10", { } } }, { } };
        p.effects[9] = { { "" }, { "normal" }, { "critical" } };
        const auto render = [&] {
            const auto frame = lightingColors(p, deviceKeyStyles(p, d, { }), d,
                { { DefaultEffectId, Qt::blue } }, deviceEffects(p, d, { }), engine.colors());
            return frame.keys.isEmpty() ? frame.uniform : frame.keys.first();
        };
        QCOMPARE(render(), QColor(Qt::blue));
        NotificationEvent event;
        event.urgency = 1;
        engine.trigger(event);
        QCOMPARE(render(), QColor(Qt::green));
        event.urgency = 2;
        engine.trigger(event);
        QCOMPARE(render(), QColor(Qt::red)); // Both active; the later critical effect wins.
        p.effects[9].swapItemsAt(1, 2);
        QCOMPARE(render(), QColor(Qt::green)); // Reordering has a visible, deterministic result.
        p.effects[9].last().opacity = 50;
        QCOMPARE(render(), QColor(127, 128, 0));
        engine.advance(10000);
        QCOMPARE(render(), QColor(Qt::blue)); // The live underlay returns after both notifications.
        // An inactive above-layer notification must never suppress a local normal notification.
        p.effects[9] = { { "" }, { "normal" } };
        p.effectOverrides[9] = { { "critical" } };
        event.urgency = 1;
        engine.trigger(event);
        QCOMPARE(render(), QColor(Qt::green));
    }

    void plasmaWatcherLifecycleAndSenderValidation()
    {
        NotificationSource source;
        QSignalSpy received(&source, &NotificationSource::received);
        source.setEnabled(true);
        QTRY_VERIFY(source.status().startsWith("Waiting"));
        NotificationServer server;
        QTRY_COMPARE(server.registrations, 1);
        QTRY_VERIFY(source.status().startsWith("Connected"));
        server.bus.asyncCall(server.event({ { "desktop-entry", "org.kde.kate" },
            { "category", "transfer.error" }, { "x-kde-eventId", "file-error" }, { "urgency", uchar(2) } }));
        QTRY_COMPARE(received.size(), 1);
        const auto event = qvariant_cast<NotificationEvent>(received.takeFirst()[0]);
        QCOMPARE(event.desktopEntry, QString("org.kde.kate"));
        QCOMPARE(event.urgency, 2);
        QCOMPARE(event.category, QString("transfer.error"));
        QCOMPARE(event.eventId, QString("file-error"));
        // A different bus client cannot inject a notification into the watcher.
        QDBusPendingCallWatcher foreign(QDBusConnection::sessionBus().asyncCall(server.event()));
        QSignalSpy finished(&foreign, &QDBusPendingCallWatcher::finished);
        QTRY_VERIFY(foreign.isFinished());
        QVERIFY(received.isEmpty());
        server.bus.asyncCall(server.event());
        QTRY_COMPARE(received.size(), 1);
        QCOMPARE(qvariant_cast<NotificationEvent>(received.takeFirst()[0]).urgency, 1);
        source.setEnabled(false);
        QTRY_COMPARE(server.removals, 1);
        QCOMPARE(source.status(), QString("Disabled"));
        source.setEnabled(true);
        QTRY_COMPARE(server.registrations, 2);
        QVERIFY(server.bus.unregisterService("org.freedesktop.Notifications"));
        QTRY_VERIFY(source.status().startsWith("Waiting"));
        QVERIFY(server.bus.registerService("org.freedesktop.Notifications"));
        QTRY_COMPARE(server.registrations, 3);
        QTRY_VERIFY(source.status().startsWith("Connected"));
        source.setEnabled(false);
        QTRY_COMPARE(server.removals, 2);
        server.supported = false;
        source.setEnabled(true);
        QTRY_VERIFY(source.status().startsWith("Notification watcher unavailable"));
        source.setEnabled(false);
    }
};
int main(int argc, char** argv)
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", dir.path().toUtf8());
    QCoreApplication app(argc, argv);
    NotificationsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_notifications.moc"
