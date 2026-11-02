// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_manager.h"
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
#include <linux/input-event-codes.h>

using namespace Ambient;
class DriversTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void realPluginDescribesHardwareWithoutStartingIt()
    {
        LoadedDriver loaded({ "G615LW" }, { ASUS_DRIVER_DIR });
        QVERIFY2(loaded.driver(), qPrintable(loaded.error()));
        QCOMPARE(loaded.description().id, QStringLiteral("asus-g615lw"));
        QCOMPARE(loaded.description().keys.size(), 88);
        QCOMPARE(loaded.description().bodyColor, QColor("#333333"));
        QCOMPARE(loaded.description().keyColor, QColor("#111111"));
        QVERIFY(loaded.description().capabilities.testFlag(DeviceCapability::PerKeyColor));
        QVERIFY(loaded.description().capabilities.testFlag(DeviceCapability::LightbarPower));
        QCOMPARE(loaded.description().keys[36].id, 36);
        QCOMPARE(loaded.description().keys[36].label, QStringLiteral("W"));
        QCOMPARE(loaded.description().groups.value("WASD"), (QList<int> { 36, 50, 51, 52 }));
        const auto codes = loaded.instance().options.value("InputKeyCodes").toMap();
        QCOMPARE(codes.value("36").toUInt(), quint32(KEY_W));
        QCOMPARE(codes.value("63").toUInt(), quint32(KEY_LEFTSHIFT));
        QCOMPARE(codes.value("74").toUInt(), quint32(KEY_RIGHTSHIFT));
        QCOMPARE(codes.value("77").toUInt(), quint32(KEY_LEFTCTRL));
        QCOMPARE(codes.value("84").toUInt(), quint32(KEY_F23));
        QVERIFY(!codes.contains("78")); // Fn is not reported by this firmware.
        QVERIFY(!codes.contains("0")); // Unverified macro bindings must not be guessed.
        LoadedDriver unsupported({ "another-board" }, { ASUS_DRIVER_DIR });
        QVERIFY(!unsupported.driver());
        QVERIFY(!unsupported.error().isEmpty());
    }
    void missingIncompatibleAndAmbiguousDriversDoNotActivate()
    {
        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        LoadedDriver missing({ "G615LW" }, { empty.path() });
        QVERIFY(!missing.driver());
        LoadedDriver incompatible({ "G615LW" }, { INCOMPATIBLE_DRIVER_DIR });
        QVERIFY(!incompatible.driver());
        QVERIFY(incompatible.error().contains("incompatible driver API"));
        LoadedDriver ambiguous({ "G615LW" }, { ASUS_DRIVER_DIR, CONFIG_DRIVER_DIR });
        QVERIFY(!ambiguous.driver());
        QVERIFY(ambiguous.error().contains("Multiple lighting drivers"));
        LoadedDriver repeatedPath({ "G615LW" }, { ASUS_DRIVER_DIR, ASUS_DRIVER_DIR });
        QVERIFY(repeatedPath.driver());
    }
    void asusStripCornersAndDiffusion()
    {
        LoadedDriver loaded({ "G615LW" }, { ASUS_DRIVER_DIR });
        QVERIFY(loaded.driver());
        const auto& strip = loaded.description().strips.first();
        QCOMPARE(strip.elements.size(), 12);
        QCOMPARE(strip.elements[1].label, QString("2"));
        QCOMPARE(strip.elements[4].label, QString("5"));
        QCOMPARE(strip.elements[7].label, QString("8"));
        QCOMPARE(strip.elements[10].label, QString("11"));
        const QList<int> cornerIds { 91, 97, 96, 90 };
        const QList<QPointF> corners { { 16, 25 }, { 16, 174 }, { 338, 174 }, { 338, 25 } };
        for (qsizetype i = 0; i < cornerIds.size(); ++i) {
            QVERIFY(std::any_of(strip.path.begin(), strip.path.end(),
                [&](const auto& p) { return p.elementId == cornerIds[i] && p.position == corners[i]; }));
            // A corner's own color never extends down an entire side.
            for (const auto& p : strip.path)
                if (p.elementId == cornerIds[i])
                    QVERIFY((p.position - corners[i]).manhattanLength() <= 1);
        }
        QCOMPARE(strip.path.first().elementId, strip.path.last().elementId);
        QCOMPARE(strip.path.first().position, strip.path.last().position);
        // Two non-corner elements cover each long side, without extra hardware IDs.
        int longTransitions = 0;
        for (qsizetype i = 1; i < strip.path.size(); ++i) {
            const auto& a = strip.path[i - 1];
            const auto& b = strip.path[i];
            if ((a.position - b.position).manhattanLength() > 100) {
                QVERIFY(!cornerIds.contains(a.elementId));
                QVERIFY(!cornerIds.contains(b.elementId));
                ++longTransitions;
            }
        }
        QCOMPARE(longTransitions, 4);
    }
    void validatesOptionalStripGeometry()
    {
        DeviceDescription d { "strip", "Strip", DeviceCapability::Color | DeviceCapability::PerStripColor,
            { }, { }, { { FirstStripId, "Strip", { { 9, "One", { } }, { 3, "Two", { } } } } } };
        QVERIFY(validateDescription(d).isEmpty()); // No layout: use the default straight line.
        auto& strip = d.strips.first();
        strip.path = { { 9, { -10, 0 } }, { 3, { 10, 0 } } };
        QVERIFY(validateDescription(d).isEmpty());
        strip.path[1].elementId = 100;
        QVERIFY(!validateDescription(d).isEmpty());
        strip.path[1].elementId = 9;
        QVERIFY(!validateDescription(d).isEmpty()); // Omitted element cannot be selected.
        strip.path[1].elementId = 3;
        strip.path[1].position = strip.path[0].position;
        QVERIFY(!validateDescription(d).isEmpty());
        strip.path[1].position = { std::numeric_limits<qreal>::quiet_NaN(), 0 };
        QVERIFY(!validateDescription(d).isEmpty());
        strip.path[1].position = { 100001, 0 };
        QVERIFY(!validateDescription(d).isEmpty());
        strip.path[1].position = { 10, 0 };
        strip.pathWidth = 0;
        QVERIFY(!validateDescription(d).isEmpty());
        strip.pathWidth = std::numeric_limits<qreal>::infinity();
        QVERIFY(!validateDescription(d).isEmpty());
        strip.pathWidth = 4;
        QVERIFY(validateDescription(d).isEmpty());
    }
    void multipleDevicesRunIndependentlyAndReconnect()
    {
        qApp->setProperty("test.devices", QStringList { "one", "two" });
        auto c = defaultCollection("test-strip");
        auto other = c.presets.first();
        other.id = "second";
        other.name = "Second";
        other.base[7] = { KeyMode::Static, Qt::green };
        c.presets[0].base[7] = { KeyMode::Static, Qt::red };
        c.presets.append(other);
        c.devices = QList<DeviceConfiguration> { { "test-strips", "one", "test-strip", "First", "default",
                                                     true, false },
            { "test-strips", "two", "test-strip", "Second", "second", true, true } };
        c.rules.append({ "App", "editor", "default", true });
        DeviceManager runtime({ }, { MULTI_DRIVER_DIR });
        QCOMPARE(runtime.drivers().devices().size(), 2);
        runtime.setEnabled(true);
        runtime.configure(c);
        QCOMPARE(runtime.count(), 2);
        QCOMPARE(runtime.ambientEffects(), QSet<QString> { "ambient-default" });
        runtime.submit({ { "ambient-default", Qt::blue } }, { });
        QCOMPARE(
            qApp->property("test.one.colors").toList(), (QVariantList { "#ff0000", "#0000ff", "#0000ff" }));
        QCOMPARE(
            qApp->property("test.two.colors").toList(), (QVariantList { "#00ff00", "#0000ff", "#0000ff" }));
        runtime.selectPresets("editor", { });
        runtime.submit({ { "ambient-default", Qt::blue } }, { });
        QCOMPARE(qApp->property("test.two.colors").toList().first().toString(), QString("#ff0000"));
        QCOMPARE(qApp->property("test.one.created").toInt(), 1);
        const int secondFrames = qApp->property("test.two.frames").toInt();
        (*c.devices)[0].enabled = false;
        runtime.configure(c);
        QCOMPARE(runtime.count(), 1);
        QCOMPARE(qApp->property("test.one.destroyed").toInt(), 1);
        runtime.submit({ { "ambient-default", Qt::blue } }, { });
        QCOMPARE(qApp->property("test.two.frames").toInt(), secondFrames + 1);
        QCOMPARE(qApp->property("test.two.created").toInt(), 1);
        qApp->setProperty("test.devices", QStringList { });
        QVERIFY(runtime.refresh());
        QCOMPARE(runtime.count(), 0);
        qApp->setProperty("test.devices", QStringList { "one", "two" });
        QVERIFY(runtime.refresh());
        QCOMPARE(runtime.count(), 1);
        QCOMPARE(qApp->property("test.two.created").toInt(), 2);
        QCOMPARE(qApp->property("test.one.created").toInt(), 1);
        runtime.setEnabled(false);
        const auto frames = runtime.completedFrames();
        runtime.submit({ { "ambient-default", Qt::red } }, { });
        QCOMPARE(runtime.completedFrames(), frames);
        QVERIFY(!runtime.ready());
        c.devices = QList<DeviceConfiguration> { };
        runtime.configure(c);
        QCOMPARE(runtime.count(), 0);
        qApp->setProperty("test.devices", QVariant());
    }
    void powerPoliciesHoldLightingAndSwitchOnlyRequestedStrips()
    {
        qApp->setProperty("test.devices", QStringList { "one", "two" });
        auto c = defaultCollection("test-strip");
        c.devices = QList<DeviceConfiguration> { { "test-strips", "one", "test-strip", "First", "default",
                                                     true, false, true, false, false, true },
            { "test-strips", "two", "test-strip", "Second", "default", true, false } };
        DeviceManager runtime({ }, { MULTI_DRIVER_DIR });
        runtime.configure(c);
        runtime.setEnabled(true);
        runtime.submit({ { DefaultEffectId, Qt::blue } }, { });
        const int created = qApp->property("test.one.created").toInt();
        auto color = [](const char* id) { return qApp->property(id).toString(); };
        QCOMPARE(color("test.one.uniform"), QString("#0000ff"));
        runtime.setPowerState({ true, true, false });
        QVERIFY(runtime.ready());
        runtime.submit({ { DefaultEffectId, Qt::red } }, { });
        QCOMPARE(color("test.one.uniform"), QString("#0000ff"));
        QCOMPARE(color("test.two.uniform"), QString("#ff0000"));
        QVERIFY(qApp->property("test.one.enabled").toBool());
        runtime.setPowerState({ true, true, true });
        runtime.submit({ { DefaultEffectId, Qt::red } }, { });
        QCOMPARE(color("test.one.uniform"), QString("#000000"));
        QCOMPARE(color("test.two.uniform"), QString("#ff0000"));
        QVERIFY(qApp->property("test.one.enabled").toBool());
        runtime.setPowerState({ true, true, false });
        runtime.submit({ { DefaultEffectId, Qt::green } }, { });
        QCOMPARE(color("test.one.uniform"), QString("#0000ff"));
        runtime.setPowerState({ true, false, false });
        runtime.submit({ { DefaultEffectId, Qt::green } }, { });
        QCOMPARE(color("test.one.uniform"), QString("#00ff00"));
        QCOMPARE(qApp->property("test.one.created").toInt(), created);
        // A strip-only device needs no capture when all of its elements are off.
        (*c.devices)[0].pauseOnBattery = false;
        (*c.devices)[1].enabled = false;
        runtime.configure(c);
        runtime.setPowerState({ true, true, true });
        QVERIFY(runtime.ambientEffects().isEmpty());
        QVERIFY(runtime.notificationEffects().isEmpty());
        QVERIFY(runtime.ready());
        runtime.setEnabled(false);
        runtime.setPowerState({ true, false, false });
        QVERIFY(!runtime.ready());
        QVERIFY(!qApp->property("test.one.enabled").toBool());
        qApp->setProperty("test.devices", QVariant());
    }
    void startingPausedRetainsAmbientMinimum()
    {
        qApp->setProperty("test.devices", QStringList { "one" });
        auto c = defaultCollection("test-strip");
        c.devices = QList<DeviceConfiguration> { { "test-strips", "one", "test-strip", "First", "default",
            true, false, true, false } };
        DeviceManager runtime({ }, { MULTI_DRIVER_DIR });
        runtime.setPowerState({ true, true, false });
        runtime.configure(c);
        runtime.setEnabled(true);
        runtime.submit({ }, { });
        QVERIFY(QColor(qApp->property("test.one.uniform").toString()).value() > 0);
        QVERIFY(runtime.ambientEffects().isEmpty());
        QVERIFY(qApp->property("test.one.enabled").toBool());
        qApp->setProperty("test.devices", QVariant());
    }
    void legacySettingsAddOnlyOneDeviceAndDuplicatesAreRejected()
    {
        LoadedDriver catalog({ }, { MULTI_DRIVER_DIR });
        const auto c = defaultCollection("test-strip");
        QCOMPARE(configuredDevices(c, catalog).size(), 1);
        qApp->setProperty("test.devices", QStringList { "one", "one", "two" });
        catalog.refresh();
        QCOMPARE(catalog.devices().size(), 1);
        QCOMPARE(catalog.devices().first().instance.id, QString("two"));
        QVERIFY(catalog.error().contains("duplicate"));
        qApp->setProperty("test.devices", QVariant());
    }
    void legacyChoiceDoesNotJumpToAnotherDeviceWhenDisconnected()
    {
        qApp->setProperty("test.devices", QStringList { "one", "two" });
        DeviceManager runtime({ }, { MULTI_DRIVER_DIR });
        runtime.setEnabled(true);
        const auto c = defaultCollection("test-strip");
        runtime.configure(c);
        QCOMPARE(runtime.count(), 1);
        QVERIFY(runtime.status().contains("test-strips:one"));
        qApp->setProperty("test.devices", QStringList { "two" });
        QVERIFY(runtime.refresh());
        QCOMPARE(runtime.count(), 0);
        runtime.configure(c);
        QCOMPARE(runtime.count(), 0);
        qApp->setProperty("test.devices", QStringList { });
        runtime.refresh();
        QVERIFY(runtime.drivers().hasDrivers());
        qApp->setProperty("test.devices", QStringList { "two", "one" });
        QVERIFY(runtime.refresh());
        QCOMPARE(runtime.count(), 1);
        QVERIFY(runtime.status().contains("test-strips:one"));
        qApp->setProperty("test.devices", QVariant());
    }
    void validatesCapabilitiesAndStableIds()
    {
        DeviceDescription d { "test-keyboard", "Test keyboard", DeviceCapability::Brightness,
            { { 900, "A", QRectF(0, 0, 10, 10) }, { 3, "B", QRectF(11, 0, 10, 10) } }, { } };
        QVERIFY(validateDescription(d).isEmpty());
        d.capabilities = DeviceCapability::Color;
        QVERIFY(validateDescription(d).isEmpty());
        d.capabilities |= DeviceCapability::PerKeyColor;
        QVERIFY(validateDescription(d).isEmpty());
        d.keys[1].id = 900;
        QVERIFY(!validateDescription(d).isEmpty());
        d.keys[1].id = 3;
        d.groups = { { "Unknown", { 4 } } };
        QVERIFY(!validateDescription(d).isEmpty());
        d.groups.clear();
        d.capabilities = DeviceCapability::PerKeyColor;
        QVERIFY(!validateDescription(d).isEmpty());
        d.capabilities = DeviceCapability::Color | DeviceCapability::LightbarPower;
        QVERIFY(!validateDescription(d).isEmpty());
    }
};
QTEST_GUILESS_MAIN(DriversTest)
#include "test_drivers.moc"
