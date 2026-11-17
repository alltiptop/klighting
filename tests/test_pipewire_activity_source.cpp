// SPDX-License-Identifier: GPL-3.0-or-later
#include "sources/media/pipewire_activity_source.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

using namespace Ambient;
class PipeWireActivitySourceTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_directory;
    QProcess m_server;
    QProcessEnvironment m_environment;
    QMap<QByteArray, QByteArray> m_original;

    bool write(const QString& name, const QByteArray& contents)
    {
        QFile file(m_directory.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }
    QByteArray run(const QString& program, const QStringList& args)
    {
        QProcess process;
        process.setProcessEnvironment(m_environment);
        process.start(program, args);
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished();
            return "ERROR: command timed out";
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode())
            return "ERROR: " + process.readAllStandardError();
        return process.readAllStandardOutput().trimmed();
    }
    void startServer()
    {
        m_server.setProcessEnvironment(m_environment);
        m_server.start(QStringLiteral(KLIGHTING_PIPEWIRE_EXECUTABLE),
            { "--config", m_directory.filePath("server.conf") });
        QVERIFY(m_server.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_directory.filePath("activity-fixture"))
                || m_server.state() == QProcess::NotRunning,
            5000);
        QVERIFY2(m_server.state() != QProcess::NotRunning, m_server.readAllStandardError().constData());
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
    void link(const QString& source, bool active)
    {
        QStringList args;
        if (!active)
            args << "--disconnect";
        args << source + ":monitor_MONO" << "fixture_consumer:playback_MONO";
        const auto result = run(QStringLiteral(KLIGHTING_PW_LINK_EXECUTABLE), args);
        QVERIFY2(!result.startsWith("ERROR:"), result.constData());
    }
    void verifyOnlyFixtureNodes()
    {
        const auto dump = run(QStringLiteral(KLIGHTING_PW_DUMP_EXECUTABLE), { "--no-colors" });
        const auto document = QJsonDocument::fromJson(dump);
        QVERIFY2(document.isArray(), dump.constData());
        QSet<QString> nodes;
        for (const auto& value : document.array()) {
            const auto object = value.toObject();
            QVERIFY(object["type"] != "PipeWire:Interface:Device");
            if (object["type"] == "PipeWire:Interface:Node")
                nodes.insert(object["info"].toObject()["props"].toObject()["node.name"].toString());
        }
        QCOMPARE(nodes,
            (QSet<QString> { "fixture_driver", "fixture_microphone", "fixture_camera",
                "kwin-screencast-fixture", "fixture_consumer" }));
    }
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        // Private configuration has no hardware discovery, session manager or D-Bus modules.
        const QMap<QByteArray, QByteArray> environment {
            { "PIPEWIRE_CONFIG_DIR", m_directory.path().toUtf8() },
            { "PIPEWIRE_RUNTIME_DIR", m_directory.path().toUtf8() },
            { "PIPEWIRE_REMOTE", "activity-fixture" },
            { "XDG_CONFIG_HOME", m_directory.path().toUtf8() },
            { "XDG_CONFIG_DIRS", m_directory.path().toUtf8() },
            { "XDG_RUNTIME_DIR", m_directory.path().toUtf8() },
        };
        for (auto it = environment.cbegin(); it != environment.cend(); ++it) {
            m_original.insert(it.key(), qgetenv(it.key()));
            qputenv(it.key(), it.value());
            m_environment.insert(QString::fromUtf8(it.key()), QString::fromUtf8(it.value()));
        }
        QVERIFY(write("client.conf", R"(
context.properties = { support.dbus = false }
context.modules = [ { name = libpipewire-module-protocol-native } ]
)"));
        QByteArray config = R"(
context.properties = { core.daemon = true core.name = activity-fixture support.dbus = false }
context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.* = support/libspa-support
}
context.modules = [
    { name = libpipewire-module-protocol-native }
    { name = libpipewire-module-spa-node-factory }
    { name = libpipewire-module-access }
    { name = libpipewire-module-adapter }
    { name = libpipewire-module-link-factory }
]
context.objects = [
    { factory = spa-node-factory args = {
        factory.name = support.node.driver node.name = fixture_driver
        node.group = pipewire.dummy priority.driver = 200000
    } }
)";
        // Synthetic audio nodes also exercise video-source metadata; no camera frames are produced.
        for (const auto& properties : { "node.name = fixture_microphone media.class = Audio/Source",
                 "node.name = fixture_camera media.class = Video/Source media.role = Camera",
                 "node.name = kwin-screencast-fixture media.class = Video/Source",
                 "node.name = fixture_consumer media.class = Audio/Sink" }) {
            config += "{ factory = adapter args = { factory.name = support.null-audio-sink ";
            config += properties;
            config += R"(
                audio.position = [ MONO ]
                adapter.auto-port-config = { mode = dsp monitor = true position = preserve }
            } }
)";
        }
        config += "]\n";
        QVERIFY(write("server.conf", config));
    }
    void observesActivityAndRecoversWithoutCapturing()
    {
        PipeWireActivitySource source;
        QVariantMap values;
        connect(&source, &MediaActivitySource::changed, this, [&](const auto& next) { values = next; });
        source.setEnabled(true);
        QVERIFY(values.isEmpty());
        startServer();
        QTRY_COMPARE_WITH_TIMEOUT(values.size(), 3, 10000);
        QCOMPARE(values["microphone-active"], QVariant(false));
        QCOMPARE(values["camera-active"], QVariant(false));
        QCOMPARE(values["screen-capture"], QVariant(false));
        verifyOnlyFixtureNodes();
        link("fixture_microphone", true);
        QTRY_COMPARE(values["microphone-active"], QVariant(true));
        QCOMPARE(values["camera-active"], QVariant(false));
        link("fixture_camera", true);
        QTRY_COMPARE(values["camera-active"], QVariant(true));
        QCOMPARE(values["screen-capture"], QVariant(false));
        link("kwin-screencast-fixture", true);
        QTRY_COMPARE(values["screen-capture"], QVariant(true));
        link("fixture_camera", false);
        QTRY_COMPARE(values["camera-active"], QVariant(false));
        QCOMPARE(values["microphone-active"], QVariant(true));
        QCOMPARE(values["screen-capture"], QVariant(true));
        verifyOnlyFixtureNodes();
        stopServer();
        QTRY_VERIFY(values.isEmpty());
        startServer();
        QTRY_COMPARE_WITH_TIMEOUT(values.size(), 3, 10000);
        QCOMPARE(values["screen-capture"], QVariant(false));
        link("kwin-screencast-fixture", true);
        QTRY_COMPARE(values["screen-capture"], QVariant(true));
        source.setEnabled(false);
        QVERIFY(values.isEmpty());
        source.setEnabled(true);
        QTRY_COMPARE(values["screen-capture"], QVariant(true));
        source.setEnabled(false);
        QTest::qWait(100);
        QVERIFY(values.isEmpty());
        verifyOnlyFixtureNodes();
        bool disabledFromCallback = false;
        connect(&source, &MediaActivitySource::changed, this, [&](const auto& next) {
            if (next.value("screen-capture").toBool()) {
                disabledFromCallback = true;
                source.setEnabled(false);
            }
        });
        source.setEnabled(true);
        QTRY_VERIFY(disabledFromCallback);
        QTest::qWait(100);
        QVERIFY(values.isEmpty());
    }
    void cleanupTestCase()
    {
        stopServer();
        for (auto it = m_original.cbegin(); it != m_original.cend(); ++it) {
            if (it.value().isNull())
                qunsetenv(it.key());
            else
                qputenv(it.key(), it.value());
        }
    }
};
QTEST_GUILESS_MAIN(PipeWireActivitySourceTest)
#include "test_pipewire_activity_source.moc"
