// SPDX-License-Identifier: GPL-3.0-or-later
#include "component_info.h"
#include "lamp_array.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QtEndian>
#include <ctime>
#include <optional>

using namespace Ambient;
namespace
{
constexpr int WarmupFrames = 200, MeasuredFrames = 1000;
enum class Input {
    Identical,
    DisabledKeyboard,
    DisabledStrip,
    ColorSpec,
    PacketLayout,
    HardwareBrightness,
    Keyboard,
    Strip,
    Power
};
struct Scenario {
    QString name;
    Input input;
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
LedFrame frameAt(Input input, int i)
{
    LedFrame frame { QColor(Qt::red), 2, true, true };
    const bool alternate = i % 2 == 0;
    switch (input) {
    case Input::Identical:
        break;
    case Input::DisabledKeyboard:
        frame.keyboard = false;
        frame.color = alternate ? Qt::green : Qt::blue;
        frame.keyColors = QList<QColor>(88, frame.color);
        frame.stripColors = QList<QColor>(12, Qt::red);
        break;
    case Input::DisabledStrip:
        frame.lightbar = false;
        frame.stripColors = QList<QColor>(12, alternate ? Qt::green : Qt::blue);
        break;
    case Input::ColorSpec:
        if (alternate)
            frame.color = QColor::fromHsv(0, 255, 255, 73);
        break;
    case Input::PacketLayout:
        if (alternate) {
            frame.keyColors = QList<QColor>(88, Qt::red);
            for (int lamp = 0; lamp < 88; ++lamp)
                frame.keyColors[lamp].setAlpha(lamp + 1);
        }
        break;
    case Input::HardwareBrightness:
        frame.brightness = 1 + i % 3;
        break;
    case Input::Keyboard:
        frame.keyColors = QList<QColor>(88, Qt::red);
        frame.keyColors[3] = alternate ? Qt::green : Qt::blue;
        break;
    case Input::Strip:
        frame.stripColors = QList<QColor>(12, Qt::red);
        frame.stripColors[7] = alternate ? Qt::green : Qt::blue;
        break;
    case Input::Power:
        frame.keyboard = alternate;
        break;
    }
    return frame;
}
// Decode the final wire reports, independently of the production encoder.
QByteArray decoded(const QList<QByteArray>& reports)
{
    QByteArray values(400, '\0');
    QList<bool> seen(100, false);
    int commits = 0;
    for (const auto& report : reports) {
        const auto assign = [&](int lamp, const char* rgbi) {
            if (lamp < 0 || lamp >= 100 || seen[lamp])
                qFatal("Invalid or repeated lamp in benchmark output");
            seen[lamp] = true;
            for (int channel = 0; channel < 4; ++channel)
                values[4 * lamp + channel] = rgbi[channel];
        };
        if (report.size() == 10 && report[0] == 5) {
            const int first = qFromLittleEndian<quint16>(report.data() + 2);
            const int last = qFromLittleEndian<quint16>(report.data() + 4);
            for (int lamp = first; lamp <= last; ++lamp)
                assign(lamp, report.data() + 6);
            commits += report[1] == 1;
        } else if (report.size() == 51 && report[0] == 4) {
            for (int i = 0; i < uchar(report[1]); ++i)
                assign(qFromLittleEndian<quint16>(report.data() + 3 + 2 * i), report.data() + 19 + 4 * i);
            commits += report[2] == 1;
        } else
            qFatal("Unexpected benchmark report");
    }
    if (commits != 1 || seen.contains(false))
        qFatal("Final benchmark output must cover and commit all 100 lamps");
    return values;
}
QJsonObject measure(const Scenario& scenario)
{
    QList<bool> layout(88, true);
    layout.append(QList<bool>(12, false));
    LampArrayOutput output;
    std::optional<LedFrame> lastFrame;
    QList<QByteArray> lastReports;
    qint64 operations = 0, frames = 0, packets = 0, bytes = 0;
    bool previousCommitted = true;
    const auto send = [&](QByteArray report) {
        if (previousCommitted)
            lastReports.clear();
        previousCommitted = report[0] == 5 ? report[1] == 1 : report[2] == 1;
        lastReports.append(report);
        ++packets;
        bytes += report.size();
        return QString();
    };
    const auto submit = [&](int i) {
        const auto frame = frameAt(scenario.input, i);
        if (lastFrame == frame)
            return; // Same cheap logical comparison as AsusdClient::pump().
        ++operations;
        const auto result = output.write(frame, layout, send);
        if (!result.error.isEmpty())
            qFatal("Unexpected output error in benchmark");
        frames += result.written;
        lastFrame = frame;
    };
    for (int i = 0; i < WarmupFrames; ++i)
        submit(i);
    operations = frames = packets = bytes = 0;
    const auto cpuStart = std::clock();
    QElapsedTimer elapsed;
    elapsed.start();
    for (int i = WarmupFrames; i < WarmupFrames + MeasuredFrames; ++i)
        submit(i);
    const double elapsedMs = elapsed.nsecsElapsed() / 1000000.0;
    const double cpuMs = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
    const auto values = decoded(lastReports);
    for (int lamp = 0; lamp < 100; ++lamp) {
        const bool off
            = ((scenario.input == Input::DisabledKeyboard || scenario.input == Input::Power) && lamp < 88)
            || (scenario.input == Input::DisabledStrip && lamp >= 88);
        const bool blue = (scenario.input == Input::Keyboard && lamp == 3)
            || (scenario.input == Input::Strip && lamp == 95);
        const auto expected = off ? QByteArray::fromHex("00000000")
            : blue                ? QByteArray::fromHex("0000ffff")
                                  : QByteArray::fromHex("ff0000ff");
        if (values.mid(4 * lamp, 4) != expected)
            qFatal("Final benchmark lamp output differs from the scenario expectation");
    }
    return { { "elapsedMs", elapsedMs }, { "cpuMs", cpuMs }, { "completedOperations", operations },
        { "committedFrames", frames }, { "outputReports", packets }, { "outputBytes", bytes } };
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("ASUS output preparation with an in-memory sink; no USB or D-Bus");
    parser.addHelpOption();
    parser.addOption({ "runs", "Repetitions per scenario (1-10)", "count", "3" });
    parser.process(app);
    bool valid = false;
    const int runs = parser.value("runs").toInt(&valid);
    if (!valid || runs < 1 || runs > 10)
        return 1;
    const QList<Scenario> scenarios {
        { "identical-logical-frame", Input::Identical },
        { "disabled-keyboard-static-strip", Input::DisabledKeyboard },
        { "disabled-strip-static-keyboard", Input::DisabledStrip },
        { "equivalent-color-spec-alpha", Input::ColorSpec },
        { "equivalent-range-multi-output", Input::PacketLayout },
        { "hardware-brightness-levels", Input::HardwareBrightness },
        { "changing-key", Input::Keyboard },
        { "changing-strip-element", Input::Strip },
        { "keyboard-power-change", Input::Power },
    };
    QJsonArray results;
    for (const auto& scenario : scenarios) {
        QJsonArray repetitions;
        for (int i = 0; i < runs; ++i)
            repetitions.append(measure(scenario));
        results.append(QJsonObject { { "name", scenario.name }, { "runs", repetitions } });
    }
    const QJsonObject result { { "scope", "ASUS LampArrayOutput with in-memory reports; no USB/D-Bus" },
        { "cpu", cpuModel() }, { "qt", qVersion() }, { "compiler", __VERSION__ }, { "buildType", BUILD_TYPE },
        { "component", QJsonObject::fromVariantMap(componentInfo(Component::Service)) }, { "lamps", 100 },
        { "warmupFrames", WarmupFrames }, { "measuredFrames", MeasuredFrames }, { "scenarios", results } };
    QTextStream(stdout) << QJsonDocument(result).toJson();
}
