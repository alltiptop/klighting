// SPDX-License-Identifier: GPL-3.0-or-later
#include "pulse_microphone_source.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

using namespace Ambient;

class PulseMicrophoneSourceTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_directory;
    QProcess m_server;
    QString m_socket;
    QProcessEnvironment m_environment;

    QByteArray command(QStringList arguments)
    {
        QProcess process;
        process.setProcessEnvironment(m_environment);
        arguments.prepend(QStringLiteral("--server=unix:") + m_socket);
        process.start(QStringLiteral(KLIGHTING_PACTL_EXECUTABLE), arguments);
        if (!process.waitForFinished(5000) || process.exitCode() != 0)
            return QByteArray("ERROR: ") + process.readAllStandardError();
        return process.readAllStandardOutput().trimmed();
    }
    void startServer()
    {
        m_server.setProcessEnvironment(m_environment);
        m_server.start(QStringLiteral(KLIGHTING_PULSEAUDIO_EXECUTABLE),
            { QStringLiteral("--daemonize=no"), QStringLiteral("--use-pid-file=no"),
                QStringLiteral("--exit-idle-time=-1"), QStringLiteral("--disallow-exit=yes"),
                QStringLiteral("--disable-shm=yes"), QStringLiteral("--log-target=stderr"),
                QStringLiteral("-n"), QStringLiteral("-F"),
                m_directory.filePath(QStringLiteral("server.pa")) });
        QVERIFY(m_server.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_socket) || m_server.state() == QProcess::NotRunning, 5000);
        QVERIFY2(QFile::exists(m_socket), m_server.readAllStandardError().constData());
    }
    void stopServer()
    {
        if (m_server.state() == QProcess::NotRunning)
            return;
        m_server.terminate();
        if (!m_server.waitForFinished(5000)) {
            m_server.kill();
            m_server.waitForFinished();
        }
    }
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        m_socket = m_directory.filePath(QStringLiteral("native"));
        QVERIFY(QDir().mkpath(m_directory.filePath(QStringLiteral("runtime"))));
        QVERIFY(QDir().mkpath(m_directory.filePath(QStringLiteral("config"))));
        m_environment = QProcessEnvironment::systemEnvironment();
        m_environment.insert(
            QStringLiteral("PULSE_RUNTIME_PATH"), m_directory.filePath(QStringLiteral("runtime")));
        m_environment.insert(
            QStringLiteral("XDG_CONFIG_HOME"), m_directory.filePath(QStringLiteral("config")));
        m_environment.insert(QStringLiteral("PULSE_SERVER"), QStringLiteral("unix:") + m_socket);
        m_environment.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
            QStringLiteral("unix:path=") + m_directory.filePath(QStringLiteral("no-session-bus")));
        m_environment.insert(QStringLiteral("DBUS_SYSTEM_BUS_ADDRESS"),
            QStringLiteral("unix:path=") + m_directory.filePath(QStringLiteral("no-system-bus")));
        // Every observer and control connection is restricted to this private socket.
        qputenv("PULSE_SERVER", (QStringLiteral("unix:") + m_socket).toUtf8());
        QFile config(m_directory.filePath(QStringLiteral("server.pa")));
        QVERIFY(config.open(QIODevice::WriteOnly));
        const QString contents = QStringLiteral(
            "load-module module-native-protocol-unix socket=%1 auth-anonymous=1\n"
            "load-module module-pipe-source source_name=fixture_a file=%2 channels=1 rate=48000\n"
            "load-module module-pipe-source source_name=fixture_b file=%3 channels=1 rate=48000\n"
            "load-module module-null-sink sink_name=fixture_monitor\n"
            "set-default-source fixture_a\n")
                                     .arg(m_socket, m_directory.filePath(QStringLiteral("a.fifo")),
                                         m_directory.filePath(QStringLiteral("b.fifo")));
        QCOMPARE(config.write(contents.toUtf8()), contents.toUtf8().size());
    }
    void observesOnlyMuteAndRecovers()
    {
        PulseMicrophoneSource source;
        source.setEnabled(true);
        QTest::qWait(100);
        QVERIFY(!source.state().muteKnown);
        startServer();
        QTRY_VERIFY_WITH_TIMEOUT(source.state().muteKnown, 5000);
        QCOMPARE(source.state().sourceName, QStringLiteral("fixture_a"));
        QVERIFY(!source.state().muted);
        QVERIFY(command({ "set-source-mute", "fixture_a", "1" }).isEmpty());
        QTRY_VERIFY(source.state().muteKnown && source.state().muted);
        QVERIFY(command({ "set-source-volume", "fixture_a", "0%" }).isEmpty());
        QVERIFY(command({ "suspend-source", "fixture_a", "1" }).isEmpty());
        QVERIFY(command({ "set-source-mute", "fixture_a", "0" }).isEmpty());
        QTRY_VERIFY(source.state().muteKnown && !source.state().muted);
        QVERIFY(command({ "set-source-mute", "fixture_a", "1" }).isEmpty());
        QTRY_VERIFY(source.state().muted);
        QVERIFY(command({ "set-default-source", "fixture_b" }).isEmpty());
        QTRY_COMPARE(source.state().sourceName, QStringLiteral("fixture_b"));
        QVERIFY(source.state().muteKnown);
        QVERIFY(!source.state().muted);
        QVERIFY(command({ "set-default-source", "fixture_monitor.monitor" }).isEmpty());
        QTRY_VERIFY(!source.state().muteKnown);
        QTRY_VERIFY(source.state().reason.contains(QStringLiteral("monitor")));
        QVERIFY(command({ "set-default-source", "fixture_b" }).isEmpty());
        QTRY_VERIFY(source.state().muteKnown);
        const QByteArray temporary = command({ "load-module", "module-pipe-source", "source_name=temporary",
            QStringLiteral("file=") + m_directory.filePath(QStringLiteral("temporary.fifo")) });
        QVERIFY(!temporary.startsWith("ERROR"));
        QVERIFY(command({ "set-default-source", "temporary" }).isEmpty());
        QTRY_COMPARE(source.state().sourceName, QStringLiteral("temporary"));
        QVERIFY(command({ "unload-module", QString::fromUtf8(temporary) }).isEmpty());
        QTRY_VERIFY(source.state().muteKnown && source.state().sourceName != QStringLiteral("temporary"));
        QCOMPARE(command({ "--format=json", "list", "source-outputs" }), QByteArray("[]"));
        QVERIFY(command({ "get-source-mute", "fixture_a" }).endsWith("yes"));
        QVERIFY(command({ "get-source-mute", "fixture_b" }).endsWith("no"));
        stopServer();
        QTRY_VERIFY(!source.state().available);
        QVERIFY(!source.state().muteKnown);
        startServer();
        QTRY_VERIFY_WITH_TIMEOUT(source.state().muteKnown, 5000);
        QCOMPARE(source.state().sourceName, QStringLiteral("fixture_a"));
        QVERIFY(!source.state().muted);
        source.setEnabled(false);
        QVERIFY(!source.state().available);
        QVERIFY(!source.state().muteKnown);
        source.setEnabled(true);
        source.setEnabled(false);
        QTest::qWait(100);
        QVERIFY(!source.state().muteKnown);
    }
    void cleanupTestCase() { stopServer(); }
};
QTEST_GUILESS_MAIN(PulseMicrophoneSourceTest)
#include "test_pulse_microphone_source.moc"
