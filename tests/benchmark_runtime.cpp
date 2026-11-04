// SPDX-License-Identifier: GPL-3.0-or-later
#include "component_info.h"
#include "lighting_runtime.h"
#include <QAbstractEventDispatcher>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <ctime>

using namespace Ambient;
namespace
{
constexpr int WarmupMs = 1000, MeasurementMs = 2000;
void runFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, Qt::PreciseTimer, &loop, &QEventLoop::quit);
    loop.exec();
}
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
QString cpuModel()
{
    QFile cpu(QStringLiteral("/proc/cpuinfo"));
    if (cpu.open(QIODevice::ReadOnly))
        while (true) {
            const auto line = cpu.readLine();
            if (line.isEmpty())
                break;
            if (line.startsWith("model name"))
                return QString::fromUtf8(line.mid(line.indexOf(':') + 1).trimmed());
        }
    return QStringLiteral("Unknown");
}
struct Scenario {
    QString name;
    int devices = 1, ledFps = 60, smoothing = 0;
    bool ambient = true, changing = false, notification = false, mixed = false;
};
QJsonObject measure(const Scenario& scenario)
{
    QStringList ids;
    auto collection = defaultCollection("test-strip");
    collection.devices = QList<DeviceConfiguration>();
    auto& profile = collection.presets[0];
    profile.layers.clear();
    profile.effects[DefaultAssignment] = scenario.ambient
        ? EffectList { { DefaultEffectId } }
        : EffectList { { InlineColorId, 100, QColor(Qt::red) } };
    collection.effects[0].ambient = { 20, scenario.ledFps, scenario.smoothing, 0, 100, 0 };
    if (scenario.mixed) {
        EffectPreset slow;
        slow.id = "slow";
        slow.ambient = { 4, 10, 800, 0, 100, 0 };
        collection.effects.append(slow);
        profile.effects[DefaultAssignment].append({ slow.id, 50 });
        profile.layers = { KeyboardLayer(), KeyboardLayer { "Ctrl", { "ctrl" } } };
    }
    if (scenario.notification) {
        EffectPreset notification;
        notification.id = "notification";
        notification.type = "notifications";
        notification.notification.period = 400;
        notification.notification.cycles = 20;
        collection.effects.append(notification);
        profile.effects[DefaultAssignment].append({ notification.id });
    }
    for (int i = 0; i < scenario.devices; ++i) {
        const auto id = QStringLiteral("device-%1").arg(i);
        ids.append(id);
        collection.devices->append({ "test-strips", id, "test-strip", id, true });
    }
    qApp->setProperty("test.devices", ids);
    LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
    runtime.configure(collection);
    runtime.setEnabled(true);
    runtime.setCaptureAvailable(scenario.ambient);
    if (runtime.status().value("devices").toInt() != scenario.devices
        || runtime.captureFps() != (scenario.ambient ? 20 : 0))
        qFatal("Benchmark fake devices or effect assignments did not initialize");
    const auto red = sample(Qt::red), blue = sample(Qt::blue);
    int injected = 0;
    QTimer input;
    input.setTimerType(Qt::PreciseTimer);
    QObject::connect(&input, &QTimer::timeout, &runtime, [&] {
        ++injected;
        runtime.sample(scenario.changing && injected % 2 == 0 ? blue : red);
    });
    if (scenario.ambient) {
        runtime.sample(red);
        input.start(50); // The input producer requests 20 FPS independently of LED output.
    }
    if (scenario.notification)
        runtime.notify({ });
    runFor(WarmupMs);
    injected = 0;
    int awakeSignals = 0;
    const auto wake = QObject::connect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::awake,
        &runtime, [&] { ++awakeSignals; });
    const auto before = runtime.status();
    const auto cpuStart = std::clock();
    QElapsedTimer elapsed;
    elapsed.start();
    runFor(MeasurementMs);
    const auto elapsedMs = elapsed.elapsed();
    const double cpuMs = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
    QObject::disconnect(wake);
    const auto after = runtime.status();
    QJsonObject result { { "elapsedMs", elapsedMs }, { "cpuMs", cpuMs }, { "injectedSamples", injected },
        { "dispatcherAwakeSignals", awakeSignals } };
    for (const auto* key : { "samples", "animationWakeups", "ticks", "effectUpdates", "compositions",
             "profileResolutions", "sent" })
        result.insert(key == QLatin1String("sent") ? QStringLiteral("fakeBackendSubmissions") : key,
            qint64(after.value(key).toULongLong() - before.value(key).toULongLong()));
    return result;
}
}
int main(int argc, char** argv)
{
    QTemporaryDir config;
    if (!config.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", config.path().toUtf8());
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Isolated runtime benchmark; fake drivers, no D-Bus or hardware");
    parser.addHelpOption();
    parser.addOption({ "runs", "Repetitions per scenario (1-10)", "count", "3" });
    parser.process(app);
    bool valid = false;
    const int runs = parser.value("runs").toInt(&valid);
    if (!valid || runs < 1 || runs > 10)
        return 1;
    const QList<Scenario> scenarios {
        { "solid-idle", 1, 60, 0, false },
        { "ambient-settled" },
        { "ambient-changing", 1, 60, 250, true, true },
        { "ambient-20fps-output-1fps", 1, 1, 0, true, true },
        { "notifications-only", 1, 60, 0, false, false, true },
        { "mixed-three-devices", 3, 60, 250, true, true, false, true },
    };
    QJsonArray results;
    for (const auto& scenario : scenarios) {
        QJsonArray repetitions;
        for (int i = 0; i < runs; ++i)
            repetitions.append(measure(scenario));
        results.append(QJsonObject { { "name", scenario.name }, { "devices", scenario.devices },
            { "elementsPerDevice", 3 }, { "layers", scenario.mixed ? 2 : 0 },
            { "activeEffectStates",
                int(scenario.ambient) + int(scenario.mixed) + int(scenario.notification) },
            { "sampleFps", scenario.ambient ? 20 : 0 }, { "ledFps", scenario.ledFps },
            { "smoothingMs", scenario.smoothing }, { "runs", repetitions } });
    }
    const QJsonObject output { { "scope", "isolated LightingRuntime with fake driver; no GPU or USB" },
        { "cpu", cpuModel() }, { "qt", qVersion() }, { "compiler", __VERSION__ }, { "buildType", BUILD_TYPE },
        { "component", QJsonObject::fromVariantMap(componentInfo(Component::Service)) },
        { "warmupMs", WarmupMs }, { "measurementMs", MeasurementMs }, { "scenarios", results } };
    QTextStream(stdout) << QJsonDocument(output).toJson();
}
