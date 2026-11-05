// SPDX-License-Identifier: GPL-3.0-or-later
#include "application_settings.h"
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
#include "fixtures/keyboard.h"
#include "fixtures/settings_services.h"
class ConfigTest : public QObject
{
    Q_OBJECT
    KSharedConfig::Ptr config = KSharedConfig::openConfig("kwinrc");
    std::unique_ptr<FakeLighting> lighting;
    std::unique_ptr<SettingsController> openModule() { return std::make_unique<SettingsController>(); }
private Q_SLOTS:
    void init()
    {
        QVERIFY(QDir(presetDirectory()).removeRecursively());
        QVERIFY(QDir(QFileInfo(applicationSettingsPath()).absolutePath()).removeRecursively());
        qApp->setProperty("test.controls.writes", 0);
        qApp->setProperty("test.controls.enabled", true);
        lighting = std::make_unique<FakeLighting>();
        config->reparseConfiguration();
        config->deleteGroup("Effect-ambientled");
        config->deleteGroup("Plugins");
        QVERIFY(config->sync());
    }
    void cleanup() { lighting.reset(); }
    void microphoneDiagnosticsDoNotBecomeServiceErrors()
    {
        lighting->info.insert("microphoneBackend", "fixture");
        lighting->info.insert("microphoneAvailable", false);
        lighting->info.insert("microphoneMuteKnown", false);
        lighting->info.insert("microphoneReason", "Synthetic backend unavailable");
        ServiceControl control;
        QTRY_COMPARE(control.microphoneStatus()["backend"].toString(), QString("fixture"));
        QVERIFY(!control.microphoneStatus()["available"].toBool());
        QCOMPARE(control.microphoneStatus()["reason"].toString(), QString("Synthetic backend unavailable"));
        QCOMPARE(control.status(), QString("Lighting service is running."));
        QVERIFY(control.error().isEmpty());
        lighting->info.insert("microphoneAvailable", true);
        lighting->info.insert("microphoneMuteKnown", true);
        lighting->info.insert("microphoneMuted", false);
        lighting->info.insert("microphoneSourceName", "fixture-input");
        lighting->info.insert("microphoneDescription", "Synthetic input");
        lighting->info.insert("microphoneReason", "");
        control.refresh();
        QTRY_VERIFY(control.microphoneStatus()["muteKnown"].toBool());
        QCOMPARE(control.microphoneStatus()["description"].toString(), QString("Synthetic input"));
        QCOMPARE(control.microphoneStatus()["sourceName"].toString(), QString("fixture-input"));
        QVERIFY(!control.microphoneStatus()["muted"].toBool());
        lighting.reset();
        control.refresh();
        QTRY_VERIFY(!control.running());
        QVERIFY(control.microphoneStatus().isEmpty());
    }
    void manualDesktopRetryReportsFailuresAndRefreshesAvailability()
    {
        lighting->canRetry = true;
        lighting->retryError = "Cannot save startup state";
        ServiceControl control;
        QTRY_VERIFY(control.desktopCanRetry());
        QCOMPARE(control.status(), "Automatic loading paused");
        control.retryDesktopIntegration();
        QVERIFY(control.busy());
        control.retryDesktopIntegration(); // A second click must not duplicate the request.
        QTRY_VERIFY(!control.busy());
        QCOMPARE(lighting->retries, 1);
        QVERIFY(control.status().contains(lighting->retryError));
        lighting->retryError.clear();
        control.retryDesktopIntegration();
        QTRY_VERIFY(!control.desktopCanRetry());
        QCOMPARE(lighting->retries, 2);
        QCOMPARE(lighting->reloads, 0); // Retry does not apply unsaved editor settings.
    }
    void buildWarningsUseInstalledFilesAndDoNotBlockCompatibleService()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("service.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"buildId\":\"installed-v2\",\"version\":\"0.27.0\"}");
        file.close();
        lighting->info = componentInfo(Component::Service);
        lighting->info["buildId"] = "running-old";
        auto capture = componentInfo(Component::Capture);
        capture["buildId"] = "capture-old";
        lighting->info["captureInfo"] = capture;
        ServiceControl control(InstallationPaths { path, { }, EFFECT_PLUGIN_PATH });
        QTRY_VERIFY(control.running());
        QVERIFY(control.compatible());
        QVERIFY(control.diagnostics().contains("running-old"));
        QVERIFY(control.diagnostics().contains("installed-v2"));
        QVERIFY(control.diagnostics().contains("capture-old"));
        control.reload();
        QTRY_COMPARE(lighting->reloads, 1);
        QTRY_VERIFY(!control.busy());
        lighting->info["buildId"] = "installed-v2";
        lighting->info.remove("captureInfo");
        control.refresh();
        QTRY_VERIFY(control.diagnostics().isEmpty());
        lighting->info.remove("buildId");
        control.refresh();
        QTRY_VERIFY(control.diagnostics().contains("does not report its build"));
        QVERIFY(QFile::remove(path));
        control.refresh();
        QTRY_VERIFY(control.diagnostics().isEmpty()); // Unknown installed identity, not mismatch.
    }
    void incompatibleServiceIsRunningButDoesNotReceiveReload()
    {
        lighting->info["protocol"] = Ipc::Version + 1;
        lighting->canRetry = true;
        ServiceControl control(InstallationPaths { });
        QTRY_VERIFY(control.running());
        QVERIFY(!control.compatible());
        QVERIFY(control.status().contains("incompatible"));
        QVERIFY(!control.desktopCanRetry());
        QSignalSpy finished(&control, &ServiceControl::finished);
        control.reload();
        QCOMPARE(finished.size(), 1);
        QVERIFY(!control.error().isEmpty());
        QCOMPARE(lighting->reloads, 0);
        control.retryDesktopIntegration();
        QCOMPARE(lighting->retries, 0);
        lighting->info = componentInfo(Component::Service);
        control.refresh();
        QTRY_VERIFY(control.compatible());
        control.reload();
        QTRY_COMPARE(lighting->reloads, 1);
        QTRY_VERIFY(!control.busy());
        QVERIFY(control.error().isEmpty());
    }
    void addingSameModelDeviceCreatesItsOwnDefaultCard()
    {
        const auto path = qgetenv("AMBIENT_LED_DRIVER_PATH");
        const auto restore = qScopeGuard([&] { qputenv("AMBIENT_LED_DRIVER_PATH", path); });
        qputenv("AMBIENT_LED_DRIVER_PATH", MULTI_DRIVER_DIR);
        SettingsController c;
        QCOMPARE(c.devices().size(), 1);
        c.assignKeys(-1, { 7 }, 1, "#ff0000");
        QVERIFY(c.addDevice("test-strips:two"));
        QCOMPARE(c.collection().presets.size(), 1);
        QCOMPARE(c.presetDevices().size(), 2);
        const auto& scene = c.collection().presets.first();
        QVERIFY(scene.multiDevice);
        QCOMPARE(scene.devices[0].targetDevice, QString("test-strips:one"));
        QCOMPARE(scene.devices[0].base[7].color, QColor(Qt::red));
        QCOMPARE(scene.devices[1].targetDevice, QString("test-strips:two"));
        QCOMPARE(scene.devices[1].effects, (EffectAssignments { { DefaultAssignment, { } } }));
        for (const auto& device : *c.collection().devices)
            QCOMPARE(devicePreset(c.collection(), device, { }), 0);
    }
    void deviceCardsShareTheDocumentButKeepAssignmentsIndependent()
    {
        const auto path = qgetenv("AMBIENT_LED_DRIVER_PATH");
        const auto restore = qScopeGuard([&] { qputenv("AMBIENT_LED_DRIVER_PATH", path); });
        qputenv("AMBIENT_LED_DRIVER_PATH", path + ":" + MULTI_DRIVER_DIR);
        SettingsController c;
        QVERIFY(c.addDevice("test-strips:one"));
        QVERIFY(c.addDevice("test-strips:two"));
        // New devices join Default as independent cards, including same-model instances.
        QCOMPARE(c.collection().presets.size(), 1);
        QCOMPARE(c.preset()["id"].toString(), c.defaultId());
        QCOMPARE(c.presetDevices().size(), 3);
        QVERIFY(c.collection().presets[c.selected()].multiDevice);
        c.setDevice("enabled", false);
        QVERIFY(c.addPreset(false));
        QCOMPARE(c.presetDevices().size(), 2); // Enabled keyboard and first strip, not disabled second strip.
        QVERIFY(c.addPresetDevice("test-strips:two"));
        QCOMPARE(c.presetDevices().size(), 3);
        auto* first = c.presetDevices()[1].value<SettingsController*>();
        auto* second = c.presetDevices()[2].value<SettingsController*>();
        QVERIFY(first && second);
        QVERIFY(first->profileInfo()["enabled"].toBool());
        QVERIFY(!second->profileInfo()["enabled"].toBool());
        QSignalSpy structure(&c, &SettingsController::presetDevicesChanged);
        const auto red = first->addLocalEffect(-1, { 7 }, "solid");
        QVERIFY(!red.isEmpty());
        first->setSolidColor("#ff0000", red);
        QCOMPARE(second->selectionEffects(-1, { 7 })["items"].toList().size(), 0);
        first->copyProfile();
        QVERIFY(second->canPasteProfile());
        QVERIFY(second->pasteWarnings().isEmpty());
        QVERIFY(second->pasteProfile());
        QCOMPARE(second->assignedEffectOptions(red)["solidColor"].toString(), QString("#ff0000"));
        second->setSolidColor("#00ff00", red);
        QCOMPARE(first->assignedEffectOptions(red)["solidColor"].toString(), QString("#ff0000"));
        QCOMPARE(structure.count(), 0); // Color changes must not recreate keyboard delegates/selections.
        QVERIFY(c.dirty());
        QString error;
        QVERIFY2(savePresets(c.collection(), &error), qPrintable(error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c.collection());
        const auto removed = second->profileInfo()["id"].toString();
        c.removePresetDevice(removed);
        QCOMPARE(c.presetDevices().size(), 2);
        QVERIFY(c.addPresetDevice("test-strips:two"));
        QCOMPARE(c.presetDevices().size(), 3);
        QCOMPARE(qApp->property("test.controls.writes").toInt(), 0);
    }
    void removingLastLegacyDeviceRetainsPresetLayers()
    {
        SettingsController c;
        const auto layers = c.preset()["layers"];
        const auto devices = c.devices();
        c.removePresetDevice(c.preset()["id"].toString());
        QVERIFY(c.presetDevices().isEmpty());
        QCOMPARE(c.devices(), devices);
        QCOMPARE(c.addablePresetDevices().size(), 1);
        QCOMPARE(devicePreset(c.collection(), c.collection().devices->first(), { }), -1);
        QCOMPARE(c.preset()["layers"], layers);
        QVERIFY(!c.representsDefaults());
        QString error;
        QVERIFY2(savePresets(c.collection(), &error), qPrintable(error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c.collection());
    }
    void layersBelongToThePresetIncludingBrightnessOnlyDevices()
    {
        const auto path = qgetenv("AMBIENT_LED_DRIVER_PATH");
        const auto restore = qScopeGuard([&] {
            qputenv("AMBIENT_LED_DRIVER_PATH", path);
            qApp->setProperty("test.devices", QVariant());
            qApp->setProperty("test.mono.brightnessSteps", QVariant());
        });
        qputenv("AMBIENT_LED_DRIVER_PATH", path + ":" + MULTI_DRIVER_DIR);
        qApp->setProperty("test.devices", QStringList { "one", "mono" });
        qApp->setProperty("test.mono.brightnessSteps", 3);
        SettingsController c;
        QVERIFY(c.addDevice("test-strips:one"));
        QVERIFY(c.addDevice("test-strips:mono"));
        QVERIFY(c.addPreset(false));
        auto* keyboard = c.presetDevices()[0].value<SettingsController*>();
        auto* strip = c.presetDevices()[1].value<SettingsController*>();
        auto* mono = c.presetDevices()[2].value<SettingsController*>();
        QVERIFY(!mono->editableKeys());
        QCOMPARE(mono->addLayer({ "shift" }), 0);
        QCOMPARE(c.addLayer({ "ctrl", "shift" }), 1);
        QCOMPARE(strip->addLayer({ "shift", "ctrl" }), -1);
        const auto selected = [](SettingsController* editor, const QVariantList& keys) {
            return QVariantMap { { "profile", editor->profileInfo()["id"] }, { "layer", 1 },
                { "keys", keys } };
        };
        const QVariantList targets { selected(keyboard, { 17 }), selected(mono, { }) };
        const auto dim = c.editDeviceSelection(targets, "new", { { "type", "brightness" } });
        QVERIFY(!dim.isEmpty());
        c.editDeviceSelection(targets, "brightness", { { "id", dim }, { "value", 33 } });
        for (auto* editor : { keyboard, strip, mono })
            QCOMPARE(editor->preset()["layers"], c.preset()["layers"]);
        QVERIFY(mono->setLayer(1, { "ctrl", "alt" }));
        QCOMPARE(keyboard->preset()["layers"].toList()[1].toMap()["conditions"].toStringList(),
            (QStringList { "ctrl", "alt" }));
        QCOMPARE(mono->selectionEffects(1, { })["items"].toList().size(), 1);
        QVERIFY(mono->selectionEffects(-1, { })["items"].toList().isEmpty());
        QCOMPARE(c.moveLayer(1, -1), 0);
        QCOMPARE(mono->selectionEffects(0, { })["items"].toList().size(), 1);
        c.removeLayer(1);
        QCOMPARE(c.preset()["layers"].toList().size(), 1);
        for (auto* editor : { keyboard, strip, mono })
            QCOMPARE(editor->preset()["layers"], c.preset()["layers"]);
        // Adding a card after defining layers creates empty assignments that inherit Base.
        c.removePresetDevice(strip->profileInfo()["id"].toString());
        QVERIFY(c.addPresetDevice("test-strips:one"));
        auto* added = c.presetDevices().last().value<SettingsController*>();
        QCOMPARE(added->preset()["layers"], c.preset()["layers"]);
        QVERIFY(added->selectionEffects(0, { })["items"].toList().isEmpty());
        // A profile copied from another scene brings its conditions, without replacing existing ones.
        mono->copyProfile();
        QVERIFY(c.addPreset(false));
        QCOMPARE(c.addLayer({ "super" }), 0);
        auto* destination = c.presetDevices().last().value<SettingsController*>();
        QVERIFY(destination->pasteProfile());
        QCOMPARE(c.preset()["layers"].toList().size(), 2);
        QCOMPARE(destination->selectionEffects(1, { })["items"].toList().size(), 1);
        QString error;
        QVERIFY2(savePresets(c.collection(), &error), qPrintable(error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c.collection());
        for (const auto& value : c.presetDevices())
            c.removePresetDevice(value.value<SettingsController*>()->profileInfo()["id"].toString());
        QVERIFY(c.presetDevices().isEmpty());
        QVERIFY(!c.representsDefaults()); // Common layers survive removal of the last device card.
        c.defaults();
        QVERIFY(c.preset()["layers"].toList().isEmpty());
        QVERIFY(c.representsDefaults());
        QCOMPARE(qApp->property("test.controls.writes").toInt(), 0);
    }
    void multipleDeviceSelectionsKeepEffectsAndExposeOutputLimits()
    {
        const auto path = qgetenv("AMBIENT_LED_DRIVER_PATH");
        const auto restore = qScopeGuard([&] {
            qputenv("AMBIENT_LED_DRIVER_PATH", path);
            qApp->setProperty("test.devices", QVariant());
            qApp->setProperty("test.mono.brightnessSteps", QVariant());
        });
        qputenv("AMBIENT_LED_DRIVER_PATH", path + ":" + MULTI_DRIVER_DIR);
        qApp->setProperty("test.devices", QStringList { "one", "two", "mono" });
        qApp->setProperty("test.mono.brightnessSteps", 3);
        SettingsController c;
        QVERIFY(c.addDevice("test-strips:one"));
        QVERIFY(c.addDevice("test-strips:two"));
        QVERIFY(c.addDevice("test-strips:mono"));
        QVERIFY(c.addPreset(false));
        QCOMPARE(c.presetDevices().size(), 4);
        auto* first = c.presetDevices()[1].value<SettingsController*>();
        auto* second = c.presetDevices()[2].value<SettingsController*>();
        auto* mono = c.presetDevices()[3].value<SettingsController*>();
        const auto target = [](SettingsController* editor, const QVariantList& keys) {
            return QVariantMap { { "profile", editor->profileInfo()["id"] }, { "layer", -1 },
                { "keys", keys } };
        };
        const QVariantList selected { target(first, { 7 }), target(second, { 42 }) };
        QVERIFY(c.deviceSelectionCapabilities(selected)["color"].toBool());
        QVERIFY(c.deviceSelectionCapabilities(selected)["brightness"].toBool());
        const auto solid = c.editDeviceSelection(selected, "new", { { "type", "solid" } });
        QVERIFY(!solid.isEmpty());
        c.editDeviceSelection(selected, "solid", { { "id", solid }, { "value", "#c05020" } });
        QCOMPARE(first->assignedEffectOptions(solid)["solidColor"].toString(), QString("#c05020"));
        QCOMPARE(second->assignedEffectOptions(solid)["solidColor"].toString(), QString("#c05020"));
        auto state = c.deviceSelectionEffects(selected);
        QCOMPARE(state["items"].toList().size(), 1);
        QCOMPARE(state["count"].toInt(), 2);
        QVERIFY(!state["mixed"].toBool());
        QVERIFY(first->selectionEffects(-1, { 42 })["items"].toList().isEmpty());
        QVERIFY(second->selectionEffects(-1, { 7 })["items"].toList().isEmpty());

        first->addAssignedEffect(-1, { 1 }, solid);
        const auto localized
            = c.editDeviceSelection(selected, "localize", { { "id", solid }, { "above", false } });
        QVERIFY(!localized.isEmpty() && localized != solid);
        c.editDeviceSelection(selected, "solid", { { "id", localized }, { "value", "#2040c0" } });
        QCOMPARE(first->assignedEffectOptions(solid)["solidColor"].toString(), QString("#c05020"));
        QCOMPARE(first->assignedEffectOptions(localized)["solidColor"].toString(), QString("#2040c0"));
        QCOMPARE(second->assignedEffectOptions(localized)["solidColor"].toString(), QString("#2040c0"));
        QCOMPARE(c.deviceSelectionEffects(selected)["items"].toList().size(), 1);

        // A copied local ID on an unselected element in another device is isolated.
        const QVariantList partial { target(first, { 42 }), target(second, { 42 }) };
        QCOMPARE(c.editDeviceSelection(partial, "localize", { { "id", localized }, { "above", false } }),
            localized);
        c.editDeviceSelection(partial, "solid", { { "id", localized }, { "value", "#20c040" } });
        QCOMPARE(first->assignedEffectOptions(localized)["solidColor"].toString(), QString("#2040c0"));
        QCOMPARE(second->assignedEffectOptions(localized)["solidColor"].toString(), QString("#20c040"));

        const QVariantList mixed { target(first, { 7 }), target(mono, { }) };
        const auto caps = c.deviceSelectionCapabilities(mixed);
        QVERIFY(!caps["color"].toBool());
        QVERIFY(!caps["ambient"].toBool());
        QVERIFY(caps["brightness"].toBool());
        QCOMPARE(caps["brightnessSteps"].toInt(), 0);
        QCOMPARE(c.deviceSelectionCapabilities({ target(mono, { }) })["brightnessSteps"].toInt(), 3);
        QVERIFY(!c.editDeviceSelection(mixed, "new", { { "type", "solid" } }).isEmpty());
        QVERIFY(!c.editDeviceSelection(mixed, "new", { { "type", "ambient" } }).isEmpty());
        const auto brightness = c.editDeviceSelection(mixed, "new", { { "type", "brightness" } });
        QVERIFY(!brightness.isEmpty());
        c.editDeviceSelection(mixed, "brightness", { { "id", brightness }, { "value", 35 } });
        QCOMPARE(first->assignedEffectOptions(brightness)["brightness"].toInt(), 35);
        QCOMPARE(mono->assignedEffectOptions(brightness)["brightness"].toInt(), 35);
        const auto microphone = c.editDeviceSelection(mixed, "new", { { "type", "microphone" } });
        QVERIFY(!microphone.isEmpty());
        c.editDeviceSelection(
            mixed, "microphone", { { "id", microphone }, { "key", "color" }, { "value", "#123456" } });
        c.editDeviceSelection(
            mixed, "microphone", { { "id", microphone }, { "key", "pauseOnBattery" }, { "value", true } });
        c.editDeviceSelection(mixed, "update",
            { { "id", microphone }, { "wasAbove", false }, { "above", true }, { "opacity", 45 } });
        for (auto* editor : { first, mono }) {
            QCOMPARE(
                editor->assignedEffectOptions(microphone)["microphoneColor"].toString(), QString("#123456"));
            QVERIFY(editor->assignedEffectOptions(microphone)["pauseOnBattery"].toBool());
        }
        const auto micState = c.deviceSelectionEffects(mixed)["items"].toList().last().toMap();
        QCOMPARE(micState["id"].toString(), microphone);
        QVERIFY(micState["above"].toBool());
        QCOMPARE(micState["opacity"].toInt(), 45);
        c.editDeviceSelection(mixed, "update",
            { { "id", microphone }, { "wasAbove", true }, { "above", false }, { "opacity", 45 } });
        // An invalid destination invalidates the entire operation.
        auto before = c.collection();
        auto invalid = mixed;
        invalid.append(QVariantMap { { "profile", "missing" }, { "layer", -1 } });
        c.editDeviceSelection(invalid, "clear", { { "inherit", false } });
        QCOMPARE(c.collection(), before);
        // Capacity is checked across all destinations before modifying any of them.
        while (first->selectionEffects(-1, { 7 })["items"].toList().size() < 32)
            QVERIFY(!first->addLocalEffect(-1, { 7 }, "brightness").isEmpty());
        before = c.collection();
        QVERIFY(c.editDeviceSelection(selected, "new", { { "type", "solid" } }).isEmpty());
        QCOMPARE(c.collection(), before);
        QString error;
        QVERIFY2(savePresets(c.collection(), &error), qPrintable(error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c.collection());
        QCOMPARE(qApp->property("test.controls.writes").toInt(), 0);
    }
    void defaultsMetadataAndPendingEdits()
    {
        QPluginLoader loader(EFFECT_PLUGIN_PATH);
        const auto metadata = loader.metaData().value("MetaData").toObject();
        QVERIFY(!metadata.contains("X-KDE-ConfigModule"));
        QCOMPARE(metadata.value("KPlugin").toObject().value("Version").toString(), QString("0.27.0"));
        SettingsController c;
        QTRY_VERIFY(c.lightbarAvailable());
        QCOMPARE(c.device().value("bodyColor").toString(), QString("#252932"));
        QCOMPARE(c.device().value("keyColor").toString(), QString("#121722"));
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
        c.assignKeys(-1, { 12 }, 1, "#0000ff");
        c.assignKeys(0, { 12 }, 1, "#ff0000");
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
        QCOMPARE(c.pressedKeys(), QVariantList { 71 });
        QSignalSpy pressedChanged(&c, &SettingsController::pressedKeysChanged);
        key(QEvent::KeyPress, KEY_W, Qt::Key_W);
        QCOMPARE(c.previewLayer(), upper); // Input indicators update even if the active layer stays the same.
        QCOMPARE(c.pressedKeys(), (QVariantList { 12, 71 }));
        QCOMPARE(pressedChanged.size(), 1);
        key(QEvent::KeyRelease, KEY_W, Qt::Key_W);
        QCOMPARE(c.pressedKeys(), QVariantList { 71 });
        QCOMPARE(c.liveKeyPreview()[12].toMap()["color"].toString(), QString("#0000ff"));
        QCOMPARE(c.liveKeyPreview()[14].toMap()["color"].toString(), QString("#00ff00"));
        key(QEvent::KeyPress, KEY_A, Qt::Key_A);
        QCOMPARE(c.previewLayer(), 0); // More conditions win even in the earlier row.
        QCOMPARE(c.liveKeyPreview()[12].toMap()["color"].toString(), QString("#ff0000"));
        QCOMPARE(c.liveKeyPreview()[14], c.keyPreview(0)[14]);
        QCOMPARE(c.liveKeyPreview()[14].toMap()["color"], c.keyPreview(-1)[14].toMap()["color"]);
        key(QEvent::KeyRelease, KEY_A, Qt::Key_A);
        QCOMPARE(c.previewLayer(), upper);
        key(QEvent::KeyPress, KEY_LEFTCTRL, Qt::Key_Control);
        QCOMPARE(c.previewLayer(), -1); // A Shift-only layer excludes Ctrl + Shift.
        QCOMPARE(c.liveKeyPreview()[12].toMap()["color"].toString(), QString("#0000ff"));
        key(QEvent::KeyRelease, KEY_LEFTCTRL, Qt::Key_Control);
        QCOMPARE(c.previewLayer(), upper);
        key(QEvent::KeyRelease, KEY_RIGHTSHIFT, Qt::Key_Shift);
        QCOMPARE(c.previewLayer(), -1);
        QCOMPARE(c.liveKeyPreview(), c.keyPreview(-1));
        QVERIFY(c.pressedKeys().isEmpty());
        key(QEvent::KeyPress, KEY_LEFTSHIFT, Qt::Key_Shift);
        QCOMPARE(c.pressedKeys(), QVariantList { 70 });
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
        QCOMPARE(qApp->property("test.controls.writes").toInt(), 0);
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
        const auto& profile = c.collection().presets.first().devices.last();
        const auto afterGradient = profile.effects[7];
        QVERIFY(std::none_of(
            afterGradient.begin(), afterGradient.end(), [&](const auto& e) { return e.id == localColor; }));

        QCOMPARE(c.gradients(-1).size(), 1);
        QCOMPARE(profile.gradients.first().start, QColor(255, 0, 0, 128));
        QVERIFY(!QFile::exists(presetDirectory() + "/settings-v2.json"));
        c.selectDevice(0);
        c.setLightbarEnabled(false);
        c.selectDevice(2);
        QVERIFY(!c.lightbarAvailable());
        c.save();
        QTRY_VERIFY(!c.busy());
        QVERIFY(!c.dirty());
        QCOMPARE(qApp->property("test.controls.writes").toInt(), 1);
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
        QCOMPARE(loaded.collection().presets.size(), 1);
    }
    void keyboardGroupsLayersAndPresetsPersist()
    {
        FakeKWin fake;
        SettingsController c;
        QVERIFY(c.editableKeys());
        QCOMPARE(c.keys().size(), TestFixture::KeyCount);
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
        c.setGradient(-1, -1, FirstStripId, 1001, 1023, "#ff0000", "#0000ff", 50, true, effectId);
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
        c.assignKeys(-1, { 4 }, int(KeyMode::Static), "#123456");
        const auto original = c.collection().presets[0];
        c.addNotificationLayout();
        const auto id = c.effectPreset()["id"].toString();
        QCOMPARE(c.effectPreset()["type"].toString(), QString("notifications"));
        for (int key : { 4, 5, 6, 7 })
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
        c.addAssignedEffect(0, { 4 }, id);
        QCOMPARE(c.collection().presets[0].effects[LightbarId], (EffectList { { DefaultEffectId }, { id } }));
        QCOMPARE(c.collection().presets[0].effectOverrides[4], (EffectList { { id } }));
        QCOMPARE(c.collection().presets[0].layers[0].effects[4],
            (EffectList { { InlineColorId, 100, QColor("#123456") }, { id } }));
        QVERIFY(!c.keyPreview(0)[4].toMap()["inherited"].toBool());
        c.clearKeyLighting(0, { 5 }, false);
        QVERIFY(c.collection().presets[0].layers[0].effects.contains(5));
        c.clearKeyLighting(0, { 5 }, true);
        QVERIFY(!c.collection().presets[0].layers[0].effects.contains(5));
        QTemporaryDir dir;
        c.exportFile(QUrl::fromLocalFile(dir.filePath("notifications.json")));
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        c.setNotification("color", "#00ff00");
        c.importFile(QUrl::fromLocalFile(dir.filePath("notifications.json")));
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto& imported = c.collection().presets.last();
        const auto remapped = imported.effects[LightbarId].last().id;
        QVERIFY(remapped != id);
        QCOMPARE(imported.layers[0].effects[4].last().id, remapped);
        QCOMPARE(imported.effectOverrides[7].last().id, remapped);
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
    void defaultPresetIsProtectedButItsLayersAndDevicesCanBeRemoved()
    {
        SettingsController c;
        const auto base = c.defaultId();
        const auto original = c.collection();
        QVERIFY(!c.removePreset());
        QCOMPARE(c.collection(), original);
        QVERIFY(!c.error().isEmpty());
        QVERIFY(c.addPreset(true));
        c.selectPreset(0);
        QVERIFY(!c.removePreset()); // Protection also applies when other presets exist.
        c.removeLayer(0);
        QVERIFY(c.preset()["layers"].toList().isEmpty());
        const auto configured = c.devices().first().toMap()["id"].toString();
        c.removePresetDevice(c.preset()["id"].toString());
        QVERIFY(c.presetDevices().isEmpty());
        c.refreshDevices();
        QVERIFY(c.presetDevices().isEmpty()); // Discovery must not undo explicit removal.
        QVERIFY(c.addPresetDevice(configured));
        QCOMPARE(c.presetDevices().size(), 1);
        c.selectPreset(1);
        QVERIFY(c.removePreset());
        QCOMPARE(c.collection().presets.size(), 1);
        QCOMPARE(c.defaultId(), base);
        c.save();
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto saved = c.collection();
        c.load();
        QCOMPARE(c.collection(), saved);
        QCOMPARE(c.presetDevices().size(), 1);
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
    void microphoneEditsStayPendingAndResetDefaults()
    {
        SettingsController c;
        const auto original = c.collection();
        QVERIFY(c.addEffect("microphone"));
        c.setSection("effects");
        QVERIFY(c.representsDefaults());
        c.setMicrophone("color", "#102030");
        c.setMicrophone("pauseOnBattery", true);
        QVERIFY(!c.representsDefaults());
        QVERIFY(c.dirty());
        QCOMPARE(lighting->reloads, 0);
        c.load();
        QCOMPARE(c.collection(), original);
        QVERIFY(c.addEffect("microphone"));
        c.setSection("effects");
        c.setMicrophone("color", "#102030");
        c.setMicrophone("pauseOnLowBattery", true);
        c.defaults();
        QVERIFY(c.representsDefaults());
        QCOMPARE(c.effectPreset()["microphoneColor"].toString(), QString("#ff0000"));
        QVERIFY(!c.effectPreset()["pauseOnLowBattery"].toBool());
        const auto savedId = c.effectPreset()["id"].toString();
        c.setMicrophone("color", "#c05020");
        for (const auto* bad : { "invalid", "transparent", "#80102030" })
            c.setMicrophone("color", bad);
        QCOMPARE(c.effectPreset()["microphoneColor"].toString(), QString("#c05020"));
        c.addAssignedEffect(-1, { 14 }, savedId);
        c.updateAssignedEffect(-1, { 14 }, savedId, false, true, 60);
        QCOMPARE(c.collection().presets[0].effectOverrides[14], (EffectList { { savedId, 60 } }));
        QCOMPARE(c.keyPreview(-1)[14].toMap()["microphoneId"].toString(), savedId);
        QVERIFY(c.keyPreview(-1)[14].toMap()["microphoneAbove"].toBool());
        const auto local = c.addLocalEffect(-1, { 15 }, "microphone");
        QVERIFY(!local.isEmpty());
        c.setMicrophone("color", "#203040", local);
        c.setMicrophone("pauseOnBattery", true, local);
        c.save();
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto saved = c.collection();
        c.load();
        QCOMPARE(c.collection(), saved);
        QTemporaryDir dir;
        c.exportFile(QUrl::fromLocalFile(dir.filePath("microphone.json")));
        c.setMicrophone("color", "#556677", savedId); // Exercise a conflicting shared definition.
        c.importFile(QUrl::fromLocalFile(dir.filePath("microphone.json")));
        QVERIFY2(c.error().isEmpty(), qPrintable(c.error()));
        const auto& imported = c.collection().presets.last();
        const auto importedShared = imported.effectOverrides[14].first().id;
        const auto importedLocal = imported.localEffects.first().id;
        QVERIFY(importedShared != savedId);
        QCOMPARE(importedLocal, local);
        QVERIFY(scopedEffectId(imported, importedLocal) != scopedEffectId(saved.presets[0], local));
        QCOMPARE(c.collection().effects.last().microphone.color, QColor("#c05020"));
        QCOMPARE(imported.localEffects.first().microphone.color, QColor("#203040"));
        QVERIFY(imported.localEffects.first().pauseOnBattery);
        const auto importedAssignments = imported.effects[15];
        QVERIFY(std::any_of(importedAssignments.cbegin(), importedAssignments.cend(),
            [&](const auto& entry) { return entry.id == importedLocal; }));
    }
    void microphoneSurvivesGradientReplacement()
    {
        const auto path = qgetenv("AMBIENT_LED_DRIVER_PATH");
        const auto restore = qScopeGuard([&] { qputenv("AMBIENT_LED_DRIVER_PATH", path); });
        qputenv("AMBIENT_LED_DRIVER_PATH", path + ":" + MULTI_DRIVER_DIR);
        SettingsController c;
        QVERIFY(c.addDevice("test-strips:one"));
        const auto local = c.addLocalEffect(-1, { 7 }, "microphone");
        QVERIFY(!local.isEmpty());
        QVERIFY(c.addEffect("microphone"));
        const auto shared = c.effectPreset()["id"].toString();
        c.addAssignedEffect(-1, { 7 }, shared);
        c.updateAssignedEffect(-1, { 7 }, shared, false, false, 35);
        QVERIFY(c.setGradient(-1, -1, FirstStripId, 7, 1, "#ff0000", "#0000ff", 100, false));
        auto items = c.selectionEffects(-1, { 7 })["items"].toList();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items[0].toMap()["id"].toString(), QString(InlineColorId));
        QCOMPARE(items[0].toMap()["color"].toString(), QString("#ff0000"));
        QCOMPARE(items[1].toMap()["id"].toString(), local);
        QCOMPARE(items[2].toMap()["id"].toString(), shared);
        QCOMPARE(items[2].toMap()["opacity"].toInt(), 35);
        QVERIFY(c.setGradient(-1, 0, FirstStripId, 7, 1, "#00ff00", "#0000ff", 100, true, DefaultEffectId));
        items = c.selectionEffects(-1, { 7 })["items"].toList();
        QCOMPARE(items.size(), 4);
        QCOMPARE(items[0].toMap()["type"].toString(), QString("ambient"));
        QCOMPARE(items[1].toMap()["color"].toString(), QString("#00ff00"));
        QCOMPARE(items[2].toMap()["id"].toString(), local);
        QCOMPARE(items[3].toMap()["id"].toString(), shared);
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
        auto collection = defaultCollection("test-keyboard");
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
        QCOMPARE(fake.savedAtLoad, (QList<bool> { false }));
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
        QCOMPARE(fake.savedAtUnload, (QList<bool> { false }));
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
        QVERIFY(!KConfigGroup(config, "Plugins").readEntry("ambientledEnabled", false));
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
    void disappearingCompositorDoesNotEnableStartup_data()
    {
        QTest::addColumn<bool>("previouslyEnabled");
        QTest::newRow("initially-disabled") << false;
        QTest::newRow("previously-enabled") << true;
    }
    void disappearingCompositorDoesNotEnableStartup()
    {
        QFETCH(bool, previouslyEnabled);
        KConfigGroup group(config, "Plugins");
        group.writeEntry("ambientledEnabled", previouslyEnabled);
        QVERIFY(group.sync());
        FakeKWin fake;
        fake.loaded.clear();
        fake.deferLoadReply = true;
        SettingsController c;
        QTRY_VERIFY(c.effect()->available());
        c.effect()->setEnabled(true);
        c.save();
        QTRY_COMPARE(fake.loads.size(), 1);
        QVERIFY(c.effect()->busy());
        QVERIFY(!KConfigGroup(config, "Plugins").readEntry("ambientledEnabled", false));
        fake.bus.unregisterService("org.kde.KWin");
        QTRY_VERIFY_WITH_TIMEOUT(!c.busy(), 5000);
        QVERIFY(c.effect()->dirty());
        QVERIFY(!KConfigGroup(config, "Plugins").readEntry("ambientledEnabled", false));
    }
    void failedUnloadStillDisablesStartup()
    {
        FakeKWin fake;
        fake.refuseUnload = true;
        KConfigGroup group(config, "Plugins");
        group.writeEntry("ambientledEnabled", true);
        QVERIFY(group.sync());
        SettingsController c;
        QTRY_VERIFY(c.effect()->loaded());
        c.effect()->setEnabled(false);
        c.save();
        QTRY_VERIFY(!c.busy());
        QVERIFY(c.effect()->loaded());
        QVERIFY(c.effect()->dirty());
        QCOMPARE(fake.savedAtUnload, (QList<bool> { false }));
        QVERIFY(!group.readEntry("ambientledEnabled", true));
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
};
int main(int argc, char** argv)
{
    // Never let the configuration plugin reach the real system bus in tests.
    const auto testBus = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    if (testBus.isEmpty())
        return 1;
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", testBus);
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
