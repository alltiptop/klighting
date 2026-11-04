// SPDX-License-Identifier: GPL-3.0-or-later
#include "component_info.h"
#include "lighting_runtime.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <ctime>

using namespace Ambient;
namespace
{
constexpr int WarmupEvents = 200, MeasuredEvents = 1000;
enum class Input {
    UnchangedLayer,
    UnchangedPreset,
    Shift,
    KeyCombination,
    CapsLock,
    Application,
    Power,
    Edit
};
struct Scenario {
    QString name;
    Input input;
    int devices = 1;
};
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
QJsonObject measure(const Scenario& scenario)
{
    QStringList ids;
    auto c = defaultCollection("test-strip");
    c.devices = QList<DeviceConfiguration>();
    auto& base = c.presets[0];
    base.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::blue) } };
    base.layers = { KeyboardLayer(), KeyboardLayer { "Ctrl+A", { "ctrl", "key:30" } } };
    base.layers[0].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::red) } };
    base.layers[1].effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::green) } };
    auto editor = base;
    editor.id = "editor";
    editor.layers.clear();
    editor.effects[DefaultAssignment] = { { InlineColorId, 100, QColor(Qt::green) } };
    c.presets.append(editor);
    c.rules = { { "Editor", "editor", "editor", true } };
    for (int i = 0; i < scenario.devices; ++i) {
        const auto id = QStringLiteral("device-%1").arg(i);
        ids.append(id);
        c.devices->append({ "test-strips", id, "test-strip", id, true, false, false, true });
    }
    qApp->setProperty("test.devices", ids);
    LightingRuntime runtime({ }, { MULTI_DRIVER_DIR });
    runtime.configure(c);
    runtime.setEnabled(true);
    runtime.setPowerState({ true, false, false });
    QCoreApplication::processEvents();
    if (runtime.status().value("devices").toInt() != scenario.devices || runtime.captureFps() != 0)
        qFatal("Profile benchmark failed to initialize its static fake devices");
    const auto input = [&](int i) {
        const bool active = i % 2 == 0;
        ModifierState modifiers;
        switch (scenario.input) {
        case Input::UnchangedLayer:
            if (active)
                modifiers.pressed.insert(30); // Tracked by Ctrl+A, but Ctrl is not held.
            runtime.setDesktop({ }, modifiers);
            break;
        case Input::UnchangedPreset:
            runtime.setDesktop(active ? "browser" : "notes", { });
            break;
        case Input::Shift:
            modifiers.effective = active ? Qt::ShiftModifier : Qt::NoModifier;
            runtime.setDesktop({ }, modifiers);
            break;
        case Input::KeyCombination:
            modifiers.effective = Qt::ControlModifier;
            if (active)
                modifiers.pressed.insert(30);
            runtime.setDesktop({ }, modifiers);
            break;
        case Input::CapsLock:
            modifiers.capsLock = active;
            runtime.setDesktop({ }, modifiers);
            break;
        case Input::Application:
            runtime.setDesktop(active ? "editor" : "browser", { });
            break;
        case Input::Power:
            runtime.setPowerState({ true, active, false });
            break;
        case Input::Edit:
            c.presets[0].effects[DefaultAssignment][0].color = active ? Qt::green : Qt::blue;
            runtime.configure(c);
            break;
        }
    };
    for (int i = 0; i < WarmupEvents; ++i)
        input(i);
    QCoreApplication::processEvents();
    const auto before = runtime.status();
    const auto cpuStart = std::clock();
    QElapsedTimer elapsed;
    elapsed.start();
    for (int i = 0; i < MeasuredEvents; ++i)
        input(i);
    const double elapsedMs = elapsed.nsecsElapsed() / 1000000.0;
    const double cpuMs = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
    const auto after = runtime.status();
    for (const auto& id : ids)
        if (qApp->property(("test." + id + ".colors").toUtf8()).toList()
            != QVariantList { "#0000ff", "#0000ff", "#0000ff" })
            qFatal("Profile benchmark did not restore the final base output");
    QJsonObject result { { "elapsedMs", elapsedMs }, { "cpuMs", cpuMs } };
    for (const auto* key : { "profileResolutions", "effectUpdates", "compositions", "sent" })
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
    parser.setApplicationDescription("Profile transitions with fake drivers; no D-Bus, GPU or USB");
    parser.addHelpOption();
    parser.addOption({ "runs", "Repetitions per scenario (1-10)", "count", "3" });
    parser.process(app);
    bool valid = false;
    const int runs = parser.value("runs").toInt(&valid);
    if (!valid || runs < 1 || runs > 10)
        return 1;
    const QList<Scenario> scenarios {
        { "tracked-key-same-layer", Input::UnchangedLayer },
        { "window-same-preset", Input::UnchangedPreset },
        { "shift-layer", Input::Shift },
        { "ctrl-key-layer", Input::KeyCombination },
        { "caps-lock-layer", Input::CapsLock },
        { "application-preset", Input::Application },
        { "power-lightbar", Input::Power },
        { "preset-edit", Input::Edit },
        { "three-devices-shift", Input::Shift, 3 },
    };
    QJsonArray results;
    for (const auto& scenario : scenarios) {
        QJsonArray repetitions;
        for (int i = 0; i < runs; ++i)
            repetitions.append(measure(scenario));
        results.append(QJsonObject { { "name", scenario.name }, { "devices", scenario.devices },
            { "elementsPerDevice", 3 }, { "presets", 2 }, { "defaultPresetLayers", 2 },
            { "runs", repetitions } });
    }
    const QJsonObject output { { "scope",
                                   "synchronous LightingRuntime inputs; fake driver, no D-Bus/GPU/USB" },
        { "cpu", cpuModel() }, { "qt", qVersion() }, { "compiler", __VERSION__ }, { "buildType", BUILD_TYPE },
        { "component", QJsonObject::fromVariantMap(componentInfo(Component::Service)) },
        { "warmupEvents", WarmupEvents }, { "measuredEvents", MeasuredEvents }, { "scenarios", results } };
    QTextStream(stdout) << QJsonDocument(output).toJson();
}
