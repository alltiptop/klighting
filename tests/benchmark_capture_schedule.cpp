// SPDX-License-Identifier: GPL-3.0-or-later
#include "capture_schedule.h"
#include "component_info.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <ctime>

using namespace Ambient;
namespace
{
constexpr int WarmupMs = 1000, MeasurementMs = 2000;
void runFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, Qt::PreciseTimer, &loop, &QEventLoop::quit);
    loop.exec();
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
    int fps, readbackMs, paintIntervalMs = 0;
    bool finalBurst = false;
};
QJsonObject measure(const Scenario& scenario)
{
    QObject context;
    CaptureSchedule schedule;
    schedule.reset((1000 + scenario.fps - 1) / scenario.fps);
    QElapsedTimer clock;
    clock.start();
    const auto now = [&] { return clock.nsecsElapsed() / 1000000.0; };
    QTimer timer, source;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setSingleShot(true);
    source.setTimerType(Qt::PreciseTimer);
    QMap<QString, int> counters { { "timerWakeups", 0 }, { "repaintRequests", 0 }, { "paintCallbacks", 0 },
        { "externalPaints", 0 }, { "captures", 0 }, { "polls", 0 }, { "samples", 0 } };
    bool pending = false, repaintQueued = false;
    double readyAt = 0;
    int version = 0, capturedVersion = -1, deliveredVersion = -1;
    const auto updateTimer = [&] {
        const int delay = schedule.nextUpdateMs(now(), pending);
        if (delay < 0)
            timer.stop();
        else if (!timer.isActive() || timer.remainingTime() > delay)
            timer.start(delay);
    };
    const auto paint = [&] {
        ++counters["paintCallbacks"];
        if (schedule.captureOnPaint(now(), pending)) {
            ++counters["captures"];
            pending = true;
            capturedVersion = version;
            readyAt = now() + scenario.readbackMs;
            schedule.captured(now());
        }
        updateTimer();
    };
    const auto requestRepaint = [&] {
        ++counters["repaintRequests"];
        if (!repaintQueued) {
            repaintQueued = true;
            QTimer::singleShot(1, Qt::PreciseTimer, &context, [&] {
                repaintQueued = false;
                paint();
            });
        }
    };
    const auto externalPaint = [&] {
        ++version;
        ++counters["externalPaints"];
        paint();
    };
    QObject::connect(&timer, &QTimer::timeout, &context, [&] {
        ++counters["timerWakeups"];
        if (pending) {
            ++counters["polls"];
            if (now() >= readyAt) {
                pending = false;
                deliveredVersion = capturedVersion;
                ++counters["samples"];
            }
        }
        if (schedule.requestRepaint(now(), pending))
            requestRepaint();
        updateTimer();
    });
    QObject::connect(&source, &QTimer::timeout, &context, externalPaint);
    if (scenario.paintIntervalMs > 0)
        source.start(scenario.paintIntervalMs);
    if (scenario.finalBurst) {
        QTimer::singleShot(WarmupMs + 100, Qt::PreciseTimer, &context, externalPaint);
        QTimer::singleShot(WarmupMs + 110, Qt::PreciseTimer, &context, externalPaint);
    }
    if (schedule.requestRepaint(0, false))
        requestRepaint();
    updateTimer();
    runFor(WarmupMs);
    if (counters["samples"] == 0)
        qFatal("Capture policy did not deliver its initial synthetic frame");
    const auto before = counters;
    const auto started = clock.elapsed();
    const auto cpuStart = std::clock();
    runFor(MeasurementMs);
    const double cpuMs = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
    if (scenario.finalBurst && deliveredVersion != version)
        qFatal("Capture policy lost the final synthetic frame");
    QJsonObject result { { "elapsedMs", clock.elapsed() - started }, { "cpuMs", cpuMs },
        { "warmupSamples", before["samples"] }, { "lastInputVersion", version },
        { "lastSampleVersion", deliveredVersion } };
    for (auto it = counters.cbegin(); it != counters.cend(); ++it)
        result.insert(it.key(), it.value() - before[it.key()]);
    return result;
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Capture timing benchmark with synthetic paints/readbacks; no KWin/GL");
    parser.addHelpOption();
    parser.addOption({ "runs", "Repetitions per scenario (1-10)", "count", "3" });
    parser.process(app);
    bool valid = false;
    const int runs = parser.value("runs").toInt(&valid);
    if (!valid || runs < 1 || runs > 10)
        return 1;
    const QList<Scenario> scenarios {
        { "static-3fps", 3, 1 },
        { "static-20fps", 20, 1 },
        { "continuous-paints", 20, 1, 17 },
        { "final-change-before-deadline", 3, 1, 0, true },
        { "slow-readback", 20, 80, 17 },
        { "final-change-while-pending", 20, 80, 0, true },
    };
    QJsonArray results;
    for (const auto& scenario : scenarios) {
        QJsonArray repetitions;
        for (int i = 0; i < runs; ++i)
            repetitions.append(measure(scenario));
        results.append(QJsonObject { { "name", scenario.name }, { "captureFps", scenario.fps },
            { "syntheticReadbackMs", scenario.readbackMs },
            { "externalPaintIntervalMs", scenario.paintIntervalMs }, { "finalBurst", scenario.finalBurst },
            { "runs", repetitions } });
    }
    const QJsonObject output { { "scope", "shared CaptureSchedule; synthetic paints/readbacks, no KWin/GL" },
        { "cpu", cpuModel() }, { "qt", qVersion() }, { "compiler", __VERSION__ }, { "buildType", BUILD_TYPE },
        { "component", QJsonObject::fromVariantMap(componentInfo(Component::Capture)) },
        { "warmupMs", WarmupMs }, { "measurementMs", MeasurementMs }, { "scenarios", results } };
    QTextStream(stdout) << QJsonDocument(output).toJson();
}
