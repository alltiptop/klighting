// SPDX-License-Identifier: GPL-3.0-or-later
#include "pulse_microphone_source.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using namespace Ambient;

class PipeWirePulseMicrophoneSourceTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_directory;
    QProcess m_pipewire;
    QProcess m_pulse;
    QProcessEnvironment m_environment;
    QString m_socket;
    QByteArray m_oldPulseServer;
    bool m_hadPulseServer = false;

    QByteArray run(const QString& program, const QStringList& arguments)
    {
        QProcess process;
        process.setProcessEnvironment(m_environment);
        process.start(program, arguments);
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished();
            return QByteArray("ERROR: command timed out: ") + process.readAllStandardError();
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return QByteArray("ERROR: ") + process.readAllStandardError();
        return process.readAllStandardOutput().trimmed();
    }

    QByteArray pactl(QStringList arguments)
    {
        arguments.prepend(QStringLiteral("--server=unix:") + m_socket);
        return run(QStringLiteral(KLIGHTING_PACTL_EXECUTABLE), arguments);
    }

    bool setDefault(const QString& name)
    {
        // No session manager is started: publish the effective default directly.
        const QByteArray result = run(QStringLiteral(KLIGHTING_PW_METADATA_EXECUTABLE),
            { "--remote=klighting-fixture", "--name=default", "0", "default.audio.source",
                QString::fromUtf8(
                    QJsonDocument(QJsonObject { { "name", name } }).toJson(QJsonDocument::Compact)),
                "Spa:String:JSON" });
        return !result.startsWith("ERROR:");
    }

    bool writeConfig(const QString& name, const QByteArray& contents)
    {
        QFile file(m_directory.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }

    void startServers()
    {
        m_pipewire.setProcessEnvironment(m_environment);
        m_pipewire.start(QStringLiteral(KLIGHTING_PIPEWIRE_EXECUTABLE),
            { "--config", m_directory.filePath(QStringLiteral("server.conf")) });
        QVERIFY(m_pipewire.waitForStarted());
        const QString nativeSocket = m_directory.filePath(QStringLiteral("klighting-fixture"));
        QTRY_VERIFY_WITH_TIMEOUT(
            QFile::exists(nativeSocket) || m_pipewire.state() == QProcess::NotRunning, 5000);
        QVERIFY2(QFile::exists(nativeSocket), m_pipewire.readAllStandardError().constData());
        m_pulse.setProcessEnvironment(m_environment);
        m_pulse.start(QStringLiteral(KLIGHTING_PIPEWIRE_PULSE_EXECUTABLE),
            { "--config", m_directory.filePath(QStringLiteral("pulse.conf")) });
        QVERIFY(m_pulse.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_socket) || m_pulse.state() == QProcess::NotRunning, 5000);
        QVERIFY2(QFile::exists(m_socket), m_pulse.readAllStandardError().constData());
        QTRY_COMPARE_WITH_TIMEOUT(pactl({ "get-default-source" }), QByteArray("fixture_a"), 5000);
    }

    static void stopProcess(QProcess& process)
    {
        if (process.state() == QProcess::NotRunning)
            return;
        process.terminate();
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished();
        }
    }

    void stopServers()
    {
        stopProcess(m_pulse);
        stopProcess(m_pipewire);
    }

    void verifyNoCaptureOrHardware()
    {
        QCOMPARE(pactl({ "--format=json", "list", "source-outputs" }), QByteArray("[]"));
        const QByteArray dump = run(
            QStringLiteral(KLIGHTING_PW_DUMP_EXECUTABLE), { "--remote=klighting-fixture", "--no-colors" });
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(dump, &error);
        QVERIFY2(error.error == QJsonParseError::NoError, dump.constData());
        QVERIFY(document.isArray());
        QSet<QString> nodes;
        for (const QJsonValue& value : document.array()) {
            const QJsonObject object = value.toObject();
            const QString type = object.value(QStringLiteral("type")).toString();
            QVERIFY(type != QStringLiteral("PipeWire:Interface:Link"));
            QVERIFY(type != QStringLiteral("PipeWire:Interface:Device"));
            if (type != QStringLiteral("PipeWire:Interface:Node"))
                continue;
            const QJsonObject props
                = object.value(QStringLiteral("info")).toObject().value(QStringLiteral("props")).toObject();
            QVERIFY(!props.value(QStringLiteral("media.class")).toString().startsWith("Stream/"));
            nodes.insert(props.value(QStringLiteral("node.name")).toString());
        }
        QCOMPARE(nodes, (QSet<QString> { "fixture_a", "fixture_b", "fixture_monitor" }));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        m_socket = m_directory.filePath(QStringLiteral("pulse-native"));
        // A clean environment and private config directory exclude host drop-ins,
        // sockets, hardware discovery, session managers and D-Bus activation.
        m_environment.insert("HOME", m_directory.path());
        m_environment.insert("XDG_RUNTIME_DIR", m_directory.path());
        m_environment.insert("XDG_CONFIG_HOME", m_directory.path());
        m_environment.insert("XDG_CONFIG_DIRS", m_directory.path());
        m_environment.insert("PIPEWIRE_CONFIG_DIR", m_directory.path());
        m_environment.insert("PIPEWIRE_RUNTIME_DIR", m_directory.path());
        m_environment.insert("PIPEWIRE_REMOTE", "klighting-fixture");
        m_environment.insert("PULSE_RUNTIME_PATH", m_directory.path());
        m_environment.insert("PULSE_SERVER", QStringLiteral("unix:") + m_socket);
        m_environment.insert("DBUS_SESSION_BUS_ADDRESS",
            QStringLiteral("unix:path=") + m_directory.filePath("no-session-bus"));
        m_environment.insert(
            "DBUS_SYSTEM_BUS_ADDRESS", QStringLiteral("unix:path=") + m_directory.filePath("no-system-bus"));
        m_environment.insert("LC_ALL", "C");
        m_hadPulseServer = qEnvironmentVariableIsSet("PULSE_SERVER");
        m_oldPulseServer = qgetenv("PULSE_SERVER");
        qputenv("PULSE_SERVER", (QStringLiteral("unix:") + m_socket).toUtf8());
        QVERIFY(writeConfig("client.conf", R"(
context.properties = { support.dbus = false }
context.modules = [
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-metadata }
]
)"));
        QVERIFY(writeConfig("server.conf", R"(
context.properties = {
    core.daemon = true
    core.name = klighting-fixture
    support.dbus = false
}
context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.* = support/libspa-support
}
context.modules = [
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-metadata }
    { name = libpipewire-module-spa-node-factory }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-access }
    { name = libpipewire-module-adapter }
    { name = libpipewire-module-link-factory }
]
context.objects = [
    { factory = metadata args = {
        metadata.name = default
        metadata.values = [
            { key = default.audio.source type = "Spa:String:JSON" value = { name = fixture_a } }
        ]
    } }
    { factory = adapter args = {
        factory.name = support.null-audio-sink
        node.name = fixture_a
        node.description = "Fixture microphone A"
        media.class = Audio/Source/Virtual
        audio.position = [ MONO ]
        adapter.auto-port-config = { mode = dsp monitor = true position = preserve }
    } }
    { factory = adapter args = {
        factory.name = support.null-audio-sink
        node.name = fixture_b
        node.description = "Fixture microphone B"
        media.class = Audio/Source/Virtual
        audio.position = [ MONO ]
        adapter.auto-port-config = { mode = dsp monitor = true position = preserve }
    } }
    { factory = adapter args = {
        factory.name = support.null-audio-sink
        node.name = fixture_monitor
        node.description = "Fixture sink"
        media.class = Audio/Sink
        audio.position = [ MONO ]
        adapter.auto-port-config = { mode = dsp monitor = true position = preserve }
    } }
]
)"));
        QVERIFY(writeConfig("pulse.conf",
            QStringLiteral(R"(
context.properties = { support.dbus = false remote.name = klighting-fixture }
context.modules = [
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-client-node }
    { name = libpipewire-module-metadata }
    { name = libpipewire-module-protocol-pulse }
]
pulse.properties = { server.address = [ "unix:%1" ] }
)")
                .arg(m_socket)
                .toUtf8()));
    }

    void observesCompatibilityServerAndRecovers()
    {
        PulseMicrophoneSource source;
        source.setEnabled(true);
        QTest::qWait(100);
        QVERIFY(!source.state().muteKnown);
        startServers();
        QTRY_VERIFY_WITH_TIMEOUT(source.state().muteKnown, 5000);
        QCOMPARE(source.state().sourceName, QStringLiteral("fixture_a"));
        QVERIFY(!source.state().muted);
        QVERIFY(pactl({ "info" }).contains("PipeWire"));
        verifyNoCaptureOrHardware();

        QVERIFY(pactl({ "set-source-mute", "fixture_a", "1" }).isEmpty());
        QTRY_VERIFY(source.state().muteKnown && source.state().muted);
        QVERIFY(pactl({ "set-source-volume", "fixture_a", "0%" }).isEmpty());
        QVERIFY(pactl({ "set-source-mute", "fixture_a", "0" }).isEmpty());
        QTRY_VERIFY(source.state().muteKnown && !source.state().muted);
        QVERIFY(pactl({ "set-source-mute", "fixture_a", "1" }).isEmpty());
        QTRY_VERIFY(source.state().muteKnown && source.state().muted);
        QVERIFY(setDefault("fixture_b"));
        QTRY_COMPARE(source.state().sourceName, QStringLiteral("fixture_b"));
        QVERIFY(source.state().muteKnown);
        QVERIFY(!source.state().muted);

        QVERIFY(setDefault("fixture_monitor"));
        QTRY_VERIFY(!source.state().muteKnown);
        QVERIFY(source.state().reason.contains(QStringLiteral("monitor")));
        QVERIFY(setDefault("fixture_b"));
        QTRY_VERIFY(source.state().muteKnown);
        QCOMPARE(source.state().sourceName, QStringLiteral("fixture_b"));
        QCOMPARE(pactl({ "get-default-source" }), QByteArray("fixture_b"));
        QVERIFY(pactl({ "get-source-mute", "fixture_a" }).endsWith("yes"));
        QVERIFY(pactl({ "get-source-mute", "fixture_b" }).endsWith("no"));
        verifyNoCaptureOrHardware();

        stopServers();
        QTRY_VERIFY(!source.state().available && !source.state().muteKnown);
        startServers();
        QTRY_VERIFY_WITH_TIMEOUT(source.state().muteKnown, 10000);
        QCOMPARE(source.state().sourceName, QStringLiteral("fixture_a"));
        QVERIFY(!source.state().muted);
        verifyNoCaptureOrHardware();
        source.setEnabled(false);
        QVERIFY(!source.state().available);
        QVERIFY(!source.state().muteKnown);
    }

    void cleanupTestCase()
    {
        stopServers();
        if (m_hadPulseServer)
            qputenv("PULSE_SERVER", m_oldPulseServer);
        else
            qunsetenv("PULSE_SERVER");
    }
};
QTEST_GUILESS_MAIN(PipeWirePulseMicrophoneSourceTest)
#include "test_pipewire_pulse_microphone_source.moc"
