// SPDX-License-Identifier: GPL-3.0-or-later
#include "service/lighting_runtime.h"
#include "sources/microphone_source.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

using namespace Ambient;
namespace
{
PresetCollection collection()
{
    auto c = defaultCollection("test-strip");
    c.devices = QList<DeviceConfiguration> { { "test-strips", "one", "test-strip", "One" } };
    auto& p = c.presets[0];
    p.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::blue) } };
    p.layers = { KeyboardLayer(), KeyboardLayer { "Ctrl+A", { "ctrl", "key:30" } } };
    p.layers[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::red) } };
    p.layers[1].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::green) } };
    return c;
}
QVariantList colors(const QString& id = "one")
{
    return qApp->property(("test." + id + ".colors").toUtf8()).toList();
}
QColor color(const QString& id = "one")
{
    const auto elements = colors(id);
    return QColor(elements.isEmpty() ? qApp->property(("test." + id + ".uniform").toUtf8()).toString()
                                     : elements.first().toString());
}
quint64 counter(const LightingRuntime& runtime, const char* name)
{
    return runtime.status().value(QLatin1String(name)).toULongLong();
}
void start(LightingRuntime& runtime, const PresetCollection& c)
{
    runtime.configure(c);
    runtime.setEnabled(true);
    QCoreApplication::processEvents();
}
}
class ProfileResolutionTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        for (const auto& name : qApp->dynamicPropertyNames())
            if (name.startsWith("test."))
                qApp->setProperty(name, QVariant());
    }
    void emptyBaseInheritsDefaultButNeverItsLayers()
    {
        auto c = collection();
        auto& base = c.presets[0];
        EffectPreset local;
        local.id = "same-id";
        local.type = "solid";
        local.solidColor = Qt::yellow;
        base.localEffects = { local };
        base.effects[7] = { { local.id } };
        auto empty = base;
        empty.id = "empty";
        empty.effects = { { DefaultAssignment, { } } };
        empty.base.clear();
        empty.localEffects[0].solidColor = Qt::cyan;
        empty.layers.clear();
        KeyboardLayer own;
        own.conditions = { "ctrl" };
        own.effects[7] = { { local.id } };
        empty.layers.append(own);
        c.presets.append(empty);
        c.rules = { { "Empty", "empty", "empty" } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setDesktop("empty", { });
        QCOMPARE(colors(), (QVariantList { "#ffff00", "#0000ff", "#0000ff" }));
        runtime.setDesktop("empty", { { }, Qt::ShiftModifier });
        QCOMPARE(colors(), (QVariantList { "#ffff00", "#0000ff", "#0000ff" }));
        runtime.setDesktop("empty", { { }, Qt::ControlModifier });
        QCOMPARE(colors(), (QVariantList { "#00ffff", "#0000ff", "#0000ff" }));
        // A local assignment replaces only that element, not the rest of Default Base.
        c.presets[1].effects[7] = { { InlineColorId, 100, QColor(Qt::green) } };
        runtime.configure(c);
        runtime.setDesktop("empty", { });
        QCOMPARE(colors(), (QVariantList { "#00ff00", "#0000ff", "#0000ff" }));
        c.presets[1].effects[42] = { }; // An explicitly cleared element still stays off.
        runtime.configure(c);
        QCOMPARE(colors(), (QVariantList { "#00ff00", "#000000", "#0000ff" }));
        runtime.setDesktop("empty", { { }, Qt::ControlModifier });
        QCOMPARE(colors(), (QVariantList { "#00ffff", "#000000", "#0000ff" }));
        c.presets[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::white) } };
        runtime.configure(c);
        QCOMPARE(colors(), (QVariantList { "#00ffff", "#000000", "#ffffff" }));
    }
    void partialBaseOverridesDefaultGradientAndStripAssignments()
    {
        auto c = collection();
        c.presets[0].gradients = { { FirstStripId, 7, 1, Qt::red, Qt::blue } };
        LightingPreset app;
        app.id = "spectacle";
        app.keyboardDeviceId = "test-strip";
        app.defaultEffectId.clear();
        EffectPreset green;
        green.id = "green";
        green.type = "solid";
        green.solidColor = Qt::green;
        app.localEffects = { green };
        app.effects = { { DefaultAssignment, { } }, { 42, { { green.id } } } };
        app.layers.clear();
        c.presets.append(app);
        c.rules = { { "Spectacle", "spectacle", app.id } };
        const auto saved = c;
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setDesktop("spectacle", { });
        QCOMPARE(colors(), (QVariantList { "#ff0000", "#00ff00", "#0000ff" }));
        runtime.setDesktop("spectacle", { { }, Qt::ShiftModifier });
        QCOMPARE(colors(), (QVariantList { "#ff0000", "#00ff00", "#0000ff" }));
        QCOMPARE(c, saved); // Resolution must not copy inherited assignments into the saved preset.
        c.presets[0].gradients.clear();
        c.presets[0].effects[FirstStripId] = { { InlineColorId, 100, QColor(Qt::yellow) } };
        runtime.configure(c);
        QCOMPARE(colors(), (QVariantList { "#ffff00", "#00ff00", "#ffff00" }));
        c.presets[1].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::cyan) } };
        runtime.configure(c);
        QCOMPARE(colors(), (QVariantList { "#00ffff", "#00ff00", "#00ffff" }));
        c.presets[1].effects[FirstStripId] = { }; // Explicit strip off overrides the default assignment.
        runtime.configure(c);
        QCOMPARE(colors(), (QVariantList { "#000000", "#00ff00", "#000000" }));
    }
    void inheritedBaseKeepsOwnAboveLayerEffectsAndUniformAssignments()
    {
        auto c = collection();
        EffectPreset indicator;
        indicator.id = "indicator";
        indicator.type = "solid";
        indicator.solidColor = Qt::green;
        c.effects.append(indicator);
        auto app = c.presets[0];
        app.id = "app";
        app.effects = { { DefaultAssignment, { } } };
        app.layers.clear();
        app.effectOverrides[42] = { { indicator.id } };
        c.presets.append(app);
        c.rules = { { "App", "app", app.id } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setDesktop("app", { { }, Qt::ShiftModifier });
        QCOMPARE(colors(), (QVariantList { "#0000ff", "#00ff00", "#0000ff" }));
        c.presets[1].effectOverrides.clear();
        qApp->setProperty("test.one.globalColor", true);
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::blue));
        c.presets[1].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::cyan) } };
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::cyan));
    }
    void defaultOverridesBelongOnlyToInheritedElements()
    {
        auto c = collection();
        EffectPreset green;
        green.id = "green";
        green.type = "solid";
        green.solidColor = Qt::green;
        c.effects.append(green);
        c.presets[0].effectOverrides[42] = { { green.id } };
        auto app = c.presets[0];
        app.id = "app";
        app.effects = { { DefaultAssignment, { } }, { 7, { { InlineColorId, 100, QColor(Qt::cyan) } } } };
        app.effectOverrides.clear();
        app.layers = { KeyboardLayer() };
        app.layers[0].effects[42] = { { InlineColorId, 100, QColor(Qt::magenta) } };
        c.presets.append(app);
        c.rules = { { "App", "app", app.id } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setDesktop("app", { });
        QCOMPARE(colors(), (QVariantList { "#00ffff", "#00ff00", "#0000ff" }));
        runtime.setDesktop("app", { { }, Qt::ShiftModifier });
        QCOMPARE(colors(), (QVariantList { "#00ffff", "#ff00ff", "#0000ff" }));
        c.presets[1].effects[42] = { };
        runtime.configure(c);
        runtime.setDesktop("app", { });
        QCOMPARE(colors(), (QVariantList { "#00ffff", "#000000", "#0000ff" }));
    }
    void microphoneAssignmentsComposeAndRestoreCurrentLayer()
    {
        auto c = collection();
        EffectPreset microphone;
        microphone.id = "microphone";
        microphone.type = "microphone";
        EffectPreset local = microphone;
        local.id = "local-microphone";
        local.microphone.color = Qt::green;
        c.effects.append(microphone);
        auto& profile = c.presets[0];
        profile.localEffects = { local };
        profile.effects[7] = { { InlineColorId, 100, QColor(Qt::blue) }, { microphone.id, 50 } };
        profile.effects[42] = { { microphone.id }, { InlineColorId, 100, QColor(Qt::white) } };
        profile.effects[1] = { { local.id } };
        profile.effectOverrides[42] = { { local.id } };
        profile.layers[0].effects[42].clear();
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setMicrophoneState({ true, true, false });
        QCOMPARE(colors(), (QVariantList { "#80007f", "#00ff00", "#00ff00" }));
        const auto resolutions = counter(runtime, "profileResolutions");
        runtime.setMicrophoneState({ true, true, true });
        QCOMPARE(colors(), (QVariantList { "#0000ff", "#ffffff", "#000000" }));
        QCOMPARE(counter(runtime, "profileResolutions"), resolutions);
        runtime.setMicrophoneState({ true, true, false });
        runtime.setDesktop({ }, { { }, Qt::ShiftModifier });
        QCOMPARE(colors(), (QVariantList { "#ff0000", "#00ff00", "#ff0000" }));
        runtime.setMicrophoneState({ true, true, true });
        QCOMPARE(colors(), (QVariantList { "#ff0000", "#000000", "#ff0000" }));
        // Above-layer assignments must not affect another device profile.
        auto other = profile;
        other.id = "other";
        other.effectOverrides.clear();
        other.effects.clear();
        other.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::cyan) } };
        other.layers.clear();
        c.presets.append(other);
        c.rules = { { "Other", "other", "other", true } };
        runtime.configure(c);
        runtime.setMicrophoneState({ true, true, false });
        runtime.setDesktop("other", { });
        QVERIFY(!runtime.microphoneEnabled());
        QCOMPARE(color(), QColor(Qt::cyan));
        runtime.setDesktop({ }, { });
        QVERIFY(runtime.microphoneEnabled());
        QCOMPARE(colors(), (QVariantList { "#0000ff", "#ffffff", "#000000" }));
        runtime.setMicrophoneState({ true, true, false });
        QCOMPARE(colors(), (QVariantList { "#80007f", "#00ff00", "#00ff00" }));
    }
    void microphoneSharedAndLocalEditsRecomposeWithoutNewInput()
    {
        auto c = collection();
        EffectPreset microphone;
        microphone.id = "microphone";
        microphone.type = "microphone";
        c.effects.append(microphone);
        c.presets[0].effects[DefaultAssignment] = { { microphone.id } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setMicrophoneState({ true, true, false });
        QCOMPARE(color(), QColor(Qt::red));
        c.effects.last().microphone.color = Qt::cyan;
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::cyan));
        auto local = c.effects.last();
        local.id = "local";
        local.microphone.color = Qt::yellow;
        c.presets[0].localEffects = { local };
        c.presets[0].effects[DefaultAssignment] = { { local.id } };
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::yellow));
        c.presets[0].localEffects[0].microphone.color = Qt::magenta;
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::magenta));
        runtime.setMicrophoneState({ true, true, true });
        QCOMPARE(color(), QColor(Qt::black));
    }
    void microphoneRespectsOrdinaryOrderingAndBrightnessFilters()
    {
        auto c = collection();
        EffectPreset microphone;
        microphone.id = "microphone";
        microphone.type = "microphone";
        EffectPreset brightness;
        brightness.id = "brightness";
        brightness.type = "brightness";
        brightness.brightness = 50;
        c.effects.append(microphone);
        c.effects.append(brightness);
        c.presets[0].effects[7] = { { microphone.id }, { brightness.id } };
        c.presets[0].effects[42] = { { brightness.id }, { microphone.id } };
        c.presets[0].effects[1] = { { microphone.id }, { InlineColorId, 100, QColor(Qt::white) } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setMicrophoneState({ true, true, false });
        QCOMPARE(colors(), (QVariantList { "#800000", "#ff0000", "#ffffff" }));
        runtime.setMicrophoneState({ true, true, true });
        QCOMPARE(colors(), (QVariantList { "#808080", "#808080", "#ffffff" }));
    }
    void microphoneAndNotificationsFollowAssignmentOrder()
    {
        auto c = collection();
        EffectPreset microphone;
        microphone.id = "microphone";
        microphone.type = "microphone";
        EffectPreset notification;
        notification.id = "notification";
        notification.type = "notifications";
        notification.notification.pattern = "blink";
        notification.notification.color = Qt::green;
        notification.notification.period = 200;
        notification.notification.cycles = 1;
        c.effects.append(microphone);
        c.effects.append(notification);
        c.presets[0].effectOverrides[7] = { { microphone.id }, { notification.id } };
        c.presets[0].effectOverrides[42] = { { notification.id }, { microphone.id } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setMicrophoneState({ true, true, false });
        QCOMPARE(colors(), (QVariantList { "#ff0000", "#ff0000", "#0000ff" }));
        runtime.notify({ });
        QCOMPARE(colors(), (QVariantList { "#00ff00", "#ff0000", "#0000ff" }));
        runtime.setMicrophoneState({ true, true, true });
        QCOMPARE(colors(), (QVariantList { "#00ff00", "#00ff00", "#0000ff" }));
        QTRY_COMPARE_WITH_TIMEOUT(colors(), (QVariantList { "#0000ff", "#0000ff", "#0000ff" }), 1500);
        QTest::qWait(150); // Finish the transparent half of the final blink cycle.
        const auto wakeups = counter(runtime, "animationWakeups");
        QTest::qWait(120);
        QCOMPARE(counter(runtime, "animationWakeups"), wakeups);
    }
    void unchangedLayerRetainsObservedKeys()
    {
        auto c = collection();
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        const auto resolutions = counter(runtime, "profileResolutions");
        const auto updates = counter(runtime, "effectUpdates");
        const auto frames = counter(runtime, "compositions");
        runtime.setDesktop("browser", { { 30 }, Qt::NoModifier });
        runtime.setDesktop("notes", { });
        runtime.setDesktop("notes", { { 30 }, Qt::NoModifier });
        QCOMPARE(color(), QColor(Qt::blue));
        QCOMPARE(runtime.status().value("windowClass").toString(), QString("notes"));
        QCOMPARE(counter(runtime, "profileResolutions"), resolutions);
        QCOMPARE(counter(runtime, "effectUpdates"), updates);
        QCOMPARE(counter(runtime, "compositions"), frames);
        // Apply uses the last observed key even though it did not previously select a layer.
        c.presets[0].layers[1].conditions = { "key:30" };
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::green));
        runtime.setDesktop("notes", { });
        QCOMPARE(color(), QColor(Qt::blue));
    }
    void effectiveLayerChanges()
    {
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, collection());
        const QList<ModifierState> states { { { }, Qt::ShiftModifier }, { }, { { 30 }, Qt::ControlModifier },
            { }, { { }, Qt::NoModifier, true }, { } };
        const QList<QColor> expected { Qt::red, Qt::blue, Qt::green, Qt::blue, Qt::red, Qt::blue };
        auto resolutions = counter(runtime, "profileResolutions");
        for (int i = 0; i < states.size(); ++i) {
            runtime.setDesktop({ }, states[i]);
            QCOMPARE(color(), expected[i]);
            QCOMPARE(counter(runtime, "profileResolutions"), ++resolutions);
        }
    }
    void applicationChangeOnlyResolvesAffectedDevice()
    {
        auto c = collection();
        c.devices->append({ "test-strips", "two", "test-strip", "Two" });
        auto& base = c.presets[0];
        DeviceProfile one = base, two = base;
        one.id = "one-profile";
        one.targetDevice = "test-strips:one";
        two.id = "two-profile";
        two.targetDevice = "test-strips:two";
        two.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::white) } };
        two.layers[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::yellow) } };
        base.multiDevice = true;
        base.layerConditions = { { "shift" }, { "ctrl", "key:30" } };
        base.devices = { one, two };
        auto editor = base;
        editor.id = "editor";
        editor.devices = { two }; // Same profile ID, different containing preset.
        editor.devices[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::green) } };
        c.presets.append(editor);
        c.rules = { { "Editor", "editor", "editor", true } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setDesktop({ }, { { }, Qt::ShiftModifier });
        QCOMPARE(color("one"), QColor(Qt::red));
        QCOMPARE(color("two"), QColor(Qt::yellow));
        runtime.setDesktop({ }, { });
        const auto resolutions = counter(runtime, "profileResolutions");
        runtime.setDesktop("editor", { });
        QCOMPARE(color("one"), QColor(Qt::blue));
        QCOMPARE(color("two"), QColor(Qt::green));
        QCOMPARE(counter(runtime, "profileResolutions"), resolutions + 1);
        runtime.setDesktop("browser", { });
        QCOMPARE(color("two"), QColor(Qt::white));
        QCOMPARE(counter(runtime, "profileResolutions"), resolutions + 2);
    }
    void sameIdEditsInvalidateProfilesAndEffects()
    {
        auto c = collection();
        c.effects[0].type = "solid";
        c.effects[0].solidColor = Qt::blue;
        c.presets[0].effects[DefaultAssignment] = { { DefaultEffectId } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        QCOMPARE(color(), QColor(Qt::blue));
        c.effects[0].solidColor = Qt::cyan;
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::cyan));
        auto local = c.effects[0];
        local.id = "local";
        local.solidColor = Qt::yellow;
        c.presets[0].localEffects = { local };
        c.presets[0].effects[DefaultAssignment] = { { local.id } };
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::yellow));
        c.presets[0].localEffects[0].solidColor = Qt::magenta;
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::magenta));
        c.presets[0].effects[DefaultAssignment].clear();
        runtime.configure(c);
        QCOMPARE(color(), QColor(Qt::black));
    }
    void powerChangeResolvesOnceAndRestoresStrip()
    {
        auto c = collection();
        c.devices->first().lightbarOffOnBattery = true;
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.setPowerState({ true, false, false });
        auto resolutions = counter(runtime, "profileResolutions");
        runtime.setPowerState({ true, true, false });
        QCOMPARE(color(), QColor(Qt::black));
        QCOMPARE(counter(runtime, "profileResolutions"), ++resolutions);
        runtime.setPowerState({ true, false, false });
        QCOMPARE(color(), QColor(Qt::blue));
        QCOMPARE(counter(runtime, "profileResolutions"), ++resolutions);
    }
    void monochromeLayerUsesItsBrightnessFilter()
    {
        qApp->setProperty("test.one.brightnessSteps", 3);
        auto c = collection();
        EffectPreset low, high;
        low.id = "low";
        low.type = "brightness";
        low.brightness = 33;
        high = low;
        high.id = "high";
        high.brightness = 67;
        c.effects = { low, high };
        c.presets[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::white) }, { low.id } };
        c.presets[0].layers[0].effects[DefaultAssignment]
            = { { InlineColorId, 100, QColor(Qt::white) }, { high.id } };
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        QCOMPARE(color(), QColor("#555555"));
        runtime.setDesktop({ }, { { }, Qt::ShiftModifier });
        QCOMPARE(color(), QColor("#aaaaaa"));
        runtime.setDesktop({ }, { });
        QCOMPARE(color(), QColor("#555555"));
    }
    void requiredInputsFollowLayersOverridesAndPower()
    {
        auto c = collection();
        c.effects[0].ambient.fps = 3;
        c.presets[0].effects[DefaultAssignment] = { { DefaultEffectId } };
        c.presets[0].layers[0].effects[DefaultAssignment].clear();
        c.devices->first().lightbarOffOnBattery = true;
        EffectPreset notification;
        notification.id = "notify";
        notification.type = "notifications";
        c.effects.append(notification);
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        QCOMPARE(runtime.captureFps(), 3);
        QVERIFY(!runtime.notificationsEnabled());
        c.presets[0].effectOverrides[42] = { { notification.id } };
        runtime.configure(c);
        QCOMPARE(runtime.captureFps(), 3);
        QVERIFY(runtime.notificationsEnabled());
        runtime.setDesktop({ }, { { }, Qt::ShiftModifier });
        QCOMPARE(runtime.captureFps(), 0); // An explicitly empty layer suppresses Base Ambient.
        QVERIFY(runtime.notificationsEnabled()); // Above-layer assignments still apply.
        runtime.setPowerState({ true, true, false });
        QCOMPARE(runtime.captureFps(), 0);
        QVERIFY(!runtime.notificationsEnabled());
        runtime.setPowerState({ true, false, false });
        QCOMPARE(runtime.captureFps(), 0);
        QVERIFY(runtime.notificationsEnabled());
        runtime.setDesktop({ }, { });
        QCOMPARE(runtime.captureFps(), 3);
    }
    void descriptorChangeReplacesBackendAndResolution()
    {
        auto c = collection();
        c.presets[0].effects[7] = { { InlineColorId, 100, QColor(Qt::red) } };
        DeviceManager devices({ }, { MULTI_DRIVER_DIR });
        devices.configure(c);
        devices.setEnabled(true);
        devices.submit({ }, { });
        QCOMPARE(colors(), (QVariantList { "#ff0000", "#0000ff", "#0000ff" }));
        const auto resolutions = devices.profileResolutions();
        qApp->setProperty("test.one.globalColor", true); // Changes description, not options.
        QVERIFY(devices.refresh());
        devices.submit({ }, { });
        QCOMPARE(color(), QColor(Qt::blue));
        QVERIFY(colors().isEmpty()); // A global-only device receives the uniform fallback.
        QCOMPARE(devices.profileResolutions(), resolutions + 1);
        QCOMPARE(qApp->property("test.one.created").toInt(), 2);
        qApp->setProperty("test.one.globalColor", false);
        QVERIFY(devices.refresh());
        devices.submit({ }, { });
        QCOMPARE(colors(), (QVariantList { "#ff0000", "#0000ff", "#0000ff" }));
        qApp->setProperty("test.one.shortStrip", true);
        QVERIFY(devices.refresh());
        devices.submit({ }, { });
        QCOMPARE(colors(), (QVariantList { "#ff0000", "#0000ff" }));
        QCOMPARE(devices.profileResolutions(), resolutions + 3);
        QCOMPARE(qApp->property("test.one.created").toInt(), 4);
    }
    void reconnectPreservesOtherDeviceResolution()
    {
        auto c = collection();
        c.devices->append({ "test-strips", "two", "test-strip", "Two" });
        DeviceManager devices({ }, { MULTI_DRIVER_DIR });
        devices.configure(c);
        devices.setEnabled(true);
        const auto resolutions = devices.profileResolutions();
        qApp->setProperty("test.devices", QStringList { "two" });
        QVERIFY(devices.refresh());
        QCOMPARE(devices.count(), 1);
        QCOMPARE(devices.profileResolutions(), resolutions);
        qApp->setProperty("test.devices", QStringList { "one", "two" });
        QVERIFY(devices.refresh());
        devices.submit({ }, { });
        QCOMPARE(devices.profileResolutions(), resolutions + 1);
        QCOMPARE(color(), QColor(Qt::blue));
        QCOMPARE(qApp->property("test.one.created").toInt(), 2);
        QCOMPARE(qApp->property("test.two.created").toInt(), 1);
    }
};
int main(int argc, char** argv)
{
    QTemporaryDir config;
    if (!config.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", config.path().toUtf8());
    QCoreApplication app(argc, argv);
    ProfileResolutionTest tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_profile_resolution.moc"
