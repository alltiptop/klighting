// SPDX-License-Identifier: GPL-3.0-or-later
#include "aura_protocol.h"
#include "lighting_ipc.h"
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
class FakeAsusd : public QDBusVirtualObject
{
public:
    QDBusConnection bus = QDBusConnection::connectToBus(
        qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), QStringLiteral("asusd-test"));
    AuraPower power { { { 1, true, true, false, false }, { 2, false, true, false, false } } };
    QList<AuraPower> writes;
    bool malformed = false, failRead = false, failWrite = false;
    int unexpected = 0, reads = 0;
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
            const ManagedObjects objects { { QDBusObjectPath("/xyz/ljones/aura/19b6_2_4"),
                { { AuraInterface, props } } } };
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

class FakeKWin : public QDBusVirtualObject
{
    Q_OBJECT
public:
    QDBusConnection bus = QDBusConnection::sessionBus();
    QStringList reconfigured;
    QStringList installed { "ambientled" }, loaded { "ambientled" };
    QStringList loads, unloads;
    bool refuseLoad = false, refuseUnload = false, overlappingLoad = false;
    int unexpected = 0;
    int windowQueries = 0;
    QVariantMap windowInfo { { "resourceClass", "org.kde.kate" }, { "resourceName", "kate" } };
    QString windowError;
    bool deferWindowReply = false;
    QDBusMessage windowRequest;
    FakeKWin()
    {
        if (!bus.registerVirtualObject("/Effects", this) || !bus.registerVirtualObject("/KWin", this)
            || !bus.registerService("org.kde.KWin"))
            qFatal("Cannot register isolated KWin test fixture");
    }
    ~FakeKWin() override
    {
        bus.unregisterService("org.kde.KWin");
        bus.unregisterObject("/Effects");
        bus.unregisterObject("/KWin");
    }
    void replyToWindowQuery()
    {
        if (windowError.isEmpty())
            bus.send(windowRequest.createReply(QVariant::fromValue(windowInfo)));
        else
            bus.send(windowRequest.createErrorReply(windowError, "Window selection failed"));
    }
    QString introspect(const QString&) const override { return { }; }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override
    {
        if (message.path() == "/KWin" && message.interface() == "org.kde.KWin"
            && message.member() == "queryWindowInfo" && message.arguments().isEmpty()) {
            ++windowQueries;
            windowRequest = message;
            if (!deferWindowReply)
                replyToWindowQuery();
        } else if (message.interface() == "org.freedesktop.DBus.Properties" && message.member() == "GetAll") {
            QVariantMap properties { { "listOfEffects", installed }, { "loadedEffects", loaded } };
            connection.send(message.createReply(QVariant::fromValue(properties)));
        } else if (message.member() == "loadEffect") {
            loads << message.arguments().first().toString();
            overlappingLoad = overlappingLoad || !loaded.isEmpty();
            if (!refuseLoad)
                loaded = loads.mid(loads.size() - 1);
            connection.send(message.createReply(!refuseLoad));
        } else if (message.member() == "unloadEffect") {
            unloads << message.arguments().first().toString();
            if (!refuseUnload)
                loaded.removeAll(unloads.last());
            connection.send(message.createReply());
        } else if (message.member() == "isEffectLoaded") {
            connection.send(message.createReply(loaded.contains(message.arguments().first().toString())));
        } else if (message.interface() == "org.freedesktop.DBus.Properties" && message.member() == "Get"
            && message.arguments() == QVariantList { "org.kde.kwin.Effects", "loadedEffects" }) {
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(loaded))));
        } else if (message.interface() == "org.kde.kwin.Effects" && message.member() == "reconfigureEffect") {
            reconfigured.append(message.arguments().first().toString());
            connection.send(message.createReply());
        } else {
            ++unexpected;
            connection.send(message.createErrorReply(QDBusError::UnknownMethod, "unexpected call"));
        }
        return true;
    }
};

class FakeLighting : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.klighting.Service1")
public:
    int reloads = 0;
    FakeLighting()
    {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.registerService(Ipc::Service)
            || !bus.registerObject(Ipc::Path, this, QDBusConnection::ExportAllSlots))
            qFatal("Cannot register isolated lighting service");
    }
    ~FakeLighting() override
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(Ipc::Path);
        bus.unregisterService(Ipc::Service);
    }
public Q_SLOTS:
    QString Reload()
    {
        ++reloads;
        return { };
    }
    QVariantMap Status() const { return { { "protocol", Ipc::Version }, { "safeToControl", true } }; }
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
    void defaultsMetadataAndPendingEdits()
    {
        QPluginLoader loader(EFFECT_PLUGIN_PATH);
        const auto metadata = loader.metaData().value("MetaData").toObject();
        QVERIFY(!metadata.contains("X-KDE-ConfigModule"));
        QCOMPARE(metadata.value("KPlugin").toObject().value("Version").toString(), QString("0.27.0"));
        SettingsController c;
        QTRY_VERIFY(c.lightbarAvailable());
        QCOMPARE(c.device().value("bodyColor").toString(), QString("#333333"));
        QCOMPARE(c.device().value("keyColor").toString(), QString("#111111"));
        QCOMPARE(c.effectPreset().value("fps").toInt(), 3);
        QCOMPARE(c.effectPreset().value("minimum").toInt(), 25);
        QCOMPARE(c.effectPreset().value("maximum").toInt(), 70);
        QVERIFY(!c.dirty());
        c.setAmbient("fps", 2);
        QVERIFY(c.dirty());
        c.setAmbient("fps", 3);
        QVERIFY(!c.dirty());
        QVERIFY(!QFile::exists(presetDirectory() + "/settings-v2.json"));
    }
    void layerCombinationsAreUniqueAndReplaceWithoutLosingAssignments()
    {
        SettingsController c;
        const auto original = c.collection();
        QVERIFY(!c.dirty());
        QCOMPARE(c.addLayer({ "shift" }), -1);
        QVERIFY(!c.error().isEmpty());
        QVERIFY(c.collection() == original);
        QVERIFY(!c.dirty());
        const int added = c.addLayer({ "ctrl-left", "shift-right" });
        QCOMPARE(added, 1);
        QVERIFY(c.error().isEmpty());
        c.assignKeys(added, { 17 }, 1, "#e06020");
        const auto assigned = c.collection();
        QCOMPARE(c.addLayer({ "shift-right", "ctrl-left" }), -1);
        QVERIFY(!c.setLayer(0, { "shift-right", "ctrl-left" }));
        QVERIFY(c.collection() == assigned);
        QVERIFY(c.setLayer(added, { "shift-right", "ctrl-left" })); // Same row is allowed.
        QVERIFY(c.setLayer(added, { "super-left", "shift-right" }));
        const auto& layer = c.collection().presets[0].layers[added];
        QCOMPARE(layer.conditions, (QStringList { "super-left", "shift-right" }));
        QCOMPARE(layer.styles, assigned.presets[0].layers[added].styles);
        QCOMPARE(layer.effects, assigned.presets[0].layers[added].effects);
        QCOMPARE(c.preset()["layers"].toList()[added].toMap()["summary"].toString(),
            QString("Meta (Left) + Shift (Right)"));
        QString error;
        QVERIFY2(savePresets(c.collection(), &error), qPrintable(error));
        SettingsController loaded;
        QCOMPARE(loaded.collection(), c.collection());
    }
    void activeLayersPreviewWithoutEditingThePreset()
    {
        SettingsController c;
        c.assignKeys(-1, { 36 }, 1, "#0000ff");
        c.assignKeys(0, { 36 }, 1, "#ff0000");
        QVERIFY(c.setLayer(0, { "shift", "key:30" }));
        const int upper = c.addLayer({ "shift-right" });
        QCOMPARE(upper, 1);
        c.assignKeys(upper, { 14 }, 1, "#00ff00");
        const auto before = c.collection();
        const bool dirty = c.dirty();
        QSignalSpy edited(&c, &SettingsController::contentChanged);
        QQuickWindow window;
        window.resize(300, 200);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.isActive());
        c.watchLayerKeys(&window);
        const auto key = [&](QEvent::Type type, quint32 code, int qtKey) {
            QKeyEvent event(type, qtKey, Qt::NoModifier, code + 8, 0, 0);
            QApplication::sendEvent(&window, &event);
        };
        QCOMPARE(c.previewLayer(), -1);
        key(QEvent::KeyPress, KEY_RIGHTSHIFT, Qt::Key_Shift);
        QCOMPARE(c.previewLayer(), upper);
        QCOMPARE(c.pressedKeys(), QVariantList { 74 });
        QSignalSpy pressedChanged(&c, &SettingsController::pressedKeysChanged);
        key(QEvent::KeyPress, KEY_W, Qt::Key_W);
        QCOMPARE(c.previewLayer(), upper); // Input indicators update even if the active layer stays the same.
        QCOMPARE(c.pressedKeys(), (QVariantList { 36, 74 }));
        QCOMPARE(pressedChanged.size(), 1);
        key(QEvent::KeyRelease, KEY_W, Qt::Key_W);
        QCOMPARE(c.pressedKeys(), QVariantList { 74 });
        QCOMPARE(c.liveKeyPreview()[36].toMap()["color"].toString(), QString("#0000ff"));
        QCOMPARE(c.liveKeyPreview()[14].toMap()["color"].toString(), QString("#00ff00"));
        key(QEvent::KeyPress, KEY_A, Qt::Key_A);
        QCOMPARE(c.previewLayer(), 0); // More conditions win even in the earlier row.
        QCOMPARE(c.liveKeyPreview()[36].toMap()["color"].toString(), QString("#ff0000"));
        QCOMPARE(c.liveKeyPreview()[14], c.keyPreview(0)[14]);
        QCOMPARE(c.liveKeyPreview()[14].toMap()["color"], c.keyPreview(-1)[14].toMap()["color"]);
        key(QEvent::KeyRelease, KEY_A, Qt::Key_A);
        QCOMPARE(c.previewLayer(), upper);
        key(QEvent::KeyPress, KEY_LEFTCTRL, Qt::Key_Control);
        QCOMPARE(c.previewLayer(), -1); // A Shift-only layer excludes Ctrl + Shift.
        QCOMPARE(c.liveKeyPreview()[36].toMap()["color"].toString(), QString("#0000ff"));
        key(QEvent::KeyRelease, KEY_LEFTCTRL, Qt::Key_Control);
        QCOMPARE(c.previewLayer(), upper);
        key(QEvent::KeyRelease, KEY_RIGHTSHIFT, Qt::Key_Shift);
        QCOMPARE(c.previewLayer(), -1);
        QCOMPARE(c.liveKeyPreview(), c.keyPreview(-1));
        QVERIFY(c.pressedKeys().isEmpty());
        key(QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
        QCOMPARE(c.pressedKeys(), QVariantList { 63 });
        c.watchLayerKeys(nullptr);
        QVERIFY(c.pressedKeys().isEmpty());
        QVERIFY(c.collection() == before);
        QCOMPARE(c.dirty(), dirty);
        QCOMPARE(edited.size(), 0);
    }
    void alternativeDeviceEnableIsBlockedUntilPreviousOwnerIsDisabled()
    {
        FakeKWin fake;
        const auto path = qgetenv("AMBIENT_LED_DRIVER_PATH");
        const auto restore = qScopeGuard([&] {
            qputenv("AMBIENT_LED_DRIVER_PATH", path);
            qApp->setProperty("test.one.claims", QVariant());
        });
        qApp->setProperty("test.one.claims", QStringList { "test:shared" });
        qputenv("AMBIENT_LED_DRIVER_PATH", path + ":" + MULTI_DRIVER_DIR + ":" + ALTERNATIVE_DRIVER_DIR);
        SettingsController c;
        QVERIFY(c.addDevice("test-strips:one"));
        QVERIFY(c.deviceSettings()["enabled"].toBool());
        QVERIFY(c.addDevice("test-alternative:alternative"));
        QVERIFY(!c.deviceSettings()["enabled"].toBool());
        QCOMPARE(c.deviceSettings()["enableBlockedBy"].toString(), QString("Test LED strip"));
        c.setDevice("enabled", true);
        QVERIFY(!c.deviceSettings()["enabled"].toBool());
        c.selectDevice(1);
        c.setDevice("enabled", false);
        c.selectDevice(2);
        QVERIFY(c.deviceSettings()["enableBlockedBy"].toString().isEmpty());
        c.setDevice("enabled", true);
        QVERIFY(c.deviceSettings()["enabled"].toBool());
        c.selectDevice(1);
        QCOMPARE(c.deviceSettings()["enableBlockedBy"].toString(), QString("Alternative"));
        QVERIFY(!QFile::exists(presetDirectory() + "/settings-v2.json"));
        QCOMPARE(asusd->writes.size(), 0);
    }
    void deviceChoicesAndGradientsRemainPendingUntilApply()
    {
        FakeKWin fake;
        const auto path = qgetenv("AMBIENT_LED_DRIVER_PATH");
        const auto restore = qScopeGuard([&] { qputenv("AMBIENT_LED_DRIVER_PATH", path); });
        qputenv("AMBIENT_LED_DRIVER_PATH", path + ":" + MULTI_DRIVER_DIR);
        SettingsController c;
        QTRY_VERIFY(c.lightbarAvailable());
        QCOMPARE(c.devices().size(), 1);
        QCOMPARE(c.availableDevices().size(), 2);
        QVERIFY(!c.dirty());
        QVERIFY(!c.collection().devices.has_value());
        QVERIFY(c.addDevice("test-strips:one"));
        QVERIFY(c.addDevice("test-strips:two"));
        QCOMPARE(c.devices().size(), 3);
        QVERIFY(c.editableLightbar());
        QVERIFY(!c.editableKeys());
        c.setDevice("pauseOnBattery", true);
        c.setDevice("pauseOnLowBattery", true);
        c.setDevice("lightbarOffOnBattery", true);
        c.setDevice("lightbarOffOnLowBattery", true);
        const auto localColor = c.addLocalEffect(-1, { 7 }, "solid");
        QVERIFY(!localColor.isEmpty());
        c.setSolidColor("#00ff00", localColor);
        c.setGradient(-1, -1, FirstStripId, 7, 1, "#ff0000", "#0000ff", 50, true, { });
        const auto afterGradient = c.collection().presets.last().effects[7];
        QVERIFY(std::none_of(
            afterGradient.begin(), afterGradient.end(), [&](const auto& e) { return e.id == localColor; }));

        QCOMPARE(c.gradients(-1).size(), 1);
        QCOMPARE(c.collection().presets.last().gradients.first().start, QColor(255, 0, 0, 128));
        QVERIFY(!QFile::exists(presetDirectory() + "/settings-v2.json"));
        c.selectDevice(0);
        c.setLightbarEnabled(false);
        c.selectDevice(2);
        QVERIFY(!c.lightbarAvailable());
        c.save();
        QTRY_VERIFY(!c.busy());
        QVERIFY(!c.dirty());
        QCOMPARE(asusd->writes.size(), 1);
        SettingsController loaded;
        QCOMPARE(loaded.devices().size(), 3);
        loaded.selectDevice(2);
        QCOMPARE(loaded.gradients(-1).size(), 1);
        QVERIFY(loaded.deviceSettings()["pauseOnBattery"].toBool());
        QVERIFY(loaded.deviceSettings()["pauseOnLowBattery"].toBool());
        QVERIFY(loaded.deviceSettings()["lightbarOffOnBattery"].toBool());
        QVERIFY(loaded.deviceSettings()["lightbarOffOnLowBattery"].toBool());
        loaded.setDevice("pauseOnBattery", false);
        QVERIFY(loaded.dirty());
        loaded.load();
        QVERIFY(loaded.deviceSettings()["pauseOnBattery"].toBool());
        loaded.setDevice("enabled", false);
        QVERIFY(loaded.dirty());
        loaded.load();
        QVERIFY(!loaded.dirty());
        loaded.removeDevice();
        QCOMPARE(loaded.devices().size(), 2);
        QVERIFY(loaded.dirty());
        QCOMPARE(loaded.collection().presets.size(), 2);
    }
    void keyboardGroupsLayersAndPresetsPersist()
    {
        FakeKWin fake;
        SettingsController c;
        QVERIFY(c.editableKeys());
        QCOMPARE(c.keys().size(), 88);
        c.assignKeys(-1, { 17, 18, 31, 32 }, 1, "#e06020");
        c.saveGroup("Movement", { 17, 18, 31, 32 });
        QCOMPARE(c.collection().presets[0].base[17].color, QColor("#e06020"));
        QVERIFY(c.setLayer(0, { "ctrl", "shift-right" }));
        c.assignKeys(0, { 17 }, 2, "#ffffff");
        QCOMPARE(c.collection().presets[0].layers[0].conditions, (QStringList { "ctrl", "shift-right" }));
        QCOMPARE(keyStyle(c.collection().presets[0].layers[0].styles, 18, true).mode, KeyMode::Inherit);
        c.setAmbient("temperature", -37);
        QVERIFY(c.addPreset(true));
        c.setName("Game");
        const auto game = c.preset().value("id").toString();
        c.setAmbient("fps", 9);
        int rule = c.addRule();
        c.setRule(rule, "name", "Game");
        c.setRule(rule, "windowClass", "org.example.game");
        c.setRule(rule, "presetId", game);
        QVERIFY(!c.removePreset()); // A referenced preset cannot silently disappear.
        c.save();
        QVERIFY(!c.dirty());
        SettingsController loaded;
        QCOMPARE(loaded.collection().presets.size(), 2);
        QCOMPARE(matchingPreset(loaded.collection(), "ORG.EXAMPLE.GAME"), 1);
        QCOMPARE(loaded.collection().effects[0].ambient.fps, 9);
        QCOMPARE(loaded.collection().effects[0].ambient.temperature, -37);
        QCOMPARE(loaded.collection().presets[1].groups.value("Movement"), (QList<int> { 17, 18, 31, 32 }));
        QCOMPARE(loaded.collection().presets[1].layers[0].styles[17].mode, KeyMode::Off);
        loaded.selectPreset(1);
        loaded.defaults();
        QVERIFY(loaded.collection().presets[1].base.isEmpty());
        loaded.load();
        QCOMPARE(loaded.collection().presets[1].base[17].color, QColor("#e06020"));
        c.removeRule(0);
        QVERIFY(c.removePreset());
        QCOMPARE(c.collection().presets.size(), 1);
    }
    void sharedEffectsCompositeAssignmentsAndImportRemapping()
    {
        SettingsController c;
        QVERIFY(c.addEffect("ambient"));
        c.setEffectName("Soft red");
        c.setAmbient("fps", 8);
        const auto effectId = c.effectPreset().value("id").toString();
        QVERIFY(!c.addEffect("unsupported"));
        c.assignKeys(-1, { 17, LightbarId }, 0, "#ff0000", effectId, 50);
        c.setGradient(-1, -1, FirstStripId, 89, 88, "#ff0000", "#0000ff", 50, true, effectId);
        QCOMPARE(c.collection().presets[0].base[17].color.alpha(), 128);
        QCOMPARE(c.collection().presets[0].base[LightbarId].effectId, effectId);
        QVERIFY(!c.removeEffect());
        QVERIFY(c.addPreset(true));
        QCOMPARE(
            c.collection().effects.size(), 2); // Duplicating a keyboard retains shared effect references.
        c.setAmbient("fps", 9);
        QCOMPARE(c.collection().presets[0].base[17].effectId, c.collection().presets[1].base[17].effectId);
        QTemporaryDir dir;
        c.exportFile(QUrl::fromLocalFile(dir.filePath("bundle.json")));
        QVERIFY(c.error().isEmpty());
        c.setAmbient("fps", 11); // Same ID, different settings in the receiving collection.
        c.importFile(QUrl::fromLocalFile(dir.filePath("bundle.json")));
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        QCOMPARE(c.collection().effects.size(), 3);
        const auto importedId = c.collection().presets.last().base[17].effectId;
        QVERIFY(importedId != effectId);
        QCOMPARE(c.collection().effects.last().id, importedId);
        QCOMPARE(c.collection().effects.last().ambient.fps, 9);
        QCOMPARE(c.collection().effects[1].ambient.fps, 11);
        QCOMPARE(c.collection().presets.last().base[LightbarId].effectId, importedId);
        QCOMPARE(c.collection().presets.last().gradients.first().effectId, importedId);
        c.setSection("keyboard");
        c.defaults();
        QCOMPARE(c.collection().effects[1].ambient.fps, 11); // Keyboard defaults never reset shared effects.
        c.selectEffect(2);
        QVERIFY(c.removeEffect());
    }
    void notificationTemplateAssignmentsAndPortablePresets()
    {
        SettingsController c;
        c.assignKeys(-1, { 14 }, int(KeyMode::Static), "#123456");
        const auto original = c.collection().presets[0];
        c.addNotificationLayout();
        const auto id = c.effectPreset()["id"].toString();
        QCOMPARE(c.effectPreset()["type"].toString(), QString("notifications"));
        for (int key : { 14, 15, 16, 17 })
            QCOMPARE(c.collection().presets[0].effectOverrides[key], (EffectList { { id } }));
        QCOMPARE(c.collection().presets[0].base, original.base);
        QCOMPARE(c.collection().presets[0].layers, original.layers);
        QCOMPARE(c.collection().presets[0].defaultEffectId, original.defaultEffectId);
        QVERIFY(!c.removeEffect());
        c.setNotification("urgency", 2);
        c.setNotification("color", "#ff8800");
        c.setNotification("application", "org.kde.kate");
        c.setNotification("pattern", "double");
        c.setDefaultEffect(id);
        QCOMPARE(c.collection().presets[0].defaultEffectId, original.defaultEffectId);
        c.addAssignedEffect(-1, { LightbarId }, id);
        c.addAssignedEffect(0, { 14 }, id);
        QCOMPARE(c.collection().presets[0].effects[LightbarId], (EffectList { { DefaultEffectId }, { id } }));
        QCOMPARE(c.collection().presets[0].effectOverrides[14], (EffectList { { id } }));
        QCOMPARE(c.collection().presets[0].layers[0].effects[14],
            (EffectList { { InlineColorId, 100, QColor("#123456") }, { id } }));
        QVERIFY(!c.keyPreview(0)[14].toMap()["inherited"].toBool());
        c.clearKeyLighting(0, { 15 }, false);
        QVERIFY(c.collection().presets[0].layers[0].effects.contains(15));
        c.clearKeyLighting(0, { 15 }, true);
        QVERIFY(!c.collection().presets[0].layers[0].effects.contains(15));
        QTemporaryDir dir;
        c.exportFile(QUrl::fromLocalFile(dir.filePath("notifications.json")));
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        c.setNotification("color", "#00ff00");
        c.importFile(QUrl::fromLocalFile(dir.filePath("notifications.json")));
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto& imported = c.collection().presets.last();
        const auto remapped = imported.effects[LightbarId].last().id;
        QVERIFY(remapped != id);
        QCOMPARE(imported.layers[0].effects[14].last().id, remapped);
        QCOMPARE(imported.effectOverrides[17].last().id, remapped);
        QCOMPARE(c.collection().effects.last().notification.color, QColor("#ff8800"));
        c.setSection("effects");
        c.defaults();
        QCOMPARE(c.collection().effects[1].notification, NotificationOptions());
        c.setSection("keyboard");
        c.defaults();
        QVERIFY(c.collection().presets.last().effects.isEmpty());
        QVERIFY(c.collection().presets.last().effectOverrides.isEmpty());
        QCOMPARE(c.collection().effects.last().notification.color, QColor("#ff8800"));
    }
    void effectListEditingPreservesOtherAssignments()
    {
        SettingsController c;
        QVERIFY(c.addEffect("notifications"));
        const auto normal = c.effectPreset()["id"].toString();
        c.setNotification("urgency", 1);
        QVERIFY(c.addEffect("notifications"));
        const auto critical = c.effectPreset()["id"].toString();
        c.setNotification("urgency", 2);
        c.addAssignedEffect(-1, { 14 }, normal);
        c.addAssignedEffect(-1, { 14, 15 }, critical);
        c.addAssignedEffect(-1, { 14 }, critical); // Adding the same preset twice is idempotent.
        QCOMPARE(c.collection().presets[0].effects[14],
            (EffectList { { DefaultEffectId }, { normal }, { critical } }));
        QCOMPARE(c.collection().presets[0].effects[15], (EffectList { { DefaultEffectId }, { critical } }));
        QVERIFY(c.selectionEffects(-1, { 14, 15 })["mixed"].toBool());
        c.moveAssignedEffect(-1, { 14 }, critical, false, -2);
        QCOMPARE(c.collection().presets[0].effects[14],
            (EffectList { { critical }, { DefaultEffectId }, { normal } }));
        c.moveAssignedEffect(-1, { 14 }, critical, false, 2);
        QCOMPARE(c.collection().presets[0].effects[14],
            (EffectList { { DefaultEffectId }, { normal }, { critical } }));
        c.moveAssignedEffect(-1, { 14 }, critical, false, -1);
        QCOMPARE(c.collection().presets[0].effects[14],
            (EffectList { { DefaultEffectId }, { critical }, { normal } }));
        c.updateAssignedEffect(-1, { 14, 15 }, critical, false, false, 40);
        QCOMPARE(c.collection().presets[0].effects[14][1].opacity, 40);
        c.updateAssignedEffect(-1, { 14, 15 }, critical, false, true, -1);
        QCOMPARE(c.collection().presets[0].effects[14], (EffectList { { DefaultEffectId }, { normal } }));
        QCOMPARE(c.collection().presets[0].effectOverrides[14], (EffectList { { critical, 40 } }));
        c.removeAssignedEffect(-1, { 14, 15 }, normal, false);
        QCOMPARE(c.collection().presets[0].effects[14], (EffectList { { DefaultEffectId } }));
        QCOMPARE(c.collection().presets[0].effectOverrides[14], (EffectList { { critical, 40 } }));
        c.addAssignedEffect(-1, { 14 }, DefaultEffectId);
        c.setKeyColor(-1, { 14 }, "#ffaa00", 30);
        QCOMPARE(c.collection().presets[0].effects[14], (EffectList { { DefaultEffectId } }));
        QCOMPARE(c.collection().presets[0].base[14].color, QColor(255, 170, 0, 77));
        c.save();
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto saved = c.collection();
        c.load();
        QCOMPARE(c.collection(), saved);
    }
    void deletingDeviceDefaultLeavesItUnassigned()
    {
        SettingsController c;
        QVERIFY(c.addPreset(true));
        const auto removed = c.preset()["id"].toString();
        c.setDevice("presetId", removed);
        QVERIFY(!c.preset()["defaultDevices"].toStringList().isEmpty());
        QVERIFY(c.removePreset());
        QVERIFY(c.deviceSettings()["presetId"].toString().isEmpty());
        QCOMPARE(c.collection().presets.size(), 1);
        c.save();
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto saved = c.collection();
        c.load();
        QCOMPARE(c.collection(), saved);
        QCOMPARE(devicePreset(c.collection(), c.collection().devices->first(), { }), -1);
    }
    void monochromeAmbientRequiresFourNonzeroSteps()
    {
        const auto oldPath = qgetenv("AMBIENT_LED_DRIVER_PATH");
        const auto oldDevices = qApp->property("test.devices");
        const auto restore = qScopeGuard([&] {
            qputenv("AMBIENT_LED_DRIVER_PATH", oldPath);
            qApp->setProperty("test.devices", oldDevices);
            qApp->setProperty("test.mono.brightnessSteps", QVariant());
        });
        qputenv("AMBIENT_LED_DRIVER_PATH", oldPath + ":" + MULTI_DRIVER_DIR);
        qApp->setProperty("test.devices", QStringList { "mono" });
        qApp->setProperty("test.mono.brightnessSteps", 3);
        SettingsController c;
        QVERIFY(c.device()["ambient"].toBool()); // RGB support needs no step metadata.
        QVERIFY(c.addDevice("test-strips:mono"));
        QVERIFY(!c.device()["color"].toBool());
        QVERIFY(!c.device()["ambient"].toBool());
        qApp->setProperty("test.mono.brightnessSteps", 4);
        c.refreshDevices();
        QVERIFY(c.device()["ambient"].toBool());
        c.setDevice("brightnessChannel", "blue");
        QCOMPARE(c.deviceSettings()["brightnessChannel"].toString(), QString("blue"));
        c.setDevice("brightnessChannel", "invalid");
        QCOMPARE(c.deviceSettings()["brightnessChannel"].toString(), QString("blue"));
        c.save();
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        c.load();
        QCOMPARE(c.deviceSettings()["brightnessChannel"].toString(), QString("blue"));
    }
    void brightnessCanBeEditedAsSharedAndLocalEffect()
    {
        SettingsController c;
        QVERIFY(c.addEffect("brightness"));
        const auto shared = c.effectPreset()["id"].toString();
        QCOMPARE(c.effectPreset()["brightness"].toInt(), 100);
        c.setBrightness(40);
        c.setSection("effects");
        c.defaults();
        QCOMPARE(c.effectPreset()["brightness"].toInt(), 100);
        c.setBrightness(40);
        c.addAssignedEffect(-1, { }, shared);
        const auto local = c.addLocalEffect(-1, { 14 }, "brightness");
        QVERIFY(!local.isEmpty());
        c.selectEffect(0);
        c.setBrightness(60, local);
        c.setBrightness(30, shared);
        QCOMPARE(c.localEffectOptions(local)["brightness"].toInt(), 60);
        QCOMPARE(c.assignedEffectOptions(shared)["brightness"].toInt(), 30);
        c.setBrightness(-1, local);
        QCOMPARE(c.localEffectOptions(local)["brightness"].toInt(), 0);
        c.setBrightness(120, local);
        QCOMPARE(c.localEffectOptions(local)["brightness"].toInt(), 100);
        c.save();
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto saved = c.collection();
        c.load();
        QCOMPARE(c.collection(), saved);
    }
    void solidPresetReplacesDefaultAmbient()
    {
        SettingsController c;
        QVERIFY(c.addEffect("solid"));
        c.setSolidColor("#112233");
        const auto id = c.effectPreset()["id"].toString();
        c.clearKeyLighting(-1, { }, false);
        c.addAssignedEffect(-1, { }, id);
        c.selectEffect(0);
        QVERIFY(c.removeEffect()); // No hidden legacy default reference after replacing the list.
        QCOMPARE(c.collection().effects.size(), 1);
        QCOMPARE(c.collection().effects.first().solidColor, QColor("#112233"));
        c.save();
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto saved = c.collection();
        c.load();
        QCOMPARE(c.collection(), saved);
    }
    void sharedEffectEditsUseIdentity()
    {
        SettingsController c;
        const auto ambient = c.effectPreset()["id"].toString();
        QVERIFY(c.addEffect("solid"));
        const auto solid = c.effectPreset()["id"].toString();
        c.setSolidColor("#112233");
        c.addAssignedEffect(-1, { 14 }, solid);
        c.addPreset(false);
        c.addAssignedEffect(-1, { 15 }, solid);
        const auto assignments = c.collection().presets;
        QVERIFY(c.addEffect("notifications"));
        const auto notify = c.effectPreset()["id"].toString();
        c.selectEffect(0);
        // The editor's global selection is unrelated to an expanded assignment's ID.
        c.setSolidColor("#c05020", solid);
        c.setNotification("urgency", 2, notify);
        c.setAmbient("fps", 7, ambient);
        c.setAmbient("pauseOnBattery", 1, ambient);
        QCOMPARE(c.selectedEffect(), 0);
        QCOMPARE(c.collection().presets, assignments);
        QCOMPARE(c.assignedEffectOptions(solid)["solidColor"].toString(), QString("#c05020"));
        QCOMPARE(c.assignedEffectOptions(notify)["urgency"].toInt(), 2);
        QCOMPARE(c.assignedEffectOptions(ambient)["fps"].toInt(), 7);
        QVERIFY(c.assignedEffectOptions(ambient)["pauseOnBattery"].toBool());
        QVERIFY(c.localEffectOptions(solid).isEmpty());
        QVERIFY(c.localizeEffect(-1, { 15 }, solid, false).isEmpty());
        QVERIFY(c.collection().presets.last().localEffects.isEmpty());
        QVERIFY(c.addEffect("ambient"));
        const auto secondAmbient = c.effectPreset()["id"].toString();
        c.setDefaultEffect(secondAmbient);
        QCOMPARE(c.assignedEffectOptions(QString())["id"].toString(), secondAmbient);
        const auto beforeInvalid = c.collection();
        c.setSolidColor("#ff0000", "missing-effect");
        QCOMPARE(c.collection(), beforeInvalid);
    }
    void removedLocalEffectsDoNotExhaustCapacity()
    {
        SettingsController c;
        for (int i = 0; i < 160; ++i) {
            const auto id = c.addLocalEffect(-1, { 14 }, "solid");
            QVERIFY2(!id.isEmpty(), qPrintable(c.error()));
            c.removeAssignedEffect(-1, { 14 }, id, false);
            QVERIFY(c.collection().presets[0].localEffects.isEmpty());
        }
        const auto id = c.addLocalEffect(-1, { 14 }, "notifications");
        c.addAssignedEffect(0, { 15 }, id);
        c.updateAssignedEffect(-1, { 14 }, id, false, true, 100);
        c.removeAssignedEffect(-1, { 14 }, id, true);
        QCOMPARE(c.collection().presets[0].localEffects.size(), 1);
        c.removeLayer(0);
        QVERIFY(c.collection().presets[0].localEffects.isEmpty());
    }
    void loadedOrphansDoNotBlockLocalEditing()
    {
        auto collection = defaultCollection("asus-g615lw");
        auto& preset = collection.presets[0];
        for (int i = 0; i < 128; ++i) {
            EffectPreset local;
            local.id = QStringLiteral("local-%1").arg(i);
            preset.localEffects.append(local);
        }
        preset.effects[DefaultAssignment] = { { "local-0" } };
        QString error;
        QVERIFY2(savePresets(collection, &error), qPrintable(error));
        SettingsController c;
        const auto isolated = c.localizeEffect(-1, { 14 }, "local-0", false);
        QVERIFY(!isolated.isEmpty() && isolated != "local-0");
        QCOMPARE(c.collection().presets[0].localEffects.size(), 2);
        QCOMPARE(c.collection().presets[0].effects[DefaultAssignment], (EffectList { { "local-0" } }));
    }
    void gradientCannotOverflowNotificationAssignments()
    {
        const auto path = qgetenv("AMBIENT_LED_DRIVER_PATH");
        const auto restore = qScopeGuard([&] { qputenv("AMBIENT_LED_DRIVER_PATH", path); });
        qputenv("AMBIENT_LED_DRIVER_PATH", path + ":" + MULTI_DRIVER_DIR);
        SettingsController c;
        QVERIFY(c.addDevice("test-strips:one"));
        c.clearKeyLighting(-1, { 7 }, false);
        for (int i = 0; i < 32; ++i)
            QVERIFY(!c.addLocalEffect(-1, { 7 }, "notifications").isEmpty());
        const auto before = c.collection();
        QVERIFY(!c.setGradient(-1, -1, FirstStripId, 7, 1, "#ff0000", "#0000ff", 100, true));
        QVERIFY(c.error().contains("32"));
        QCOMPARE(c.collection(), before);
        QVERIFY(c.setGradient(-1, -1, FirstStripId, 7, 1, "#ff0000", "#0000ff", 100, false));
    }
    void localEffectsAndDefaultAssignments()
    {
        SettingsController c;
        const auto originalSaved = c.collection().effects;
        const auto color = c.addLocalEffect(-1, { }, "solid");
        QVERIFY(!color.isEmpty());
        c.setSolidColor("#ff8000", color);
        c.updateAssignedEffect(-1, { }, color, false, false, 35);
        const auto defaults = c.selectionEffects(-1, { });
        QVERIFY(defaults["defaults"].toBool());
        QCOMPARE(defaults["items"].toList().size(), 2);
        QVariantList all;
        for (const auto& key : c.keys())
            all.append(key.toMap()["id"]);
        QCOMPARE(c.selectionEffects(-1, all), defaults);
        QCOMPARE(c.selectionEffects(-1, { 14 })["items"], defaults["items"]);
        // Editing an inherited local color isolates this selection from the default layout.
        const auto isolated = c.localizeEffect(-1, { 14 }, color, false);
        QVERIFY(isolated != color && !isolated.isEmpty());
        c.setSolidColor("#00ff00", isolated);
        QCOMPARE(c.localEffectOptions(color)["solidColor"].toString(), QString("#ff8000"));
        QCOMPARE(c.localEffectOptions(isolated)["solidColor"].toString(), QString("#00ff00"));
        const auto notify = c.addLocalEffect(-1, { 14 }, "notifications");
        c.setNotification("urgency", 2, notify);
        c.setNotification("color", "#ff0000", notify);
        c.updateAssignedEffect(-1, { 14 }, notify, false, true, 70);
        const auto ambient = c.addLocalEffect(-1, { 15 }, "ambient");
        c.setAmbient("fps", 6, ambient);
        QCOMPARE(c.localEffectOptions(ambient)["fps"].toInt(), 6);
        QCOMPARE(c.collection().effects, originalSaved);
        c.clearKeyLighting(-1, { 16 }, false);
        const auto before = c.collection().presets[0].effects[14];
        c.removeAssignedEffect(-1, all, color, false);
        QCOMPARE(c.collection().presets[0].effects[14], before);
        QVERIFY(c.collection().presets[0].effects[16].isEmpty());
        QCOMPARE(c.selectionEffects(-1, { })["items"].toList().size(), 1);
        c.save();
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto saved = c.collection();
        c.load();
        QCOMPARE(c.collection(), saved);
        QTemporaryDir dir;
        c.exportFile(QUrl::fromLocalFile(dir.filePath("local-effects.json")));
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        c.importFile(QUrl::fromLocalFile(dir.filePath("local-effects.json")));
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        QCOMPARE(c.collection().presets.last().localEffects.size(), 4);
        QCOMPARE(c.collection().effects.size(), originalSaved.size());
    }
    void foreignDeviceAssignmentsArePreserved()
    {
        FakeKWin fake;
        SettingsController c;
        auto p = c.collection().presets[0];
        p.keyboardDeviceId = "another-keyboard";
        p.base[104] = { KeyMode::Static, QColor("#11aa33") };
        QTemporaryDir dir;
        QString error;
        QVERIFY(exportPreset(p, c.collection().effects, dir.filePath("preset.json"), &error));
        c.importFile(QUrl::fromLocalFile(dir.filePath("preset.json")));
        QVERIFY(!c.editableKeys());
        c.assignKeys(-1, { 17 }, 1, "#ffffff");
        c.saveGroup("A", { 17 });
        QCOMPARE(c.collection().presets[1].base, p.base);
        QVERIFY(c.collection().presets[1].groups.isEmpty());
        c.setAmbient("fps", 4);
        c.save();
        QVERIFY(c.error().isEmpty());
        SettingsController loaded;
        QCOMPARE(loaded.collection().presets[1].base, p.base);
    }
    void invalidRulesAndConcurrentEditsDoNotOverwrite()
    {
        FakeKWin fake;
        SettingsController first, second;
        first.setAmbient("fps", 8);
        first.save();
        QVERIFY(first.error().isEmpty());
        second.setAmbient("fps", 5);
        second.save();
        QVERIFY(second.error().contains("changed on disk"));
        SettingsController check;
        QCOMPARE(check.effectPreset().value("fps").toInt(), 8);
        check.addRule();
        check.setAmbient("fps", 4);
        check.save();
        QVERIFY(!check.error().isEmpty());
        SettingsController unchanged;
        QCOMPARE(unchanged.effectPreset().value("fps").toInt(), 8);
        QVERIFY(unchanged.rules().isEmpty());
    }
    void windowSelectionPreservesRulesAndGuardsDelayedReplies()
    {
        FakeKWin fake;
        SettingsController c;
        int row = c.addRule();
        c.setRule(row, "name", "Editor");
        c.setRule(row, "enabled", false);
        c.selectWindow(row);
        QTRY_VERIFY(!c.pickingWindow());
        QCOMPARE(c.collection().rules[row].windowClass, QString("org.kde.kate"));
        QCOMPARE(c.collection().rules[row].name, QString("Editor"));
        QVERIFY(!c.collection().rules[row].enabled);
        fake.deferWindowReply = true;
        fake.windowInfo["resourceClass"] = "org.kde.konsole";
        c.selectWindow(row);
        QTRY_COMPARE(fake.windowQueries, 2);
        c.setRule(row, "windowClass", "manual");
        fake.replyToWindowQuery();
        QTRY_VERIFY(!c.pickingWindow());
        QCOMPARE(c.collection().rules[row].windowClass, QString("manual"));
        fake.deferWindowReply = false;
        fake.windowError = "org.kde.KWin.Error.UserCancel";
        c.selectWindow(row);
        QTRY_VERIFY(!c.pickingWindow());
        QVERIFY(c.windowStatus().isEmpty());
        QCOMPARE(c.collection().rules[row].windowClass, QString("manual"));
        fake.windowError = "org.kde.KWin.Error.Failed";
        c.selectWindow(row);
        QTRY_VERIFY(!c.pickingWindow());
        QVERIFY(c.windowStatus().contains("Could not select"));
        fake.windowError.clear();
        fake.windowInfo.clear();
        c.selectWindow(row);
        QTRY_VERIFY(!c.pickingWindow());
        QVERIFY(c.windowStatus().contains("no application class"));
    }
    void effectEnableDisablePersistsAndSurvivesClose()
    {
        FakeKWin fake;
        fake.loaded.clear();
        auto c = openModule();
        QTRY_VERIFY(c->effect()->available());
        c->effect()->setEnabled(true);
        QVERIFY(c->dirty());
        QVERIFY(fake.loads.isEmpty());
        c->save();
        c.reset();
        QTRY_COMPARE(fake.loads, (QStringList { "ambientled" }));
        QTRY_VERIFY(KConfigGroup(config, "Plugins").readEntry("ambientledEnabled", false));
        SettingsController reopened;
        QTRY_VERIFY(reopened.effect()->loaded());
        QVERIFY(reopened.effect()->enabled());
        reopened.effect()->setEnabled(false);
        reopened.save();
        QTRY_VERIFY(!reopened.effect()->busy());
        QVERIFY(!reopened.effect()->loaded());
        QVERIFY(!reopened.dirty());
        QCOMPARE(fake.unloads, (QStringList { "ambientled" }));
        QVERIFY(!KConfigGroup(config, "Plugins").readEntry("ambientledEnabled", true));
    }
    void effectFailureAndExternalChanges()
    {
        FakeKWin fake;
        fake.loaded.clear();
        fake.refuseLoad = true;
        SettingsController c;
        QTRY_VERIFY(c.effect()->available());
        c.effect()->setEnabled(true);
        c.save();
        QTRY_VERIFY(!c.busy());
        QVERIFY(c.dirty());
        QVERIFY(!c.effect()->loaded());
        QVERIFY(c.effect()->status().contains("could not change"));
        fake.refuseLoad = false;
        c.save();
        QTRY_VERIFY(!c.busy());
        QVERIFY(c.effect()->loaded());
        QVERIFY(!c.dirty());
        c.effect()->setEnabled(false);
        KConfigGroup group(config, "Plugins");
        group.writeEntry("ambientledEnabled", false);
        QVERIFY(group.sync());
        c.save();
        QVERIFY(c.effect()->status().contains("changed elsewhere"));
        QVERIFY(fake.unloads.isEmpty());
        QTRY_VERIFY(!c.busy());
        c.load();
        QTRY_VERIFY(c.effect()->enabled()); // Reset shows the still-running effect, not just its saved flag.
        QVERIFY(!c.dirty());
    }
    void olderRunningEffectRemainsControllable()
    {
        FakeKWin fake;
        fake.loaded = { "ambientled_dev15" };
        SettingsController c;
        QTRY_VERIFY(c.effect()->available());
        QVERIFY(c.effect()->enabled());
        QVERIFY(c.effect()->loaded());
        QVERIFY(!c.dirty());
        c.effect()->setEnabled(false);
        c.save();
        QTRY_VERIFY(!c.busy());
        QCOMPARE(fake.unloads, (QStringList { "ambientled_dev15" }));
        QVERIFY(fake.loaded.isEmpty());
        QVERIFY(!c.effect()->enabled());
        QVERIFY(!c.dirty());
        c.effect()->setEnabled(true);
        c.save();
        QTRY_VERIFY(!c.busy());
        QCOMPARE(fake.loads, (QStringList { "ambientled" }));
        QCOMPARE(fake.loaded, fake.installed);
        QVERIFY(!fake.overlappingLoad);
        QVERIFY(c.effect()->enabled());
    }
    void replacingAnOlderRunningEffectVerifiesUnload_data()
    {
        QTest::addColumn<bool>("refuseUnload");
        QTest::newRow("unload-confirmed") << false;
        QTest::newRow("unload-refused") << true;
    }
    void replacingAnOlderRunningEffectVerifiesUnload()
    {
        QFETCH(bool, refuseUnload);
        FakeKWin fake;
        fake.loaded = { "ambientled_dev15" };
        fake.refuseUnload = refuseUnload;
        SettingsController c;
        QTRY_VERIFY(c.effect()->available());
        QVERIFY(c.effect()->enabled()); // Running even though its saved flag is absent.
        c.effect()->setEnabled(false);
        c.effect()->setEnabled(true); // Apply also synchronizes the missing installed flag.
        QVERIFY(c.dirty());
        c.effect()->refresh();
        QTest::qWait(30);
        QVERIFY(c.effect()->enabled()); // Polling preserves pending edits.
        c.save();
        QTRY_VERIFY(!c.busy());
        QCOMPARE(fake.unloads, (QStringList { "ambientled_dev15" }));
        if (refuseUnload) {
            QVERIFY(fake.loads.isEmpty());
            QCOMPARE(fake.loaded, (QStringList { "ambientled_dev15" }));
            QVERIFY(c.dirty());
            QVERIFY(c.effect()->status().contains("could not change"));
        } else {
            QCOMPARE(fake.loads, (QStringList { "ambientled" }));
            QCOMPARE(fake.loaded, fake.installed);
            QVERIFY(!c.dirty());
        }
        QVERIFY(!fake.overlappingLoad);
    }
    void effectCheckboxTracksRuntimeWithoutDirtyingSettings()
    {
        FakeKWin fake;
        SettingsController c;
        QTRY_VERIFY(c.effect()->available());
        QVERIFY(c.effect()->enabled());
        QVERIFY(!c.dirty());
        fake.loaded.clear();
        c.effect()->refresh();
        QTRY_VERIFY(!c.effect()->enabled());
        QVERIFY(!c.dirty());
        fake.loaded = fake.installed;
        c.effect()->refresh();
        QTRY_VERIFY(c.effect()->enabled());
        QVERIFY(!c.dirty());
    }
    void duplicateEffectsCannotBeEnabled()
    {
        FakeKWin fake;
        fake.installed << "ambientled_dev15";
        SettingsController c;
        QTRY_VERIFY(c.effect()->status().contains("Multiple"));
        QVERIFY(!c.effect()->available());
        c.effect()->setEnabled(true);
        QVERIFY(!c.effect()->enabled());
        QVERIFY(fake.loads.isEmpty());
    }
    void applyAndReloadPreserveOtherSettings()
    {
        FakeKWin fake;
        KConfigGroup group(config, "Effect-ambientled");
        group.writeEntry("FramesPerSecond", 7);
        group.writeEntry("SmoothingMs", 200);
        group.writeEntry("ScreenName", "eDP-1");
        QVERIFY(group.sync());
        auto module = openModule();
        QCOMPARE(module->effectPreset().value("fps").toInt(), 3);
        QCOMPARE(module->effectPreset().value("smoothing").toInt(), 400);
        module->setAmbient("temperature", -40);
        module->setAmbient("fps", 2);
        module->setAmbient("smoothing", 750);
        module->setAmbient("ledFps", 45);
        module->setAmbient("minimum", 15);
        module->setAmbient("maximum", 50);
        QVERIFY(module->dirty());
        module->save();
        QVERIFY(!module->dirty());
        QTRY_COMPARE(lighting->reloads, 1);
        QVERIFY(fake.reconfigured.isEmpty());
        QCOMPARE(fake.unexpected, 0);
        config->reparseConfiguration();
        QCOMPARE(group.readEntry("FramesPerSecond", 0), 7); // Old snapshot is left untouched.
        QCOMPARE(group.readEntry("SmoothingMs", 0), 200);
        QCOMPARE(group.readEntry("ScreenName", QString()), QStringLiteral("eDP-1"));
        auto reopened = openModule();
        QCOMPARE(reopened->effectPreset().value("fps").toInt(), 2);
        QCOMPARE(reopened->effectPreset().value("smoothing").toInt(), 750);
        QCOMPARE(reopened->effectPreset().value("ledFps").toInt(), 45);
        QCOMPARE(reopened->effectPreset().value("minimum").toInt(), 15);
        QCOMPARE(reopened->effectPreset().value("maximum").toInt(), 50);
        QCOMPARE(reopened->effectPreset().value("temperature").toInt(), -40);
    }
    void defaultsWaitForApplyAndApplySurvivesClose()
    {
        FakeKWin fake;
        auto module = openModule();
        module->setSection("effects");
        module->setAmbient("fps", 9);
        module->setAmbient("minimum", 80);
        module->setAmbient("temperature", 60);
        module->save();
        QTRY_VERIFY(!module->busy());
        QTRY_COMPARE(lighting->reloads, 1);
        QVERIFY(fake.reconfigured.isEmpty());
        lighting->reloads = 0;
        module->defaults();
        QVERIFY(module->dirty());
        QVERIFY(module->representsDefaults());
        SettingsController before;
        QCOMPARE(before.effectPreset().value("fps").toInt(), 9);
        QCOMPARE(before.effectPreset().value("minimum").toInt(), 80);
        module->save();
        module.reset();
        QTRY_COMPARE(lighting->reloads, 1);
        QVERIFY(fake.reconfigured.isEmpty());
        SettingsController after;
        QCOMPARE(after.effectPreset().value("fps").toInt(), 3);
        QCOMPARE(after.effectPreset().value("minimum").toInt(), 25);
        QCOMPARE(after.effectPreset().value("maximum").toInt(), 70);
        QCOMPARE(after.effectPreset().value("temperature").toInt(), 0);
    }
    void brightnessBoundsAndCancel()
    {
        auto module = openModule();
        module->setAmbient("minimum", 80);
        QCOMPARE(module->effectPreset().value("maximum").toInt(), 80);
        module->setAmbient("maximum", 25);
        module->setAmbient("temperature", -60);
        QCOMPARE(module->effectPreset().value("minimum").toInt(), 25);
        QVERIFY(module->dirty());
        module.reset();
        config->reparseConfiguration();
        const KConfigGroup group(config, "Effect-ambientled");
        QVERIFY(!group.hasKey("MinimumBrightness"));
        QVERIFY(!group.hasKey("MaximumBrightness"));
        QVERIFY(!group.hasKey("LedFramesPerSecond"));
        QVERIFY(!group.hasKey("ColorTemperature"));
    }
    void invalidSettingsAreClamped()
    {
        QString error;
        EffectPreset p;
        auto json = effectPresetJson(p);
        auto options = json["options"].toObject();
        options["minimum"] = 200;
        options["maximum"] = 30;
        options["ledFps"] = 999;
        options["temperature"] = -999;
        json["options"] = options;
        auto parsed = parseEffectPreset(QJsonDocument(json).toJson(), &error);
        QVERIFY(parsed);
        QCOMPARE(parsed->ambient.minimum, 30);
        QCOMPARE(parsed->ambient.maximum, 30);
        QCOMPARE(parsed->ambient.ledFps, 60);
        QCOMPARE(parsed->ambient.temperature, -100);
        options["minimum"] = -10;
        options["maximum"] = -50;
        options["ledFps"] = 0;
        options["temperature"] = 999;
        json["options"] = options;
        parsed = parseEffectPreset(QJsonDocument(json).toJson(), &error);
        QVERIFY(parsed);
        QCOMPARE(parsed->ambient.minimum, 0);
        QCOMPARE(parsed->ambient.maximum, 0);
        QCOMPARE(parsed->ambient.ledFps, 1);
        QCOMPARE(parsed->ambient.temperature, 100);
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
#include "test_config.moc"
