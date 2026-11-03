// SPDX-License-Identifier: GPL-3.0-or-later
#include <QPluginLoader>
#include <QProcess>
#include <QtTest>

class PluginLoadingTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void resolvesEverySymbol_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("ambient-capture") << QString(EFFECT_PLUGIN_PATH);
        QTest::newRow("desktop-integration") << QString(INTEGRATION_PLUGIN_PATH);
    }
    void resolvesEverySymbol()
    {
        QFETCH(QString, path);
        QPluginLoader loader(path);
        // Resolve lazy function references too. Do not instantiate the plugin:
        // that requires a compositor and would connect to the user's session.
        loader.setLoadHints(QLibrary::ResolveAllSymbolsHint);
        QVERIFY2(loader.load(), qPrintable(loader.errorString()));
        QVERIFY(loader.isLoaded());
    }
    void eagerBindingRejectsMissingSymbolsBeforeAnyCall()
    {
        QPluginLoader loader(UNRESOLVED_PLUGIN_PATH);
        // No ResolveAllSymbolsHint: the ELF must request eager binding itself,
        // including when loaded by a compositor we do not control.
        QVERIFY(!loader.load());
        QVERIFY(loader.errorString().contains("klighting_missing_dependency"));
    }
    void disposableChecks_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("failure");
        QTest::newRow("capture") << QString(EFFECT_PLUGIN_PATH) << QString();
        QTest::newRow("desktop") << QString(INTEGRATION_PLUGIN_PATH) << QString();
        QTest::newRow("never-instantiate") << QString(CONSTRUCTOR_PLUGIN_PATH) << QString();
        QTest::newRow("missing-symbol")
            << QString(UNRESOLVED_PLUGIN_PATH) << QString("klighting_missing_dependency");
        QTest::newRow("constructor-crash") << QString(CRASH_PLUGIN_PATH) << QString("crashed");
        QTest::newRow("constructor-hang") << QString(HANG_PLUGIN_PATH) << QString("timed out");
        QTest::newRow("missing-file")
            << QString("/missing/klighting-test-plugin.so") << QString("plugin check failed");
    }
    void disposableChecks()
    {
        QFETCH(QString, path);
        QFETCH(QString, failure);
        QProcess checker;
        checker.setProcessChannelMode(QProcess::MergedChannels);
        checker.start(QStringLiteral(PLUGIN_CHECK_PATH), { path });
        QVERIFY(checker.waitForFinished(8000));
        QCOMPARE(checker.exitStatus(), QProcess::NormalExit);
        const auto output = checker.readAll();
        QCOMPARE(checker.exitCode(), failure.isEmpty() ? 0 : 1);
        QVERIFY2(output.contains(failure.isEmpty() ? ": OK" : failure.toUtf8()), output.constData());
    }
};
QTEST_GUILESS_MAIN(PluginLoadingTest)
#include "test_plugin_loading.moc"
