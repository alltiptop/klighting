// SPDX-License-Identifier: GPL-3.0-or-later
#include "effect_palette.h"
#include "preset_reader_p.h"
#include "preset_transfer.h"
#include "presets.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <linux/input-event-codes.h>
using namespace Ambient;
class PresetsTest : public QObject
{
    Q_OBJECT
    static std::optional<QByteArray> readFile(const QString& path, QString* error)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = "Cannot read " + path;
            return std::nullopt;
        }
        return file.readAll();
    }
private Q_SLOTS:
    void init() { QVERIFY(QDir(presetDirectory()).removeRecursively()); }
    void readerRecoversWhenPublishedIndexReplacesItsFiles()
    {
        const auto old = defaultCollection("test");
        auto next = old;
        next.presets[0].name = "New preset";
        next.effects[0].ambient.minimum = 17;
        QString error;
        QVERIFY(savePresets(old, &error));
        bool published = false, saved = false;
        const auto loaded = Internal::loadPresets(
            old, &error, [&](const QString& path, QString* readError) -> std::optional<QByteArray> {
                if (!published && path.contains("/presets/")) {
                    published = true;
                    QString saveError;
                    saved = savePresets(next, &saveError);
                }
                return readFile(path, readError);
            });
        QVERIFY(saved);
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(*loaded, next); // Never mix the old preset with the new effect.
        QVERIFY(error.isEmpty());
    }
    void unchangedIndexKeepsMissingFileError()
    {
        const auto original = defaultCollection("test");
        QString error;
        QVERIFY(savePresets(original, &error));
        bool removed = false;
        QString missing;
        const auto loaded = Internal::loadPresets(
            original, &error, [&](const QString& path, QString* readError) -> std::optional<QByteArray> {
                if (!removed && path.contains("/presets/")) {
                    missing = path;
                    removed = QFile::remove(path);
                }
                return readFile(path, readError);
            });
        QVERIFY(removed);
        QVERIFY(!loaded);
        QVERIFY(error.contains(missing));
    }
    void continuouslyChangingIndexHasBoundedRetries()
    {
        auto current = defaultCollection("test");
        QString error;
        QVERIFY(savePresets(current, &error));
        int writes = 0;
        bool saved = true;
        const auto loaded = Internal::loadPresets(
            current, &error, [&](const QString& path, QString* readError) -> std::optional<QByteArray> {
                if (path.contains("/presets/")) {
                    // Stop the fixture too: a missing retry bound must fail, not hang CI.
                    if (++writes <= 5) {
                        current.presets[0].name = QString("Revision %1").arg(writes);
                        QString saveError;
                        saved &= savePresets(current, &saveError);
                    }
                }
                return readFile(path, readError);
            });
        QVERIFY(saved);
        QVERIFY(!loaded);
        QVERIFY(writes > 1 && writes <= 5);
        QVERIFY(error.contains("keep changing"));
    }
    void multiDevicePresetsRoundTripAndScopeLocalEffects()
    {
        auto c = defaultCollection("test");
        auto& scene = c.presets.first();
        scene.multiDevice = true;
        DeviceProfile a, b;
        a.id = "profile-one";
        a.targetDevice = "test:one";
        a.keyboardDeviceId = "test";
        a.effects[DefaultAssignment] = { { "local" } };
        EffectPreset local;
        local.id = "local";
        local.type = "solid";
        local.solidColor = Qt::red;
        a.localEffects.append(local);
        b = a;
        b.id = "profile-two";
        b.targetDevice = "test:two";
        b.localEffects[0].solidColor = Qt::green;
        scene.devices = { a, b };
        QVERIFY(mergePresetLayers(scene));
        c.devices = QList<DeviceConfiguration> { { "test", "one", "test", "One" },
            { "test", "two", "test", "Two" } };
        c.rules.append({ "Editor", "editor", scene.id });
        QString error;
        QVERIFY2(savePresets(c, &error), qPrintable(error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c);
        QCOMPARE(devicePreset(c, c.devices->at(1), "editor"), 0);
        QCOMPARE(profileFor(scene, c.devices->at(1))->id, b.id);
        const auto effects = runtimeEffects(c);
        QCOMPARE(effects[1].id, QString("profile-one:local"));
        QCOMPARE(effects[2].id, QString("profile-two:local"));
        QTemporaryDir dir;
        QVERIFY(exportPreset(scene, c.effects, dir.filePath("scene.json"), &error));
        const auto imported = importPreset(dir.filePath("scene.json"), &error);
        QVERIFY2(imported, qPrintable(error));
        QCOMPARE(imported->keyboard, scene);
        auto json = presetJson(scene);
        auto devices = json["devices"].toArray();
        devices.append(devices.first());
        json["devices"] = devices;
        QVERIFY(!parsePreset(QJsonDocument(json).toJson(), &error));
        scene.devices[1].effects[DefaultAssignment] = { { "missing" } };
        QVERIFY(!savePresets(c, &error));
        scene.devices.clear();
        QVERIFY2(savePresets(c, &error), qPrintable(error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c);
    }
    void sharedLayersMigrateAndSelectOneLayerForEveryDevice()
    {
        LightingPreset rgb, mono;
        rgb.id = "rgb-profile";
        rgb.keyboardDeviceId = "rgb";
        rgb.layers = { { "Shift", { "shift" }, { } }, { "Shift+A", { "shift", "key:30" }, { } } };
        rgb.layers[0].effects[1] = { { InlineColorId, 100, Qt::red } };
        rgb.layers[1].effects[1] = { { InlineColorId, 100, Qt::green } };
        mono.id = "mono-profile";
        mono.keyboardDeviceId = "mono";
        mono.effects[DefaultAssignment] = { { "bright" } };
        mono.layers[0].effects[DefaultAssignment] = { { "dim" } };
        auto a = presetJson(rgb), b = presetJson(mono);
        a["targetDevice"] = "test:rgb";
        b["targetDevice"] = "test:mono";
        QJsonObject old { { "format", "kwin-ambient-led-preset" }, { "version", 6 }, { "id", "scene" },
            { "name", "Scene" }, { "devices", QJsonArray { a, b } } };
        QString error;
        auto scene = parsePreset(QJsonDocument(old).toJson(), &error);
        QVERIFY2(scene, qPrintable(error));
        QCOMPARE(scene->layerConditions, (QList<QStringList> { { "shift" }, { "shift", "key:30" } }));
        QCOMPARE(scene->devices[0].layers.size(), 2);
        QCOMPARE(scene->devices[1].layers.size(), 2);
        QVERIFY(scene->devices[1].layers[1].effects.isEmpty());
        DeviceDescription color { "rgb", "RGB", DeviceCapability::Color | DeviceCapability::PerKeyColor,
            { { 1, "A", { } } }, { } };
        DeviceDescription brightness { "mono", "Mono", DeviceCapability::Brightness,
            { { 10, "A", { } }, { 20, "B", { } } }, { } };
        ModifierState state;
        state.pressed.insert(KEY_LEFTSHIFT);
        int layer = activePresetLayer(*scene, state);
        QCOMPARE(layer, 0);
        QCOMPARE(
            deviceEffectsForLayer(scene->devices[0], color, layer).value(1).first().color, QColor(Qt::red));
        QCOMPARE(deviceEffectsForLayer(scene->devices[1], brightness, layer).value(10),
            (EffectList { { "dim" } }));
        QCOMPARE(deviceEffectsForLayer(scene->devices[1], brightness, layer).value(20),
            (EffectList { { "dim" } }));
        state.pressed.insert(KEY_A);
        layer = activePresetLayer(*scene, state);
        QCOMPARE(layer, 1);
        QCOMPARE(
            deviceEffectsForLayer(scene->devices[0], color, layer).value(1).first().color, QColor(Qt::green));
        // An unassigned shared layer inherits Base, never another device's simpler matching layer.
        QCOMPARE(deviceEffectsForLayer(scene->devices[1], brightness, layer).value(10),
            (EffectList { { "bright" } }));
        scene->devices[1].layers[1].effects[DefaultAssignment] = { };
        QVERIFY(deviceEffectsForLayer(scene->devices[1], brightness, layer).value(10).isEmpty());
        auto json = presetJson(*scene);
        QCOMPARE(json["version"].toInt(), 7);
        const auto payload = json["devices"]
                                 .toArray()[0]
                                 .toObject()["keyboard"]
                                 .toObject()["layers"]
                                 .toArray()[0]
                                 .toObject();
        QVERIFY(!payload.contains("conditions"));
        QVERIFY(!payload.contains("name"));
        const auto parsed = parsePreset(QJsonDocument(json).toJson(), &error);
        QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(*parsed, *scene);
        auto invalid = json;
        invalid["layers"] = QJsonArray { QJsonArray { "shift", "ctrl" }, QJsonArray { "ctrl", "shift" } };
        QVERIFY(!parsePreset(QJsonDocument(invalid).toJson(), &error));
        invalid["layers"] = QJsonArray { QJsonArray { "shift" } };
        QVERIFY(!parsePreset(QJsonDocument(invalid).toJson(), &error));
        invalid["layers"] = QJsonArray { QJsonArray { "unknown" }, QJsonArray { "shift" } };
        QVERIFY(!parsePreset(QJsonDocument(invalid).toJson(), &error));
    }
    void profileTransferMatchesKeysAndFallsBackByCapabilities()
    {
        DeviceInstance source, target;
        source.description = { "a", "Source", DeviceCapability::Color | DeviceCapability::PerKeyColor,
            { { 1, "A", { } }, { 2, "B", { } }, { 3, "Extra", { } } }, { } };
        source.options["InputKeyCodes"] = QVariantMap { { "1", KEY_A }, { "2", KEY_B } };
        target.description = { "b", "Target", DeviceCapability::Color | DeviceCapability::PerKeyColor,
            { { 2, "A", { } }, { 1, "B", { } }, { 4, "New", { } } }, { } };
        target.options["InputKeyCodes"] = QVariantMap { { "2", KEY_A }, { "1", KEY_B } };
        DeviceProfile p;
        p.keyboardDeviceId = "a";
        p.effects[DefaultAssignment] = { { InlineColorId, 100, Qt::blue } };
        p.effects[1] = { { InlineColorId, 100, Qt::red } };
        p.effects[2] = { { InlineColorId, 100, Qt::green } };
        p.effects[3] = { { DefaultEffectId } };
        p.layers[0].effects[DefaultAssignment] = { { InlineColorId, 100, Qt::yellow } };
        const auto transferred = transferProfile(p, source, target, { EffectPreset() });
        QVERIFY(!transferred.warnings.isEmpty());
        QCOMPARE(transferred.profile.effects[2].first().color, QColor(Qt::red));
        QCOMPARE(transferred.profile.effects[1].first().color, QColor(Qt::green));
        QVERIFY(!transferred.profile.effects.contains(3));
        QVERIFY(!transferred.profile.effects.contains(4));
        const auto resolved = deviceEffects(transferred.profile, target.description, { });
        QCOMPARE(resolved[4].first().color, QColor(Qt::blue));
        auto mono = target;
        mono.description.capabilities = DeviceCapability::Brightness;
        mono.options["BrightnessSteps"] = 3;
        p.effects[DefaultAssignment] = { { DefaultEffectId }, { InlineColorId, 100, Qt::red } };
        auto simple = transferProfile(p, source, mono, { EffectPreset() });
        QCOMPARE(simple.profile.layers.size(), p.layers.size());
        QVERIFY(simple.profile.layers[0].styles.isEmpty());
        QCOMPARE(simple.profile.layers[0].effects.value(DefaultAssignment),
            p.layers[0].effects.value(DefaultAssignment));
        QCOMPARE(simple.profile.effects.size(), 1);
        QCOMPARE(simple.profile.effects[DefaultAssignment], p.effects[DefaultAssignment]);
        QVERIFY(!simple.warnings.join(' ').contains("Ambient will be omitted"));
        mono.options["BrightnessSteps"] = 4;
        simple = transferProfile(p, source, mono, { EffectPreset() });
        QCOMPARE(simple.profile.effects[DefaultAssignment].size(), 2);
        QCOMPARE(p.effects[DefaultAssignment].size(), 2); // Conversion never edits its source.
        const auto identical = transferProfile(p, source, source, { EffectPreset() });
        QCOMPARE(identical.profile.effects, p.effects);
        QVERIFY(identical.warnings.isEmpty());
        // Labels alone must not guess which side of an ambiguous modifier was meant.
        source.description.keys = { { 1, "Shift", { } }, { 2, "Shift", { } } };
        source.options.clear();
        target.description.keys = { { 9, "Shift", { } } };
        target.options.clear();
        const auto ambiguous = transferProfile(p, source, target, { EffectPreset() });
        QVERIFY(!ambiguous.profile.effects.contains(9));
        // Strips use physical order, never the unrelated numeric element IDs.
        source.description.keys.clear();
        target.description.keys.clear();
        source.description.strips
            = { { FirstStripId, "From", { { 1, "1", { } }, { 2, "2", { } }, { 3, "3", { } } } } };
        target.description.capabilities = DeviceCapability::Color | DeviceCapability::PerStripColor;
        target.description.strips = { { FirstStripId, "To", { { 9, "1", { } }, { 4, "2", { } } } } };
        const auto strip = transferProfile(p, source, target, { EffectPreset() });
        QCOMPARE(strip.profile.effects[9].first().color, QColor(Qt::red));
        QCOMPARE(strip.profile.effects[4].first().color, QColor(Qt::green));
        QVERIFY(!strip.profile.effects.contains(3));
        QVERIFY(!strip.warnings.isEmpty());
    }
    void identifiersMustMatchWholeInput()
    {
        QString error;
        auto c = defaultCollection("test");
        c.effects[0].id += '\n';
        QVERIFY(!parseEffectPreset(QJsonDocument(effectPresetJson(c.effects[0])).toJson(), &error));
        c = defaultCollection("test");
        c.presets[0].id += '\n';
        QVERIFY(!savePresets(c, &error));
        QVERIFY(!QFile::exists(presetDirectory() + "/settings-v2.json"));
    }
    void brightnessPresetsRoundTripWithDefaultAndStrictRange()
    {
        EffectPreset effect;
        effect.id = "dim";
        effect.name = "Dim";
        effect.type = "brightness";
        effect.brightness = 35;
        QString error;
        auto json = effectPresetJson(effect);
        auto parsed = parseEffectPreset(QJsonDocument(json).toJson(), &error);
        QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(*parsed, effect);
        json["options"] = QJsonObject();
        parsed = parseEffectPreset(QJsonDocument(json).toJson(), &error);
        QVERIFY(parsed);
        QCOMPARE(parsed->brightness, 100);
        for (const auto& bad : QJsonArray { -1, 101, 1.5, "35", true, QJsonValue::Null }) {
            json["options"] = QJsonObject { { "brightness", bad } };
            QVERIFY(!parseEffectPreset(QJsonDocument(json).toJson(), &error));
        }
        auto c = defaultCollection("fixture");
        c.effects.append(effect);
        c.presets[0].effects[DefaultAssignment] = { { DefaultEffectId }, { effect.id } };
        auto local = effect;
        local.id = "local-dim";
        local.brightness = 60;
        c.presets[0].localEffects.append(local);
        c.presets[0].effects[9] = { { DefaultEffectId }, { local.id } };
        QVERIFY2(savePresets(c, &error), qPrintable(error));
        QCOMPARE(*loadPresets(defaultCollection("fixture"), &error), c);
        QTemporaryDir dir;
        QVERIFY(exportPreset(c.presets[0], c.effects, dir.filePath("preset.json"), &error));
        const auto bundle = importPreset(dir.filePath("preset.json"), &error);
        QVERIFY2(bundle, qPrintable(error));
        QCOMPARE(bundle->keyboard, c.presets[0]);
        QVERIFY(bundle->effects.contains(effect));
        QVERIFY(exportEffectPreset(effect, dir.filePath("effect.json"), &error));
        QCOMPARE(*importEffectPreset(dir.filePath("effect.json"), &error), effect);
    }
    void brightnessFiltersRespectOrderOpacityAndNotificationOverrides()
    {
        DeviceDescription d { "fixture", "Fixture",
            DeviceCapability::Color | DeviceCapability::Brightness | DeviceCapability::PerKeyColor
                | DeviceCapability::Lightbar | DeviceCapability::PerStripColor,
            { { 1, "A", { 0, 0, 1, 1 } }, { 2, "B", { 1, 0, 1, 1 } } }, { } };
        d.strips = { { FirstStripId, "Strip", { { 100, "1", { } }, { 101, "2", { } } } } };
        auto c = defaultCollection(d.id);
        auto& p = c.presets[0];
        EffectPreset solid, dim;
        solid.id = "color";
        solid.type = "solid";
        solid.solidColor = QColor(200, 100, 40);
        dim.id = "dim";
        dim.type = "brightness";
        dim.brightness = 50;
        p.effects[DefaultAssignment] = { { solid.id }, { dim.id } };
        p.effects[2] = { { dim.id }, { solid.id } };
        p.effects[100] = { { solid.id }, { dim.id, 50 } };
        p.effects[101] = { };
        EffectPalette palette;
        palette.configure({ solid, dim }, { solid.id, dim.id });
        QCOMPARE(palette.samplingFps(), 0);
        QCOMPARE(palette.ledFps(), 0);
        auto frame = [&] {
            return lightingColors(p, deviceKeyStyles(p, d, { }), d, palette.colors(),
                deviceEffects(p, d, { }), { { "notify", Qt::red } }, palette.brightnessFilters());
        };
        QCOMPARE(frame().keys, (QList<QColor> { QColor(100, 50, 20), solid.solidColor }));
        QCOMPARE(frame().strips[FirstStripId], (QList<QColor> { QColor(150, 75, 30), Qt::black }));
        p.effectOverrides[1] = { { "notify" } };
        QCOMPARE(frame().keys[0], QColor(Qt::red));
        p.effectOverrides.clear();
        // Uniform/brightness-only devices use the same default list, without per-key selection.
        d.capabilities = DeviceCapability::Brightness;
        d.strips.clear();
        QCOMPARE(frame().uniform, QColor(100, 50, 20));
        dim.brightness = 0;
        palette.configure({ solid, dim }, { solid.id, dim.id });
        QCOMPARE(frame().uniform, QColor(Qt::black));
        dim.brightness = 100;
        palette.configure({ solid, dim }, { solid.id, dim.id });
        QCOMPARE(frame().uniform, solid.solidColor);
        p.effects[DefaultAssignment] = { { dim.id } };
        dim.brightness = 33;
        palette.configure({ solid, dim }, { solid.id, dim.id });
        QCOMPARE(frame().uniform.name(), QString("#545454"));
        p.effects[DefaultAssignment] = { { dim.id, 0 } };
        QCOMPARE(frame().uniform, QColor(Qt::black));
        // Explicit black remains off, even when followed by a brightness filter.
        solid.solidColor = Qt::black;
        palette.configure({ solid, dim }, { solid.id, dim.id });
        p.effects[DefaultAssignment] = { { solid.id }, { dim.id } };
        QCOMPARE(frame().uniform, QColor(Qt::black));
        dim.brightness = 100;
        // Static filters remain defined while battery policy freezes screen-derived colors.
        palette.configure({ dim }, { }, { true, true, true });
        QCOMPARE(palette.brightnessFilters().value(dim.id), 100);
        QCOMPARE(palette.samplingFps(), 0);
        QCOMPARE(palette.ledFps(), 0);
    }
    void savedSnapshotIsCheckedDuringPublication()
    {
        const auto original = defaultCollection("test");
        auto first = original;
        first.presets[0].name = "First window";
        QString error;
        QVERIFY(savePresets(first, &error, &original, &original));
        auto second = original;
        second.presets[0].name = "Stale second window";
        QVERIFY(!savePresets(second, &error, &original, &original));
        QVERIFY(error.contains("changed on disk"));
        QCOMPARE(*loadPresets(original, &error), first);
    }
    void largeGroupsRoundTripWithoutLoss()
    {
        auto c = defaultCollection("test");
        for (int group = 0; group < 32; ++group)
            for (int key = 0; key < 1024; ++key)
                c.presets[0].groups[QString::number(group)].append(key);
        QVERIFY(serializeKeyGroups(c.presets[0].groups).size() > 65536);
        QString error;
        QVERIFY2(savePresets(c, &error), qPrintable(error));
        const auto loaded = loadPresets(defaultCollection(), &error);
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->presets[0].groups, c.presets[0].groups);
        c.presets[0].groups["too many"] = { 1 };
        QVERIFY(!savePresets(c, &error));
        QCOMPARE(loadPresets(defaultCollection(), &error)->presets[0].groups.size(), 32);
    }
    void uniformDeviceComposesGlobalNotifications()
    {
        DeviceDescription d { "uniform", "Uniform keyboard", DeviceCapability::Color,
            { { 1, "A", QRectF(0, 0, 1, 1) } }, { } };
        auto c = defaultCollection(d.id);
        auto& p = c.presets[0];
        p.effectOverrides[DefaultAssignment] = { { "notify" } };
        const auto assignments = deviceEffects(p, d, { });
        QCOMPARE(assignments.value(1), (EffectList { { DefaultEffectId }, { "notify" } }));
        const auto colors = lightingColors(p, deviceKeyStyles(p, d, { }), d,
            { { DefaultEffectId, Qt::blue } }, assignments, { { "notify", Qt::red } });
        QCOMPARE(colors.uniform, QColor(Qt::red));
        p.effects[DefaultAssignment] = { };
        QCOMPARE(deviceEffects(p, d, { }).value(1), (EffectList { { "notify" } }));
    }
    void defaultEffectCannotReferenceLocalDefinition()
    {
        auto c = defaultCollection("test");
        EffectPreset local;
        local.id = "local";
        c.presets[0].localEffects.append(local);
        c.presets[0].defaultEffectId = local.id;
        QString error;
        QVERIFY(!savePresets(c, &error));
        QVERIFY(!error.isEmpty());
    }
    void oversizedExportLeavesDestinationUntouched()
    {
        auto c = defaultCollection("test");
        for (int key = 0; key < 26000; ++key)
            c.presets[0].base[key] = { KeyMode::Static, Qt::red };
        QString error;
        QVERIFY2(parsePreset(QJsonDocument(presetJson(c.presets[0])).toJson(), &error), qPrintable(error));
        QTemporaryDir dir;
        const auto path = dir.filePath("bundle.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("previous");
        file.close();
        QVERIFY(!exportPreset(c.presets[0], c.effects, path, &error));
        QVERIFY(error.contains("1 MiB"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("previous"));
    }
    void devicesAndStableZoneIds()
    {
        auto collection = defaultCollection("test-keyboard");
        auto& p = collection.presets[0];
        QCOMPARE(p.keyboardDeviceId, QString("test-keyboard"));
        p.base[1000] = { KeyMode::Static, Qt::green };
        p.base[36] = { KeyMode::Static, Qt::red };
        p.base[LightbarId] = { KeyMode::Ambient, QColor("#80ff0000"), DefaultEffectId };
        p.groups["Extra"] = { 1000, LightbarId };
        QString error;
        const auto saved = presetJson(p);
        QCOMPARE(saved["version"].toInt(), 5);
        QVERIFY(!saved.contains("ambient"));
        const auto restored = parsePreset(QJsonDocument(saved).toJson(), &error);
        QVERIFY2(restored, qPrintable(error));
        QCOMPARE(*restored, p);
        DeviceDescription d { "test-keyboard", "Test",
            DeviceCapability::Color | DeviceCapability::PerKeyColor,
            { { 1000, "First", QRectF(0, 0, 10, 10) }, { 36, "Second", QRectF(12, 0, 10, 10) } }, { } };
        auto styles = deviceKeyStyles(p, d, { });
        QCOMPARE(styles[1000].color, QColor(Qt::green));
        d.id = "other-device";
        QVERIFY(deviceKeyStyles(p, d, { }).isEmpty());
        d.id = p.keyboardDeviceId;
        d.capabilities = DeviceCapability::Color | DeviceCapability::Lightbar;
        styles = deviceKeyStyles(p, d, { });
        QVERIFY(styles.isEmpty()); // A uniform-only driver cannot address the bar separately.
    }
    void stripGradientsRespectPhysicalOrderLayersAndNotifications()
    {
        DeviceDescription d { "strip", "Strip", DeviceCapability::Color | DeviceCapability::PerStripColor,
            { }, { },
            { { FirstStripId, "LEDs",
                { { 9, "1", { } }, { 2, "2", { } }, { 50, "3", { } }, { 7, "4", { } } } } } };
        auto c = defaultCollection("strip");
        auto& p = c.presets[0];
        p.base[FirstStripId] = { KeyMode::Static, Qt::green };
        p.gradients = { { FirstStripId, 9, 50, Qt::red, Qt::blue, false, { } } };
        auto styles = deviceKeyStyles(p, d, { });
        QVERIFY(activeEffects(p, styles, d).isEmpty());
        const auto colors = lightingColors(p, styles, d, { }).strips[FirstStripId];
        QCOMPARE(colors, (QList<QColor> { Qt::red, QColor(128, 0, 128), Qt::blue, Qt::green }));
        p.gradients[0].from = 50;
        p.gradients[0].to = 9;
        styles = deviceKeyStyles(p, d, { });
        QCOMPARE(styles[9].color, QColor(Qt::blue));
        QCOMPARE(styles[50].color, QColor(Qt::red));
        p.base[2] = { KeyMode::Static, Qt::yellow };
        QCOMPARE(deviceKeyStyles(p, d, { })[2].color, QColor(Qt::yellow));
        p.layers[0].styles[FirstStripId] = { KeyMode::Static, Qt::white };
        p.layers[0].styles[7] = { KeyMode::Off, Qt::transparent };
        ModifierState shift;
        shift.pressed = { KEY_LEFTSHIFT };
        styles = deviceKeyStyles(p, d, shift);
        QCOMPARE(styles[2].color, QColor(Qt::white));
        QCOMPARE(styles[7].mode, KeyMode::Off);
        p.effects[FirstStripId] = { { "notify" } };
        p.layers[0].effects[2] = { };
        const auto hidden = deviceEffects(p, d, shift);
        QVERIFY(hidden.value(2).isEmpty());
        QVERIFY(!hidden.contains(FirstStripId));
        const auto noFlash = lightingColors(p, styles, d, { }, hidden, { { "notify", Qt::red } });
        QCOMPARE(noFlash.strips[FirstStripId][1], QColor(Qt::white));
        p.effectOverrides[FirstStripId] = { { "notify" } };
        p.effects[2] = { { "lower" } };
        auto notifications = deviceEffects(p, d, shift);
        QCOMPARE(notifications[2], (EffectList { { "notify" } }));
        const auto flash = lightingColors(p, styles, d, { }, notifications, { { "notify", Qt::red } });
        QCOMPARE(flash.strips[FirstStripId], (QList<QColor> { Qt::red, Qt::red, Qt::red, Qt::red }));
        p.gradients[0].ambient = true;
        p.gradients[0].start.setAlpha(128);
        p.gradients[0].end.setAlpha(128);
        const auto mixed
            = lightingColors(p, deviceKeyStyles(p, d, { }), d, { { "ambient-default", Qt::green } });
        QCOMPARE(mixed.strips[FirstStripId][0], QColor(0, 127, 128));
        QString error;
        const auto restored = parsePreset(QJsonDocument(presetJson(p)).toJson(), &error);
        QVERIFY2(restored, qPrintable(error));
        QCOMPARE(*restored, p);
    }
    void legacyDeviceDefaultsDoNotOverrideTheSharedDefault()
    {
        auto c = defaultCollection("fixture");
        c.devices = QList<DeviceConfiguration> { { "driver", "instance", "fixture", "Device" } };
        QString error;
        QVERIFY2(savePresets(c, &error), qPrintable(error));
        QFile file(presetDirectory() + "/settings-v2.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto index = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto devices = index["devices"].toArray();
        auto device = devices[0].toObject();
        QVERIFY(!device.contains("preset"));
        // Previous files used null for None, an empty string for automatic
        // selection, or a separate preset ID. All now use the shared default.
        for (const QJsonValue& legacy :
            { QJsonValue(QJsonValue::Null), QJsonValue(""), QJsonValue("old-preset") }) {
            device["preset"] = legacy;
            devices[0] = device;
            index["devices"] = devices;
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(QJsonDocument(index).toJson());
            file.close();
            const auto loaded = loadPresets(defaultCollection("fixture"), &error);
            QVERIFY2(loaded, qPrintable(error));
            QCOMPARE(*loaded, c);
            QCOMPARE(devicePreset(*loaded, loaded->devices->first(), { }), 0);
        }
    }
    void applicationBindingsAlwaysOverrideDeviceDefault()
    {
        auto c = defaultCollection("fixture");
        auto application = c.presets.first();
        application.id = "editor";
        c.presets.append(application);
        c.rules.append({ "Editor", "editor", application.id, true });
        c.devices = QList<DeviceConfiguration> { { "driver", "instance", "fixture", "Device" } };
        QString error;
        QVERIFY2(savePresets(c, &error), qPrintable(error));
        QFile file(presetDirectory() + "/settings-v2.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto index = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        auto devices = index["devices"].toArray();
        auto device = devices[0].toObject();
        QVERIFY(!device.contains("followApplications"));
        device["followApplications"] = false; // Retired option must not disable application rules.
        devices[0] = device;
        index["devices"] = devices;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(index).toJson());
        file.close();
        const auto loaded = loadPresets(defaultCollection("fixture"), &error);
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(*loaded, c);
        const auto& configured = loaded->devices->first();
        QCOMPARE(devicePreset(*loaded, configured, "editor"), 1);
        QCOMPARE(devicePreset(*loaded, configured, "other"), 0);
        QCOMPARE(devicePreset(*loaded, configured, { }), 0);
    }
    void deviceListRoundTripsAndPreservesExplicitEmpty()
    {
        auto c = defaultCollection("strip");
        QString error;
        QVERIFY(savePresets(c, &error));
        QVERIFY(!loadPresets(defaultCollection(), &error)->devices.has_value());
        c.devices = QList<DeviceConfiguration> { { "driver", "serial:1", "strip", "External", true, true,
                                                     false, true, false },
            { "driver", "serial:2", "strip", "Offline", "", false, false, true } };
        QVERIFY2(savePresets(c, &error), qPrintable(error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c);
        auto wrong = c;
        wrong.devices->first().modelId = "wrong-model";
        QVERIFY(savePresets(wrong, &error));
        QCOMPARE(devicePreset(wrong, wrong.devices->first(), { }), -1);
        wrong = c;
        wrong.devices->append(wrong.devices->first());
        QVERIFY(!savePresets(wrong, &error));
        c.devices = QList<DeviceConfiguration> { };
        QVERIFY(savePresets(c, &error));
        const auto saved = loadPresets(defaultCollection(), &error);
        QVERIFY(saved->devices.has_value());
        QVERIFY(saved->devices->isEmpty());
    }
    void separatePresetsAndPortableBundle()
    {
        auto collection = defaultCollection();
        auto& p = collection.presets[0];
        p.layers.append({ "Ctrl + Right Shift", { "ctrl", "shift-right" }, inheritedKeyStyles() });
        p.layers.last().styles[4] = { KeyMode::Off, Qt::transparent };
        p.groups = { { "Movement", { 36, 50, 51, 52 } } };
        auto effect = collection.effects.first();
        effect.id = "night";
        effect.name = "Night";
        effect.ambient.minimum = 35;
        collection.effects.append(effect);
        auto other = p;
        other.id = "other";
        other.name = "Editor";
        other.defaultEffectId = "night";
        other.base[LightbarId] = { KeyMode::Ambient, QColor("#40ff0000"), DefaultEffectId };
        collection.presets.append(other);
        collection.rules.append({ "Code", "org.kde.kate", other.id, true });
        QString error;
        QVERIFY(!QFile::exists(presetDirectory() + "/settings-v2.json"));
        QVERIFY2(savePresets(collection, &error), qPrintable(error));
        const auto saved = loadPresets(defaultCollection(), &error);
        QVERIFY2(saved, qPrintable(error));
        QCOMPARE(*saved, collection);
        collection.effects[1].ambient.temperature = 45;
        QVERIFY(savePresets(collection, &error));
        QCOMPARE(QDir(presetDirectory() + "/presets").entryList({ "*.json" }, QDir::Files).size(), 2);
        QCOMPARE(QDir(presetDirectory() + "/effects").entryList({ "*.json" }, QDir::Files).size(), 2);
        QCOMPARE(*loadPresets(defaultCollection(), &error), collection);
        QTemporaryDir exported;
        QVERIFY(exportPreset(other, collection.effects, exported.filePath("share.json"), &error));
        const auto imported = importPreset(exported.filePath("share.json"), &error);
        QVERIFY2(imported, qPrintable(error));
        QCOMPARE(imported->keyboard, other);
        QCOMPARE(imported->effects, collection.effects);
        QVERIFY(exportEffectPreset(effect, exported.filePath("effect.json"), &error));
        QCOMPARE(*importEffectPreset(exported.filePath("effect.json"), &error), effect);
    }
    void missingEffectsAndMalformedAssignmentsFailWithoutSaving()
    {
        auto c = defaultCollection();
        QString error;
        QVERIFY(savePresets(c, &error));
        auto bad = c;
        bad.presets[0].base[17] = { KeyMode::Ambient, Qt::transparent, "missing" };
        QVERIFY(!savePresets(bad, &error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), c);
        bad = c;
        bad.effects.clear();
        QVERIFY(!savePresets(bad, &error));
        auto json = presetJson(c.presets[0]);
        auto keyboard = json["keyboard"].toObject();
        keyboard["base"] = QJsonArray { "1:unknown:", "65537:off:" };
        json["keyboard"] = keyboard;
        QVERIFY(!parsePreset(QJsonDocument(json).toJson(), &error));
    }
    void explicitEmptyEffectsSuppressAmbientFallback()
    {
        LightingPreset p;
        DeviceDescription device { p.keyboardDeviceId, "Test",
            DeviceCapability::Color | DeviceCapability::PerKeyColor, { { 1, "A", { } } }, { } };
        const KeyboardStyles styles { { 1, { KeyMode::Ambient, Qt::transparent, DefaultEffectId } } };
        const QMap<QString, QColor> palette { { DefaultEffectId, Qt::blue }, { "green", Qt::green } };
        EffectAssignments assignments;
        QCOMPARE(activeEffects(p, styles, device, assignments), (QSet<QString> { DefaultEffectId }));
        QCOMPARE(lightingColors(p, styles, device, palette, assignments).uniform, QColor(Qt::blue));
        assignments[1] = { };
        QVERIFY(activeEffects(p, styles, device, assignments).isEmpty());
        QCOMPARE(lightingColors(p, styles, device, palette, assignments).uniform, QColor(Qt::black));
        assignments[1] = { { "green" } };
        QCOMPARE(activeEffects(p, styles, device, assignments), (QSet<QString> { "green" }));
        QCOMPARE(lightingColors(p, styles, device, palette, assignments).uniform, QColor(Qt::green));
    }
    void effectMixAndLightbarAreIndependent()
    {
        LightingPreset p;
        DeviceDescription d { p.keyboardDeviceId, "Test",
            DeviceCapability::PerKeyColor | DeviceCapability::Lightbar,
            { { 17, "A", { } }, { 44, "B", { } } }, { } };
        KeyboardStyles styles;
        styles[17] = { KeyMode::Ambient, QColor(255, 0, 0, 128), "blue" };
        styles[LightbarId] = { KeyMode::Static, QColor(0, 255, 0, 128) };
        const QMap<QString, QColor> palette { { DefaultEffectId, Qt::red }, { "blue", Qt::blue } };
        auto frame = lightingColors(p, styles, d, palette);
        QCOMPARE(frame.uniform, QColor(0, 128, 0));
        QCOMPARE(frame.keys, (QList<QColor> { QColor(128, 0, 127), QColor(Qt::red) }));
        QCOMPARE(activeEffects(p, styles, d), (QSet<QString> { DefaultEffectId, "blue" }));
        styles[17].color.setAlpha(0);
        QCOMPARE(lightingColors(p, styles, d, palette).keys[0], QColor(Qt::blue));
        styles[17].color.setAlpha(255);
        QCOMPARE(lightingColors(p, styles, d, palette).keys[0], QColor(Qt::red));
        QVERIFY(!activeEffects(p, styles, d).contains("blue"));
        styles[44] = styles[17];
        styles[LightbarId] = styles[17];
        QVERIFY(activeEffects(p, styles, d).isEmpty()); // Opaque colors need no screen capture.
        frame = lightingColors(p, styles, d, palette);
        QCOMPARE(frame.uniform, QColor(Qt::red));
        QVERIFY(frame.keys.isEmpty());
        styles[LightbarId] = { KeyMode::Off, Qt::transparent };
        frame = lightingColors(p, styles, d, palette);
        QCOMPARE(frame.uniform, QColor(Qt::black));
        QCOMPARE(frame.keys[0], QColor(Qt::red));
    }
    void powerSavingFreezesOnlySelectedAmbientEffects()
    {
        EffectPreset first, second;
        first.id = "first";
        second.id = "second";
        first.ambient = second.ambient = { 20, 60, 0, 25, 100, 0 };
        first.pauseOnBattery = true;
        second.pauseOnLowBattery = true;
        EffectPalette palette;
        palette.configure({ first, second }, { "first", "second" });
        palette.sample(Qt::red);
        palette.advance(100);
        palette.configure({ first, second }, { "first", "second" }, { true, true, false });
        palette.sample(Qt::blue);
        palette.advance(100);
        QCOMPARE(palette.colors()["first"], QColor(Qt::red));
        QCOMPARE(palette.colors()["second"], QColor(Qt::blue));
        QCOMPARE(palette.samplingFps(), 20);
        palette.configure({ first, second }, { "first", "second" }, { true, true, true });
        QCOMPARE(palette.samplingFps(), 0);
        QCOMPARE(palette.ledFps(), 0);
        palette.reset();
        palette.sample(Qt::green);
        palette.advance(100);
        QCOMPARE(palette.colors()["first"], QColor(Qt::red));
        QCOMPARE(palette.colors()["second"], QColor(Qt::blue));
        palette.configure({ first, second }, { "first", "second" }, { true, false, false });
        palette.sample(Qt::green);
        palette.advance(100);
        QCOMPARE(palette.colors()["first"], QColor(Qt::green));
        QCOMPARE(palette.colors()["second"], QColor(Qt::green));
        QString error;
        const auto json = effectPresetJson(first);
        const auto parsed = parseEffectPreset(QJsonDocument(json).toJson(), &error);
        QVERIFY2(parsed.has_value(), qPrintable(error));
        QCOMPARE(*parsed, first);
        auto bad = json;
        bad["power"] = QJsonObject { { "pauseOnBattery", "yes" } };
        QVERIFY(!parseEffectPreset(QJsonDocument(bad).toJson(), &error));
        // Cold start uses the existing minimum brightness, never all black.
        EffectPalette cold;
        cold.configure({ first }, { "first" }, { true, true, false });
        QVERIFY(cold.colors()["first"].value() > 0);
        QCOMPARE(cold.samplingFps(), 0);
    }
    void recoveredCaptureAcceptsFirstSampleWithoutResettingColors()
    {
        EffectPreset slow, fast;
        slow.id = "slow";
        fast.id = "fast";
        slow.ambient = { 1, 60, 0, 0, 100, 0 };
        fast.ambient = { 20, 60, 0, 0, 100, 0 };
        EffectPalette palette;
        palette.configure({ slow, fast }, { "slow", "fast" });
        palette.sample(Qt::red);
        palette.advance(20);
        QCOMPARE(palette.colors()["slow"], QColor(Qt::red));
        QCOMPARE(palette.colors()["fast"], QColor(Qt::red));
        palette.resumeSampling();
        QCOMPARE(palette.colors()["slow"], QColor(Qt::red));
        QCOMPARE(palette.colors()["fast"], QColor(Qt::red));
        palette.sample(Qt::blue);
        palette.advance(20);
        QCOMPARE(palette.colors()["slow"], QColor(Qt::blue));
        QCOMPARE(palette.colors()["fast"], QColor(Qt::blue));
        palette.sample(Qt::green); // Normal per-effect rate limits still apply after recovery.
        palette.advance(20);
        QCOMPARE(palette.colors()["slow"], QColor(Qt::blue));
        QCOMPARE(palette.colors()["fast"], QColor(Qt::blue));
    }
    void ambientPresetsHaveIndependentCadencesAndDarkFloor()
    {
        EffectPreset slow, fast;
        slow.id = "slow";
        slow.ambient = { 1, 10, 0, 0, 100, 0 };
        fast.id = "fast";
        fast.ambient = { 20, 60, 0, 0, 100, 0 };
        EffectPalette palette;
        palette.configure({ slow, fast }, { "slow", "fast" });
        QCOMPARE(palette.samplingFps(), 20);
        QCOMPARE(palette.ledFps(), 60);
        palette.sample(Qt::red);
        palette.advance(20);
        QCOMPARE(palette.colors()["slow"], QColor(Qt::red));
        palette.advance(40);
        palette.sample(Qt::blue);
        palette.advance(20);
        QCOMPARE(palette.colors()["slow"], QColor(Qt::red));
        QCOMPARE(palette.colors()["fast"], QColor(Qt::blue));
        palette.configure({ slow, fast }, { "fast" }); // Keep the live color when only the selection changes.
        QCOMPARE(palette.colors()["fast"], QColor(Qt::blue));
        fast.ambient.minimum = 25;
        fast.ambient.maximum = 70;
        fast.ambient.temperature = -60;
        palette.configure({ fast }, { "fast" });
        palette.sample(Qt::black);
        palette.advance(20);
        QCOMPARE(palette.colors()["fast"].value(), 64);
        palette.advance(60);
        palette.sample(Qt::blue);
        palette.advance(20);
        QCOMPARE(palette.colors()["fast"], QColor(0, 0, 179)); // White balance preserves pure blue.
        palette.configure({ fast }, { });
        QCOMPARE(palette.samplingFps(), 0);
    }
    void oldCapsOptionIsIgnored()
    {
        QString error;
        LightingPreset preset;
        for (bool previousOption : { false, true }) {
            auto json = presetJson(preset);
            auto keyboard = json["keyboard"].toObject();
            QVERIFY(!keyboard.contains("capsLockAsShift"));
            keyboard["capsLockAsShift"] = previousOption;
            json["keyboard"] = keyboard;
            const auto parsed = parsePreset(QJsonDocument(json).toJson(), &error);
            QVERIFY2(parsed.has_value(), qPrintable(error));
            QCOMPARE(*parsed, preset);
            ModifierState caps;
            caps.capsLock = true;
            QVERIFY(layerActive(parsed->layers.first(), caps));
        }
    }
    void exactApplicationMatchingAndFallback()
    {
        LightingPreset base, editor;
        editor.id = "editor";
        PresetCollection c { { base, editor }, base.id,
            { { "Disabled", "org.kde.kate", base.id, false }, { "Editor", "org.kde.kate", editor.id, true },
                { "Lower priority", "org.kde.kate", base.id, true } } };
        QCOMPARE(matchingPreset(c, "org.kde.kate"), 1);
        QCOMPARE(matchingPreset(c, "Org.KDE.Kate"), 1);
        QCOMPARE(matchingPreset(c, "org.kde.kate-other"), 0);
        QCOMPARE(matchingPreset(c, "kate org.kde.kate"), 0);
        QCOMPARE(matchingPreset(c, QString()), 0);
        c.rules[1].presetId = "missing";
        QCOMPARE(matchingPreset(c, "org.kde.kate"), 0);
        c.defaultId = editor.id;
        QCOMPARE(matchingPreset(c, "other"), 1);
    }
    void chordsSidesLocksAndPriority()
    {
        KeyboardLayer chord { "Chord", { "ctrl", "shift-right" }, inheritedKeyStyles() };
        ModifierState s;
        QVERIFY(!layerActive(chord, s));
        s.pressed = { KEY_LEFTCTRL, KEY_LEFTSHIFT };
        QVERIFY(!layerActive(chord, s));
        s.pressed = { KEY_LEFTCTRL, KEY_RIGHTSHIFT };
        QVERIFY(layerActive(chord, s));
        s.pressed = { KEY_RIGHTCTRL, KEY_RIGHTSHIFT };
        QVERIFY(layerActive(chord, s));
        s.pressed = { KEY_RIGHTSHIFT };
        s.effective = Qt::ControlModifier;
        QVERIFY(layerActive(chord, s)); // System-latched/locked Ctrl has no physical side.
        QVERIFY(!conditionActive("ctrl-left", s));
        s.pressed = { KEY_RIGHTCTRL };
        s.effective = { };
        QVERIFY(!layerActive(chord, s)); // Releasing Right Shift restores the lower layer.
        s = { };
        s.capsLock = true;
        QVERIFY(!conditionActive("shift", s));
        QVERIFY(layerActive(KeyboardLayer(), s));
        QVERIFY(!layerActive({ "Left Shift", { "shift-left" }, inheritedKeyStyles() }, s));
        KeyboardLayer combined { "Shift + Ctrl", { "shift", "ctrl" }, inheritedKeyStyles() };
        s.pressed = { KEY_LEFTCTRL };
        QVERIFY(!layerActive(combined, s)); // Caps Lock must not substitute in chords.
        QVERIFY(!layerActive(chord, s));
        s.pressed.insert(KEY_LEFTSHIFT);
        QVERIFY(layerActive(combined, s));
        s.pressed.clear();
        QVERIFY(!layerActive({ "Shift + A", { "shift", "key:30" }, inheritedKeyStyles() }, s));
        QVERIFY(conditionActive("caps-lock", s));
        QVERIFY(!conditionActive("shift-left", s));
        s = { };
        s.effective = Qt::ShiftModifier | Qt::MetaModifier;
        QVERIFY(conditionActive("shift", s));
        QVERIFY(conditionActive("super", s));
        s = { };
        s.pressed = { KEY_RIGHTALT, KEY_COMPOSE, KEY_A };
        QVERIFY(conditionActive("alt", s));
        QVERIFY(conditionActive("alt-right", s));
        QVERIFY(!conditionActive("alt-left", s));
        QVERIFY(conditionActive("menu", s));
        QVERIFY(conditionActive(QStringLiteral("key:%1").arg(KEY_A), s));
        QVERIFY(!conditionActive("fn-lock", s)); // Never infer a firmware toggle from a key press.
        KeyboardStyles base;
        base[0] = { KeyMode::Static, Qt::red };
        auto generic = KeyboardLayer();
        generic.styles[1] = { KeyMode::Static, Qt::green };
        chord.styles[0] = { KeyMode::Off, Qt::transparent };
        chord.styles[1] = { KeyMode::Static, Qt::blue };
        s = { };
        s.pressed = { KEY_RIGHTSHIFT, KEY_LEFTCTRL };
        const auto resolved = resolveLayers(base, { generic, chord }, s);
        QCOMPARE(resolved[0].mode, KeyMode::Off);
        QCOMPARE(resolved[1].color, QColor(Qt::blue));
        QCOMPARE(resolved[2], base[2]);
        s.pressed.remove(KEY_LEFTCTRL);
        QCOMPARE(resolveLayers(base, { generic, chord }, s)[0].color, QColor(Qt::red));
        QCOMPARE(resolveLayers(base, { generic, chord }, s)[1].color, QColor(Qt::green));
        s = { };
        QCOMPARE(resolveLayers(base, { generic, chord }, s), base);
    }
    void onlyTheMostSpecificLayerReplacesBase()
    {
        DeviceDescription device { "test", "Test",
            DeviceCapability::PerKeyColor | DeviceCapability::PerStripColor,
            { { 0, "A", { } }, { 1, "B", { } }, { 2, "C", { } } }, { },
            { { FirstStripId, "Strip", { { 10, "1", { } }, { 11, "2", { } } } } } };
        LightingPreset preset;
        preset.keyboardDeviceId = device.id;
        preset.base[0] = { KeyMode::Static, Qt::white };
        preset.base[1] = { KeyMode::Static, Qt::blue };
        preset.base[11] = { KeyMode::Static, Qt::blue };
        preset.effects[1] = { { "base" } };
        preset.effects[11] = { { "base-strip" } };
        KeyboardLayer simple { "Shift", { "shift" }, { } };
        simple.styles[1] = { KeyMode::Static, Qt::red };
        simple.styles[11] = { KeyMode::Static, Qt::red };
        simple.effects[1] = { { "simple" } };
        simple.effects[11] = { { "simple-strip" } };
        KeyboardLayer complex { "Shift + A", { "shift", "key:30" }, { } };
        complex.styles[0] = { KeyMode::Off, Qt::transparent };
        complex.effects[0] = { { "complex" } };
        complex.effects[10] = { { "complex-strip" } };
        ModifierState state;
        state.pressed = { KEY_LEFTSHIFT, KEY_A };
        for (const auto& layers :
            { KeyboardLayers { simple, complex }, KeyboardLayers { complex, simple } }) {
            preset.layers = layers;
            QCOMPARE(layers[activeLayer(layers, state)].conditions, complex.conditions);
            const auto resolved = resolveLayers(preset.base, layers, state);
            QCOMPARE(resolved[0].mode, KeyMode::Off);
            QCOMPARE(resolved[1].color, QColor(Qt::blue));
            const auto styles = deviceKeyStyles(preset, device, state);
            QCOMPARE(styles[0].mode, KeyMode::Off);
            QCOMPARE(styles[1].color, QColor(Qt::blue));
            QCOMPARE(styles[11].color, QColor(Qt::blue));
            const auto effects = deviceEffects(preset, device, state);
            QCOMPARE(effects[0], (EffectList { { "complex" } }));
            QCOMPARE(effects[1], (EffectList { { "base" } }));
            QCOMPARE(effects[10], (EffectList { { "complex-strip" } }));
            QCOMPARE(effects[11], (EffectList { { "base-strip" } }));
        }
        // Equally complex matches choose the last row, still inheriting only Base.
        KeyboardLayer tied { "Right Shift", { "shift-right" }, { } };
        tied.styles[0] = { KeyMode::Static, Qt::green };
        state.pressed = { KEY_RIGHTSHIFT };
        QCOMPARE(activeLayer({ simple, tied }, state), 1);
        QCOMPARE(resolveLayers(preset.base, { simple, tied }, state)[1].color, QColor(Qt::blue));
        QCOMPARE(resolveLayers(preset.base, { tied, simple }, state)[0].color, QColor(Qt::white));
        // Extra modifiers exclude simple layers even if the chord has no assignment for a key.
        KeyboardLayer alt { "Alt", { "alt" }, { } };
        complex.conditions = { "shift", "alt" };
        state.pressed = { KEY_LEFTSHIFT, KEY_LEFTALT };
        QCOMPARE(activeLayer({ complex, simple, alt }, state), 0);
        QCOMPARE(activeLayer({ simple, alt, complex }, state), 2);
        QCOMPARE(resolveLayers(preset.base, { complex, simple, alt }, state)[1].color, QColor(Qt::blue));
    }
    void exactModifiers()
    {
        const KeyboardLayer shift;
        const KeyboardLayer ctrlShift { "Ctrl + Shift", { "ctrl", "shift" }, inheritedKeyStyles() };
        const KeyboardLayer altShift { "Alt + Shift", { "alt", "shift" }, inheritedKeyStyles() };
        ModifierState state;
        state.pressed = { KEY_LEFTSHIFT, KEY_A };
        QVERIFY(layerActive(shift, state)); // Ordinary typing must keep a modifier layer active.
        for (const auto key :
            { KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTMETA, KEY_RIGHTMETA }) {
            state.pressed.insert(key);
            QVERIFY(!layerActive(shift, state));
            state.pressed.remove(key);
        }
        state.pressed = { KEY_LEFTSHIFT, KEY_RIGHTCTRL, KEY_A };
        QVERIFY(layerActive(ctrlShift, state));
        state.pressed.insert(KEY_LEFTALT);
        QVERIFY(!layerActive(ctrlShift, state));
        QVERIFY(!layerActive(altShift, state));
        state.pressed.remove(KEY_RIGHTCTRL);
        QVERIFY(layerActive(altShift, state));

        state = { };
        state.effective = Qt::ShiftModifier | Qt::ControlModifier;
        QVERIFY(!layerActive(shift, state));
        QVERIFY(layerActive(ctrlShift, state));
        state.effective |= Qt::GroupSwitchModifier; // Latched/locked AltGr also counts as extra Alt.
        QVERIFY(!layerActive(ctrlShift, state));
        state.effective = Qt::ShiftModifier;
        QVERIFY(layerActive(shift, state));
        state = { };
        state.capsLock = true;
        QVERIFY(layerActive(shift, state));
        state.effective = Qt::ControlModifier;
        QVERIFY(!layerActive(shift, state)); // Caps Lock does not bypass exact modifier matching.
        QVERIFY(!layerActive(ctrlShift, state)); // Nor does it stand for Shift in a chord.
        state = { };
        const KeyboardLayer letter { "A", { "key:30" }, inheritedKeyStyles() };
        state.pressed = { KEY_A };
        QVERIFY(layerActive(letter, state));
        state.pressed.insert(KEY_LEFTSHIFT);
        QVERIFY(!layerActive(letter, state));
    }
    void copilotDoesNotActivateShift()
    {
        ModifierState state;
        KeyboardLayer shift;
        shift.styles[0] = { KeyMode::Static, Qt::red };
        KeyboardStyles base;
        // Real G615LW sequence: Super, Shift, F23 down; F23, Shift, Super up.
        for (const auto code : { KEY_LEFTMETA, KEY_LEFTSHIFT, KEY_F23 }) {
            state.pressed.insert(code);
            QCOMPARE(resolveLayers(base, { shift }, state), base);
        }
        for (const auto code : { KEY_F23, KEY_LEFTSHIFT, KEY_LEFTMETA }) {
            state.pressed.remove(code);
            QCOMPARE(resolveLayers(base, { shift }, state), base);
        }
        state.pressed = { KEY_LEFTSHIFT };
        QCOMPARE(resolveLayers(base, { shift }, state)[0].color, QColor(Qt::red));
    }
    void inactiveModifierLayersDoNotLeakAssignments()
    {
        KeyboardStyles base;
        KeyboardLayer shift;
        shift.styles[0] = { KeyMode::Static, Qt::red };
        KeyboardLayer chord { "Ctrl + Right Shift", { "ctrl", "shift-right" }, inheritedKeyStyles() };
        chord.styles[1] = { KeyMode::Static, Qt::green };
        ModifierState state;
        state.pressed = { KEY_LEFTCTRL, KEY_RIGHTSHIFT };
        auto resolved = resolveLayers(base, { shift, chord }, state);
        QCOMPARE(resolved[0], base[0]); // Inherit no longer includes the non-matching Shift layer.
        QCOMPARE(resolved[1].color, QColor(Qt::green));
        auto higher = chord;
        higher.styles[1] = { KeyMode::Static, Qt::blue };
        QCOMPARE(resolveLayers(base, { shift, chord, higher }, state)[1].color, QColor(Qt::blue));
    }
    void invalidFilesAndFailedSavePreservePriorCollection()
    {
        QString error;
        LightingPreset p;
        QVERIFY(!parsePreset("{}", &error));
        auto json = presetJson(p);
        json["version"] = 999;
        QVERIFY(!parsePreset(QJsonDocument(json).toJson(), &error));
        p.id = "../../escape";
        QVERIFY(!parsePreset(QJsonDocument(presetJson(p)).toJson(), &error));
        p = { };
        p.layers.first().conditions = { "not-a-condition" };
        QVERIFY(!parsePreset(QJsonDocument(presetJson(p)).toJson(), &error));
        p = { };
        PresetCollection valid { { p }, p.id, { } };
        QVERIFY(savePresets(valid, &error));
        auto bad = valid;
        bad.rules.append({ "Broken", "", p.id, true });
        QVERIFY(!savePresets(bad, &error));
        QCOMPARE(*loadPresets(defaultCollection(), &error), valid);
        QFile file(presetDirectory() + "/settings-v2.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{broken");
        file.close();
        QVERIFY(!loadPresets(defaultCollection(), &error));
        QVERIFY(!error.isEmpty());
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
    PresetsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_presets.moc"
