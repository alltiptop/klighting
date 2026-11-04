// SPDX-License-Identifier: GPL-3.0-or-later
#include "color.h"
#include "effect_names.h"
#include "effect_palette.h"
#include "fixtures/keyboard.h"
#include "keyboard_profile.h"
#include "presets.h"
#include <QtTest>
using namespace Ambient;
static QByteArray colorSample(std::initializer_list<std::pair<QColor, int>> patches)
{
    QByteArray sample;
    for (const auto& [color, count] : patches) {
        const char rgba[] = { char(color.red()), char(color.green()), char(color.blue()), char(255) };
        sample += QByteArray(rgba, 4).repeated(count);
    }
    return sample;
}

class AmbientTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void keyAssignmentsRoundTripAndUseFinalEffectColors()
    {
        KeyboardStyles styles;
        const LightingPreset preset = defaultCollection("test-keyboard").presets.first();
        const QMap<QString, QColor> palette { { DefaultEffectId, QColor(10, 20, 70) } };
        QVERIFY(lightingColors(preset, styles, TestFixture::keyboardDescription(), palette).keys.isEmpty());
        styles[0] = { KeyMode::Static, Qt::red };
        styles[4] = { KeyMode::Off, Qt::transparent };
        styles[36] = { KeyMode::Static, QColor(120, 100, 80) };
        QCOMPARE(parseKeyStyles(serializeKeyStyles(styles)), styles);
        const auto colors = lightingColors(preset, styles, TestFixture::keyboardDescription(), palette).keys;
        QCOMPARE(colors.size(), TestFixture::KeyCount);
        QCOMPARE(colors[0], QColor(Qt::red)); // An explicit color is independent of effect settings.
        QCOMPARE(colors[1], QColor(10, 20, 70)); // Ambient is already calibrated.
        QCOMPARE(colors[4], QColor(Qt::black)); // Explicit Off bypasses the floor.
        QCOMPARE(colors[36], QColor(120, 100, 80));
        QCOMPARE(parseKeyStyles({ "-1:static:#ff0000", "65600:off:", "x:off:", "5:static:invalid",
                     "6:unknown:#ff0000" }),
            KeyboardStyles());
        const KeyGroups groups { { "Gaming", { 0, 4, 36, 50, 51, 52 } } };
        QCOMPARE(parseKeyGroups(serializeKeyGroups(groups)), groups);
        QCOMPARE(parseKeyGroups(QStringLiteral(
                     "{\"Valid\":[4,2,2,-1,65600,1.5,\"3\",true],\"Empty\":[],\"Bad\":false}")),
            (KeyGroups { { "Valid", { 2, 4 } } }));
    }
    void orderedSolidLocalAndDefaultEffects()
    {
        PresetCollection collection = defaultCollection("test-keyboard");
        auto& p = collection.presets[0];
        EffectPreset solid;
        solid.id = "local-red";
        solid.type = "solid";
        solid.solidColor = Qt::red;
        p.localEffects = { solid };
        p.effects[DefaultAssignment] = { { DefaultEffectId }, { solid.id, 50 } };
        p.base[1] = { KeyMode::Off };
        const auto d = TestFixture::keyboardDescription();
        auto styles = deviceKeyStyles(p, d, { });
        auto assignments = deviceEffects(p, d, { });
        EffectPalette palette;
        palette.configure(runtimeEffects(collection), activeEffects(p, styles, d, assignments));
        auto colors = palette.colors();
        colors[DefaultEffectId] = Qt::blue;
        auto frame = lightingColors(p, styles, d, colors, assignments);
        QCOMPARE(frame.keys[0], QColor(128, 0, 127));
        QCOMPARE(frame.keys[1], QColor(Qt::black));
        p.effects[DefaultAssignment].move(1, 0);
        assignments = deviceEffects(p, d, { });
        frame = lightingColors(p, deviceKeyStyles(p, d, { }), d, colors, assignments);
        QCOMPARE(frame.keys[0], QColor(Qt::blue));
        // A solid-only default needs no screen captures, and survives palette reset.
        p.effects[DefaultAssignment] = { { solid.id } };
        assignments = deviceEffects(p, d, { });
        palette.configure(runtimeEffects(collection), activeEffects(p, styles, d, assignments));
        QCOMPARE(palette.samplingFps(), 0);
        palette.reset();
        QCOMPARE(palette.colors()[scopedEffectId(p, solid.id)], QColor(Qt::red));
        // Duplicated keyboard presets may edit their local effects independently.
        auto duplicate = p;
        duplicate.id = "copy";
        duplicate.localEffects[0].solidColor = Qt::green;
        collection.presets.append(duplicate);
        const auto definitions = runtimeEffects(collection);
        palette.configure(definitions, { "default:local-red", "copy:local-red" });
        QCOMPARE(palette.colors()["default:local-red"], QColor(Qt::red));
        QCOMPARE(palette.colors()["copy:local-red"], QColor(Qt::green));
        // The same default list also drives devices without per-key control.
        auto uniform = d;
        uniform.capabilities = DeviceCapability::Color;
        const auto& original = collection.presets.first();
        const auto resolved = deviceEffects(original, uniform, { });
        frame = lightingColors(
            original, deviceKeyStyles(original, uniform, { }), uniform, palette.colors(), resolved);
        QCOMPARE(frame.uniform, QColor(Qt::red));
    }
    void shiftLayerInheritsAndRestoresBase()
    {
        KeyboardStyles base;
        base[0] = { KeyMode::Static, Qt::red };
        base[1] = { KeyMode::Static, Qt::green };
        base[2] = { KeyMode::Off, Qt::transparent };
        auto shift = inheritedKeyStyles();
        QVERIFY(serializeKeyStyles(shift, true).isEmpty());
        QCOMPARE(parseKeyStyles({ }, true), shift); // Existing configurations have no Shift overrides.
        QCOMPARE(effectiveKeyStyles(base, shift, true), base);
        shift[1] = { KeyMode::Ambient, Qt::transparent }; // Explicit Ambient overrides Base's static color.
        shift[2] = { KeyMode::Static, Qt::blue };
        shift[3] = { KeyMode::Off, Qt::transparent };
        QCOMPARE(serializeKeyStyles(shift, true),
            (QStringList { "1:ambient:#00000000:", "2:static:#ff0000ff", "3:off:" }));
        QCOMPARE(parseKeyStyles(serializeKeyStyles(shift, true), true), shift);
        QCOMPARE(
            parseKeyStyles({ "65600:off:", "0:static:invalid", "1:unknown:" }, true), inheritedKeyStyles());
        auto expected = base;
        expected[1] = { KeyMode::Ambient, Qt::transparent };
        expected[2] = { KeyMode::Static, Qt::blue };
        expected[3] = { KeyMode::Off, Qt::transparent };
        QCOMPARE(effectiveKeyStyles(base, shift, true), expected);
        QCOMPARE(effectiveKeyStyles(base, shift, false), base);
        base[0].color = Qt::yellow;
        QCOMPARE(effectiveKeyStyles(base, shift, true)[0].color, QColor(Qt::yellow));
    }
    void duplicateEffectNames()
    {
        QVERIFY(!hasConflictingAmbientEffects({ "blur", "ambientled" }));
        QVERIFY(!hasConflictingAmbientEffects({ "ambientled_dev5", "ambientled_other", "ambientled_dev" }));
        QVERIFY(hasConflictingAmbientEffects({ "ambientled", "ambientled_dev4" }));
        QVERIFY(hasConflictingAmbientEffects({ "ambientled_dev5", "ambientled_dev6" }));
        QVERIFY(!isAmbientEffectName("ambientled_dev4_other"));
    }
    void screenColors()
    {
        QCOMPARE(averageColor(QByteArray::fromHex("ff0000ffff0000ff")), QColor(Qt::red));
        QCOMPARE(averageColor(QByteArray::fromHex("000000ff000000ff")), QColor(Qt::black));
        auto mixed = averageColor(QByteArray::fromHex("000000ffffffffff"));
        // A 50% linear-light mixture of white and black is sRGB 188, not 128.
        QVERIFY(qAbs(mixed.red() - 188) <= 1);
        QCOMPARE(mixed.red(), mixed.green());
        QCOMPARE(averageColor(QByteArray::fromHex("ffffffff"), 0.0), QColor(Qt::black));
        const auto gray = averageColor(QByteArray::fromHex("ff0000ff"), 1.0, 0.0);
        QCOMPARE(gray.red(), gray.green());
        QCOMPARE(gray.green(), gray.blue());
        QVERIFY(!averageColor({ }).isValid());
        QVERIFY(!averageColor(QByteArray("abc")).isValid());
    }
    void dominantColorKeepsTheLargestHue()
    {
        DominantColorPicker picker;
        QCOMPARE(picker.select(colorSample({ { Qt::red, 60 }, { Qt::blue, 40 } })), QColor(Qt::red));
        QCOMPARE(picker.select(colorSample({ { Qt::green, 100 } })), QColor(Qt::green));
        const auto shades = colorSample({ { QColor::fromHsv(0, 100, 200), 20 },
            { QColor::fromHsv(0, 150, 200), 20 }, { QColor::fromHsv(0, 250, 200), 20 }, { Qt::blue, 40 } });
        const auto selected = picker.select(shades);
        QVERIFY(selected.red() > selected.green());
        QCOMPARE(selected.green(), selected.blue());
    }
    void dominantColorIgnoresTinyAccentsAndHandlesNeutralScreens()
    {
        DominantColorPicker picker;
        const auto tiny = picker.select(colorSample({ { Qt::blue, 4 }, { QColor(80, 80, 80), 96 } }));
        QCOMPARE(tiny.red(), tiny.green());
        QCOMPARE(tiny.green(), tiny.blue());
        QCOMPARE(picker.select(colorSample({ { Qt::blue, 5 }, { Qt::black, 95 } })), QColor(Qt::blue));
        QCOMPARE(picker.select(colorSample({ { Qt::black, 100 } })), QColor(Qt::black));
        QCOMPARE(picker.select(colorSample({ { Qt::white, 100 } })), QColor(Qt::white));
        QCOMPARE(picker.select(colorSample({ { QColor(80, 80, 80), 100 } })), QColor(80, 80, 80));
        const auto dark = picker.select(colorSample({ { QColor(15, 0, 0), 100 } }));
        QCOMPARE(dark.red(), dark.green());
        QCOMPARE(dark.green(), dark.blue());
    }
    void dominantColorMergesHueWrapAndResistsNearTies()
    {
        DominantColorPicker picker;
        const auto red = picker.select(colorSample({ { QColor::fromHsv(359, 255, 255), 30 },
            { QColor::fromHsv(1, 255, 255), 30 }, { Qt::blue, 40 } }));
        QCOMPARE(red.red(), 255);
        QVERIFY(red.green() <= 5 && red.blue() <= 5);
        QCOMPARE(picker.select(colorSample({ { Qt::red, 51 }, { Qt::blue, 49 } })), QColor(Qt::red));
        const auto nearTie = colorSample({ { Qt::red, 49 }, { Qt::blue, 51 } });
        QCOMPARE(picker.select(nearTie), QColor(Qt::red));
        picker.reset();
        QCOMPARE(picker.select(nearTie), QColor(Qt::blue));
        QCOMPARE(picker.select(colorSample({ { Qt::red, 70 }, { Qt::blue, 30 } })), QColor(Qt::red));
    }
    void dominantColorPreservesColorAdjustments()
    {
        DominantColorPicker picker;
        const auto sample = colorSample({ { Qt::red, 100 } });
        QCOMPARE(picker.select(sample, 0.0), QColor(Qt::black));
        const auto gray = picker.select(sample, 1.0, 0.0);
        QCOMPARE(gray.red(), gray.green());
        QCOMPARE(gray.green(), gray.blue());
        QVERIFY(!picker.select({ }).isValid());
        QVERIFY(!picker.select(QByteArray("abc")).isValid());
    }
    void colorTemperaturePreservesPrimaryColorsAndBrightness()
    {
        for (int temperature = -100; temperature <= 100; ++temperature) {
            for (int value : { 1, 64, 127, 255 }) {
                for (const auto& primary : { QColor(value, 0, 0), QColor(0, value, 0), QColor(0, 0, value) })
                    QCOMPARE(adjustColorTemperature(primary, temperature), primary);
            }
            for (const auto& sample : { QColor(Qt::black), QColor(Qt::white), QColor(210, 180, 120),
                     QColor(10, 40, 120), QColor(90, 35, 70), QColor(64, 64, 64) }) {
                const auto corrected = adjustColorTemperature(sample, temperature);
                QCOMPARE(std::max({ corrected.red(), corrected.green(), corrected.blue() }),
                    std::max({ sample.red(), sample.green(), sample.blue() }));
            }
        }
        QVERIFY(!adjustColorTemperature(QColor(), -50).isValid());
        QCOMPARE(adjustColorTemperature(Qt::black, -100), QColor(Qt::black));
        const QColor sand(210, 180, 120);
        QCOMPARE(adjustColorTemperature(sand, 0), sand);
        QCOMPARE(adjustColorTemperature(sand, -999), adjustColorTemperature(sand, -100));
        QCOMPARE(adjustColorTemperature(sand, 999), adjustColorTemperature(sand, 100));
    }
    void colorTemperatureBalancesMixedColorsAndNightLight()
    {
        const QColor sand(210, 180, 120);
        const auto warm = adjustColorTemperature(sand, -50);
        const auto warmer = adjustColorTemperature(sand, -100);
        QCOMPARE(warm.red(), sand.red());
        QVERIFY(warm.green() < sand.green());
        QVERIFY(warm.blue() < sand.blue());
        QVERIFY(warmer.blue() < warm.blue());
        // Temperature applies after the black-screen floor, so the night light
        // has the requested white balance without dropping below the minimum.
        const auto floor = mapBrightness(Qt::black, 25, 50);
        const auto warmFloor = adjustColorTemperature(floor, -50);
        const auto coolFloor = adjustColorTemperature(floor, 50);
        QCOMPARE(warmFloor.red(), 64);
        QVERIFY(warmFloor.red() > warmFloor.green());
        QVERIFY(warmFloor.green() > warmFloor.blue());
        QCOMPARE(coolFloor.blue(), 64);
        QVERIFY(coolFloor.blue() > coolFloor.green());
        QVERIFY(coolFloor.green() > coolFloor.red());
    }
    void brightnessMappingDimsMidtonesAndSharesRgbAcrossZones()
    {
        QCOMPARE(mapBrightness(Qt::black, 20, 60), QColor(51, 51, 51));
        QCOMPARE(mapBrightness(Qt::white, 20, 60), QColor(153, 153, 153));
        QCOMPARE(mapBrightness(QColor(100, 50, 0), 20, 60), QColor(91, 46, 0));
        QCOMPARE(mapBrightness(QColor(120, 60, 30), 0, 20), QColor(24, 12, 6));
        QCOMPARE(mapBrightness(QColor(120, 60, 30), 0, 100), QColor(120, 60, 30));
        QCOMPARE(mapBrightness(Qt::black, 90, 20), QColor(51, 51, 51));
        QCOMPARE(mapBrightness(Qt::white, 0, 0), QColor(Qt::black));
        QVERIFY(!mapBrightness(QColor(), 20, 60).isValid());
    }
    void brightnessLimits()
    {
        QCOMPARE(constrainBrightness(Qt::black, 50, 50), QColor(128, 128, 128));
        QCOMPARE(constrainBrightness(Qt::black, 20, 60), QColor(51, 51, 51));
        QCOMPARE(constrainBrightness(Qt::white, 20, 60), QColor(153, 153, 153));
        QCOMPARE(constrainBrightness(QColor(10, 5, 0), 20, 60), QColor(51, 26, 0));
        QCOMPARE(constrainBrightness(QColor(250, 100, 50), 20, 60), QColor(153, 61, 31));
        QCOMPARE(constrainBrightness(QColor(100, 50, 10), 20, 60), QColor(100, 50, 10));
        QCOMPARE(constrainBrightness(Qt::black, 0, 100), QColor(Qt::black));
        QCOMPARE(constrainBrightness(Qt::white, 0, 0), QColor(Qt::black));
        QCOMPARE(constrainBrightness(Qt::black, 90, 20), QColor(51, 51, 51));
        QVERIFY(!constrainBrightness(QColor(), 20, 60).isValid());
    }
    void brightnessFloorSurvivesHueTransitions()
    {
        ColorSmoother smoother;
        smoother.setTarget(constrainBrightness(Qt::red, 20, 20));
        smoother.step(17, 400);
        for (const auto& target : { QColor(Qt::blue), QColor(Qt::black), QColor(Qt::white) }) {
            smoother.setTarget(constrainBrightness(target, 20, 20));
            for (int i = 0; i < 100; ++i) {
                const auto output = constrainBrightness(*smoother.step(17, 400), 20, 20);
                QCOMPARE(std::max({ output.red(), output.green(), output.blue() }), 51);
            }
        }
    }
    void regionGeometry()
    {
        QCOMPARE(sampleRegion({ -1920, 80, 1920, 1080 }, true, 25), QRect(-1920, 890, 1920, 270));
        QCOMPARE(sampleRegion({ 100, 200, 500, 3 }, true, 1), QRect(100, 202, 500, 1));
        QCOMPARE(sampleRegion({ 100, 200, 500, 300 }, false, 25), QRect(100, 200, 500, 300));
    }
    void smoothingUsesElapsedTime()
    {
        ColorSmoother one, four;
        QVERIFY(!one.step(50, 200));
        for (auto* s : { &one, &four }) {
            s->setTarget(Qt::red);
            s->step(50, 200);
            s->setTarget(Qt::blue);
        }
        const auto a = one.step(200, 200);
        for (int i = 0; i < 3; ++i)
            four.step(50, 200);
        const auto b = four.step(50, 200);
        QCOMPARE(a, b);
        QVERIFY(qAbs(a->red() - 94) <= 1);
        QVERIFY(qAbs(a->blue() - 161) <= 1);
        one.reset();
        QVERIFY(!one.step(50, 200));
    }
    void smoothsBetweenSparseSnapshots()
    {
        constexpr int interval = 50; // Six intermediate colors between sparse snapshots at 20 FPS.
        ColorSmoother smoother;
        smoother.setTarget(Qt::red);
        QCOMPARE(smoother.step(interval, DefaultSmoothingMs), QColor(Qt::red));
        smoother.setTarget(Qt::blue); // The next snapshot; no more targets for 300 ms.
        QColor previous(Qt::red);
        int intermediateColours = 0;
        for (int elapsed = interval; elapsed <= 300; elapsed += interval) {
            const auto next = smoother.step(interval, DefaultSmoothingMs);
            QVERIFY(next);
            QVERIFY(next->red() < previous.red());
            QVERIFY(next->blue() > previous.blue());
            QVERIFY(next->red() > 0 && next->blue() < 255);
            ++intermediateColours;
            previous = *next;
        }
        QCOMPARE(intermediateColours, 6);
    }
};
QTEST_GUILESS_MAIN(AmbientTest)
#include "test_ambient.moc"
