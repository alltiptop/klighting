// SPDX-License-Identifier: GPL-3.0-or-later
#include "lighting_runtime.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

using namespace Ambient;
namespace
{
QByteArray sample(const QColor& color)
{
    QByteArray bytes(32 * 18 * 4, '\0');
    for (int i = 0; i < bytes.size(); i += 4) {
        bytes[i] = char(color.red());
        bytes[i + 1] = char(color.green());
        bytes[i + 2] = char(color.blue());
        bytes[i + 3] = char(255);
    }
    return bytes;
}
PresetCollection collection(bool ambient = true, int ledFps = 60, int smoothing = 0)
{
    auto c = defaultCollection("test-strip");
    c.devices = QList<DeviceConfiguration> { { "test-strips", "one", "test-strip", "Strip", true } };
    c.presets[0].layers.clear();
    c.presets[0].effects[DefaultAssignment] = ambient
        ? EffectList { { DefaultEffectId } }
        : EffectList { { InlineColorId, 100, QColor(Qt::red) } };
    c.effects[0].ambient = { 20, ledFps, smoothing, 0, 100, 0 };
    return c;
}
QColor output()
{
    const auto elements = qApp->property("test.one.colors").toList();
    return QColor(
        elements.isEmpty() ? qApp->property("test.one.uniform").toString() : elements.first().toString());
}
quint64 counter(const LightingRuntime& runtime, const char* key)
{
    return runtime.status().value(key).toULongLong();
}
void start(LightingRuntime& runtime, const PresetCollection& c, bool capture = true)
{
    runtime.configure(c);
    runtime.setEnabled(true);
    runtime.setCaptureAvailable(capture);
    QCoreApplication::processEvents(); // Drain synchronous backend readiness notifications.
}
}
class RuntimeSchedulingTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        qApp->setProperty("test.devices", QStringList { "one" });
        for (const auto* name :
            { "test.one.ready", "test.one.notifyReady", "test.one.uniform", "test.one.colors" })
            qApp->setProperty(name, QVariant());
    }
    void roundedColorsDoNotEndSlowTransitions()
    {
        ColorSmoother smoother;
        QVERIFY(!smoother.active());
        smoother.setTarget(Qt::black);
        QCOMPARE(smoother.step(0, 3000), QColor(Qt::black));
        smoother.setTarget(QColor(1, 0, 0));
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(smoother.step(17, 3000), QColor(Qt::black));
            QVERIFY(smoother.active());
        }
        for (int i = 0; i < 1000 && smoother.active(); ++i)
            smoother.step(17, 3000);
        QVERIFY(!smoother.active());
        QCOMPARE(smoother.color(), QColor(1, 0, 0));
        smoother.reset();
        QVERIFY(!smoother.active());
        QVERIFY(!smoother.color());
    }
    void finalOutputWaitsForItsLedDeadline()
    {
        EffectPreset effect;
        effect.id = "ambient";
        effect.ambient = { 20, 1, 0, 0, 100, 0 };
        EffectPalette palette;
        palette.configure({ effect }, { effect.id });
        palette.sample(Qt::red);
        palette.advance(0);
        QCOMPARE(palette.colors()[effect.id], QColor(Qt::red));
        QCOMPARE(palette.nextUpdateMs(), -1);
        palette.advance(50, false);
        palette.sample(Qt::blue);
        palette.advance(0, false);
        QCOMPARE(palette.nextUpdateMs(), 950);
        palette.advance(949);
        QCOMPARE(palette.colors()[effect.id], QColor(Qt::red));
        palette.advance(1);
        QCOMPARE(palette.colors()[effect.id], QColor(Qt::blue));
        QCOMPARE(palette.nextUpdateMs(), -1);
    }
    void pendingSampleKeepsOnlyTheLatestInput()
    {
        EffectPreset slow, fast;
        slow.id = "slow";
        fast.id = "fast";
        slow.ambient = { 1, 60, 0, 0, 100, 0 };
        fast.ambient = { 20, 60, 0, 0, 100, 0 };
        EffectPalette palette;
        palette.configure({ slow, fast }, { slow.id, fast.id });
        palette.sample(Qt::red);
        palette.advance(0);
        palette.advance(100, false);
        palette.sample(Qt::blue);
        palette.advance(0);
        QCOMPARE(palette.colors()[slow.id], QColor(Qt::red));
        QCOMPARE(palette.colors()[fast.id], QColor(Qt::blue));
        palette.advance(100, false);
        palette.sample(Qt::green);
        palette.advance(0);
        QCOMPARE(palette.colors()[fast.id], QColor(Qt::green));
        QCOMPARE(palette.nextUpdateMs(), 800);
        palette.advance(800);
        QCOMPARE(palette.colors()[slow.id], QColor(Qt::green));
        QCOMPARE(palette.nextUpdateMs(), -1);
        palette.advance(100, false);
        palette.sample(Qt::blue);
        palette.advance(100, false);
        palette.sample(Qt::green); // Replace the queued blue with the current target.
        palette.advance(0);
        QCOMPARE(palette.nextUpdateMs(), -1);
        palette.advance(100000, false); // An idle clock must not age the next transition.
        slow.ambient.smoothing = 400;
        palette.configure({ slow }, { slow.id });
        palette.sample(Qt::red);
        palette.advance(0);
        QCOMPARE(palette.colors()[slow.id], QColor(Qt::green));
        palette.advance(17);
        const auto transition = palette.colors()[slow.id];
        QVERIFY(transition.red() > 0 && transition.red() < 255);
        QVERIFY(transition.green() > 0 && transition.green() < 255);
    }
    void settledAmbientDoesNotAnimateOrCompose()
    {
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, collection());
        runtime.sample(sample(Qt::red));
        QTRY_COMPARE(output(), QColor(Qt::red));
        const auto wakeups = counter(runtime, "animationWakeups");
        const auto compositions = counter(runtime, "compositions");
        const auto samples = counter(runtime, "samples");
        for (int i = 0; i < 6; ++i) {
            QTest::qWait(60);
            runtime.sample(sample(Qt::red));
        }
        QTest::qWait(2100); // Discovery must not recompose a stable device either.
        QCOMPARE(counter(runtime, "animationWakeups"), wakeups);
        QCOMPARE(counter(runtime, "compositions"), compositions);
        QVERIFY(counter(runtime, "samples") > samples);
        QCOMPARE(runtime.captureFps(), 20);
    }
    void ambientLayerUsesLastScreenColorWhileCaptureIsIdle()
    {
        auto c = collection();
        auto second = c.effects[0];
        second.id = "ambient-second";
        c.effects.append(second);
        KeyboardLayer layer;
        layer.effects[DefaultAssignment] = { { second.id } };
        c.presets[0].layers.append(layer);
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.sample(sample(Qt::red));
        QTRY_COMPARE(output(), QColor(Qt::red));
        const auto samples = counter(runtime, "samples");
        ModifierState shift;
        shift.effective = Qt::ShiftModifier;
        runtime.setDesktop("editor", shift);
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::red), 500);
        QCOMPARE(runtime.captureFps(), 20);
        QCOMPARE(counter(runtime, "samples"), samples);
        runtime.setDesktop("editor", { });
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::red), 500);
        const auto wakeups = counter(runtime, "animationWakeups");
        const auto compositions = counter(runtime, "compositions");
        QTest::qWait(100);
        QCOMPARE(counter(runtime, "animationWakeups"), wakeups);
        QCOMPARE(counter(runtime, "compositions"), compositions);
    }
    void ambientEditsReprocessLastScreenColor()
    {
        auto c = collection();
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.sample(sample(Qt::white));
        QTRY_COMPARE(output(), QColor(Qt::white));
        c.effects[0].ambient.temperature = -100;
        runtime.configure(c);
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(255, 191, 38), 500);
        QCOMPARE(counter(runtime, "samples"), 1);
        c.effects[0].ambient.temperature = 0;
        runtime.configure(c);
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::white), 500);
        QCOMPARE(counter(runtime, "samples"), 1);
    }
    void unavailableCaptureDoesNotSeedNewAmbientLayer_data()
    {
        QTest::addColumn<QString>("change");
        for (const auto* name : { "capture", "lock", "disable", "power" })
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void unavailableCaptureDoesNotSeedNewAmbientLayer()
    {
        QFETCH(QString, change);
        auto c = collection();
        c.effects[0].pauseOnBattery = true;
        auto second = c.effects[0];
        second.id = "ambient-second";
        c.effects.append(second);
        KeyboardLayer layer;
        layer.effects[DefaultAssignment] = { { second.id } };
        c.presets[0].layers.append(layer);
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.sample(sample(Qt::red));
        QTRY_COMPARE(output(), QColor(Qt::red));
        if (change == "capture") {
            runtime.setCaptureAvailable(false);
            runtime.setCaptureAvailable(true);
        } else if (change == "lock") {
            runtime.setLocked(true);
            runtime.setLocked(false);
        } else if (change == "disable") {
            runtime.setEnabled(false);
            runtime.setEnabled(true);
        } else {
            runtime.setPowerState({ true, true, false });
            runtime.setPowerState({ true, false, false });
        }
        ModifierState shift;
        shift.effective = Qt::ShiftModifier;
        runtime.setDesktop("editor", shift);
        QTest::qWait(100);
        QCOMPARE(output(), QColor(Qt::black));
        runtime.sample(sample(Qt::blue));
        QTRY_COMPARE(output(), QColor(Qt::blue));
    }
    void samplesRespectSlowOutputAndKeepTheLastColor()
    {
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, collection(true, 1));
        runtime.sample(sample(Qt::red));
        QTRY_COMPARE(output(), QColor(Qt::red));
        const auto compositions = counter(runtime, "compositions");
        for (int i = 0; i < 8; ++i) {
            QTest::qWait(60);
            runtime.sample(sample(i % 2 ? Qt::blue : Qt::green));
        }
        QCOMPARE(output(), QColor(Qt::red));
        QCOMPARE(counter(runtime, "compositions"), compositions);
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::blue), 1200);
        QCOMPARE(counter(runtime, "compositions"), compositions + 1);
        const auto wakeups = counter(runtime, "animationWakeups");
        QTest::qWait(150);
        QCOMPARE(counter(runtime, "animationWakeups"), wakeups);
    }
    void newTargetAfterIdleStillTransitions()
    {
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, collection(true, 60, 100));
        runtime.sample(sample(Qt::red));
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::red), 1500);
        QTest::qWait(700);
        runtime.sample(sample(Qt::blue));
        QCoreApplication::processEvents();
        QVERIFY(output().red() > 200);
        QTest::qWait(40);
        QVERIFY(output().red() > 0 && output().blue() > 0);
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::blue), 1500);
    }
    void lastEarlySampleSurvivesWithoutFurtherInput()
    {
        auto c = collection();
        c.effects[0].ambient.fps = 2;
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.sample(sample(Qt::red));
        QTRY_COMPARE(output(), QColor(Qt::red));
        for (int i = 0; i < 100; ++i)
            runtime.sample(sample(i == 99 ? Qt::green : Qt::blue));
        runtime.sample("invalid"); // Malformed input must not replace the last valid frame.
        QCOMPARE(counter(runtime, "samples"), 1);
        QTest::qWait(100);
        QCOMPARE(output(), QColor(Qt::red));
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::green), 700);
        QCOMPARE(counter(runtime, "samples"), 2);
        const auto wakeups = counter(runtime, "animationWakeups");
        QTest::qWait(100);
        QCOMPARE(counter(runtime, "animationWakeups"), wakeups);
    }
    void unavailableInputDiscardsQueuedSample_data()
    {
        QTest::addColumn<QString>("change");
        for (const auto* name : { "capture", "lock", "disable", "power", "configure" })
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void unavailableInputDiscardsQueuedSample()
    {
        QFETCH(QString, change);
        auto c = collection();
        c.effects[0].ambient.fps = 2;
        c.effects[0].pauseOnBattery = true;
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c);
        runtime.sample(sample(Qt::red));
        QTRY_COMPARE(output(), QColor(Qt::red));
        runtime.sample(sample(Qt::blue));
        if (change == "capture") {
            runtime.setCaptureAvailable(false);
            runtime.setCaptureAvailable(true);
        } else if (change == "lock") {
            runtime.setLocked(true);
            runtime.setLocked(false);
        } else if (change == "disable") {
            runtime.setEnabled(false);
            runtime.setEnabled(true);
        } else if (change == "power") {
            runtime.setPowerState({ true, true, false });
            runtime.setPowerState({ true, false, false });
        } else {
            runtime.configure(c);
        }
        QTest::qWait(600);
        QCOMPARE(output(), QColor(Qt::red));
        QCOMPARE(counter(runtime, "samples"), 1);
        runtime.sample(sample(Qt::green));
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::green), 700);
        QCOMPARE(counter(runtime, "samples"), 2);
    }
    void backendReadiness_data()
    {
        QTest::addColumn<bool>("notify");
        QTest::newRow("callback") << true;
        QTest::newRow("discovery-fallback") << false;
    }
    void backendReadiness()
    {
        QFETCH(bool, notify);
        qApp->setProperty("test.one.ready", false);
        qApp->setProperty("test.one.notifyReady", notify);
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, collection(false), false);
        QCOMPARE(counter(runtime, "compositions"), 0);
        qApp->setProperty("test.one.ready", true);
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::red), notify ? 500 : 2500);
        const auto compositions = counter(runtime, "compositions");
        QTest::qWait(100);
        QCOMPARE(counter(runtime, "compositions"), compositions);
        if (notify) {
            qApp->setProperty("test.one.ready", false);
            qApp->setProperty("test.one.ready", true);
            QTRY_COMPARE(counter(runtime, "compositions"), compositions + 1);
        }
    }
    void notificationAfterIdleAndFinalOverlayRemoval()
    {
        auto c = collection(false);
        EffectPreset notify;
        notify.id = "notify";
        notify.type = "notifications";
        notify.notification.pattern = "blink";
        notify.notification.color = Qt::green;
        notify.notification.period = 200;
        notify.notification.cycles = 1;
        notify.notification.application = "editor";
        c.effects.append(notify);
        c.presets[0].effects[DefaultAssignment].append({ notify.id });
        LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
        start(runtime, c, false);
        QTest::qWait(500);
        const auto compositions = counter(runtime, "compositions");
        runtime.notify({ "unmatched" });
        QCOMPARE(counter(runtime, "compositions"), compositions);
        runtime.notify({ "editor" });
        QCOMPARE(output(), QColor(Qt::green));
        runtime.notify({ "editor" }); // Coalescing cannot restart or recompose the pulse.
        QCOMPARE(counter(runtime, "compositions"), compositions + 1);
        QTRY_COMPARE_WITH_TIMEOUT(output(), QColor(Qt::red), 500);
        QTest::qWait(150);
        const auto wakeups = counter(runtime, "animationWakeups");
        QTest::qWait(100);
        QCOMPARE(counter(runtime, "animationWakeups"), wakeups);
        QCOMPARE(runtime.captureFps(), 0);
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
    RuntimeSchedulingTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_runtime_scheduling.moc"
