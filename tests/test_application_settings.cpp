// SPDX-License-Identifier: GPL-3.0-or-later
#include "application_settings.h"
#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace Ambient;
namespace
{
QByteArray bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString& path, const QByteArray& content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}
bool legacy(const QVariantMap& values)
{
    KConfig config(QStringLiteral("kwinrc"), KConfig::NoGlobals);
    KConfigGroup group(&config, "Effect-ambientled");
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        group.writeEntry(it.key(), it.value());
    KConfigGroup(&config, "Plugins").writeEntry("ambientledEnabled", true);
    return config.sync();
}
}
class ApplicationSettingsTest : public QObject
{
    Q_OBJECT
    QString error;
private Q_SLOTS:
    void init()
    {
        const auto root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QVERIFY(QDir(root).removeRecursively());
        QVERIFY(QDir().mkpath(root));
        error.clear();
    }
    void readOnlyFallbackAndOneTimePublication()
    {
        QVERIFY(legacy({ { "ScreenName", "Display\\Name" }, { "Region", "bottom" }, { "BottomPercent", 42 },
            { "Brightness", 0.4 }, { "Saturation", 1.5 } }));
        const auto oldPath
            = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/kwinrc";
        const auto oldBytes = bytes(oldPath);
        const auto read = loadApplicationSettings(SettingsAccess::ReadOnly, &error);
        QVERIFY2(read, qPrintable(error));
        QCOMPARE(read->capture.value("screen").toString(), "Display\\Name");
        QCOMPARE(read->capture.value("bottomOnly").toBool(), true);
        QCOMPARE(read->capture.value("bottomPercent").toInt(), 42);
        QCOMPARE(read->brightness, 0.4);
        QCOMPARE(read->saturation, 1.5);
        QVERIFY(!QFile::exists(applicationSettingsPath()));
        const auto migrated = loadApplicationSettings(SettingsAccess::Migrate, &error);
        QVERIFY2(migrated, qPrintable(error));
        QCOMPARE(migrated->capture, read->capture);
        QCOMPARE(bytes(oldPath), oldBytes);
        const auto published = bytes(applicationSettingsPath());
        QVERIFY(QJsonDocument::fromJson(published).object().value("legacyImported").toBool());
        QVERIFY(legacy({ { "ScreenName", "Changed later" }, { "Brightness", 0.9 } }));
        QLockFile lock(applicationSettingsPath() + ".lock");
        QVERIFY(lock.tryLock()); // A completed migration needs neither a writer lock nor a rewrite.
        const auto repeated = loadApplicationSettings(SettingsAccess::Migrate, &error);
        QVERIFY2(repeated, qPrintable(error));
        QCOMPARE(repeated->capture, read->capture);
        QCOMPARE(repeated->brightness, 0.4);
        QCOMPARE(bytes(applicationSettingsPath()), published);
    }
    void explicitNewValuesAndUnknownFieldsSurviveImport()
    {
        QVERIFY(legacy({ { "ScreenName", "legacy" }, { "Brightness", 0.8 }, { "Saturation", 1.5 },
            { "Region", "bottom" }, { "BottomPercent", 30 } }));
        QVERIFY(write(applicationSettingsPath(), R"({"version":1,"future":{"preserve":true},
            "values":{"ScreenName":"","Brightness":0,"Region":"full","futureValue":"kept"}})"));
        const auto settings = loadApplicationSettings(SettingsAccess::Migrate, &error);
        QVERIFY2(settings, qPrintable(error));
        QVERIFY(settings->capture.value("screen").toString().isEmpty());
        QVERIFY(!settings->capture.value("bottomOnly").toBool());
        QCOMPARE(settings->capture.value("bottomPercent").toInt(), 30);
        QCOMPARE(settings->brightness, 0.0);
        QCOMPARE(settings->saturation, 1.5);
        const auto root = QJsonDocument::fromJson(bytes(applicationSettingsPath())).object();
        QVERIFY(root.value("future").toObject().value("preserve").toBool());
        QCOMPARE(root.value("values").toObject().value("futureValue").toString(), "kept");
    }
    void lockAndDirectoryFailuresLeaveImportRetryable()
    {
        QVERIFY(legacy({ { "ScreenName", "legacy" } }));
        QVERIFY(QDir().mkpath(QFileInfo(applicationSettingsPath()).absolutePath()));
        {
            QLockFile lock(applicationSettingsPath() + ".lock");
            QVERIFY(lock.tryLock());
            QVERIFY(!loadApplicationSettings(SettingsAccess::Migrate, &error));
            QVERIFY(error.contains("busy"));
            QVERIFY(!QFile::exists(applicationSettingsPath()));
            QVERIFY(loadApplicationSettings(SettingsAccess::ReadOnly, &error));
        }
        const auto parent = QFileInfo(applicationSettingsPath()).absolutePath();
        QVERIFY(QDir(parent).removeRecursively());
        QVERIFY(write(parent, "file obstructing the directory"));
        QVERIFY(!loadApplicationSettings(SettingsAccess::Migrate, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(bytes(parent), QByteArray("file obstructing the directory"));
        QVERIFY(QFile::remove(parent));
        const auto retry = loadApplicationSettings(SettingsAccess::Migrate, &error);
        QVERIFY2(retry, qPrintable(error));
        QCOMPARE(retry->capture.value("screen").toString(), "legacy");
    }
    void invalidDocumentsAreNotReplaced_data()
    {
        QTest::addColumn<QByteArray>("document");
        QTest::newRow("syntax") << QByteArray("{");
        QTest::newRow("future-version") << QByteArray(R"({"version":2,"values":{}})");
        QTest::newRow("wrong-values-type") << QByteArray(R"({"version":1,"values":[]})");
        QTest::newRow("wrong-setting-type") << QByteArray(R"({"version":1,"values":{"Brightness":"bad"}})");
        QTest::newRow("wrong-marker") << QByteArray(R"({"version":1,"legacyImported":"true","values":{}})");
        QTest::newRow("oversized") << QByteArray(65537, ' ');
    }
    void invalidDocumentsAreNotReplaced()
    {
        QFETCH(QByteArray, document);
        QVERIFY(write(applicationSettingsPath(), document));
        QVERIFY(!loadApplicationSettings(SettingsAccess::Migrate, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(bytes(applicationSettingsPath()), document);
    }
    void driverRecordsRemainIndependentAndRetryAfterPartialMigration()
    {
        const QJsonObject defaults { { "DevicePath", "" } };
        QVERIFY(legacy({ { "ScreenName", "legacy-screen" }, { "DevicePath", "/legacy/device" } }));
        QVERIFY(loadApplicationSettings(SettingsAccess::Migrate, &error));
        const auto application = bytes(applicationSettingsPath());
        QVERIFY(write(driverSettingsPath("first"), R"({"version":1,"values":{"DevicePath":"/new/device"}})"));
        const auto first = loadDriverSettings("first", defaults, SettingsAccess::Migrate, &error);
        QVERIFY2(first, qPrintable(error));
        QCOMPARE(first->value("DevicePath").toString(), "/new/device");
        const auto firstBytes = bytes(driverSettingsPath("first"));
        QLockFile lock(driverSettingsPath("second") + ".lock");
        QVERIFY(lock.tryLock());
        QVERIFY(!loadDriverSettings("second", defaults, SettingsAccess::Migrate, &error));
        const auto fallback = loadDriverSettings("second", defaults, SettingsAccess::ReadOnly, &error);
        QVERIFY2(fallback, qPrintable(error));
        QCOMPARE(fallback->value("DevicePath").toString(), "/legacy/device");
        lock.unlock();
        QVERIFY(loadApplicationSettings(SettingsAccess::Migrate, &error));
        QVERIFY(loadDriverSettings("first", defaults, SettingsAccess::Migrate, &error));
        QVERIFY(loadDriverSettings("second", defaults, SettingsAccess::Migrate, &error));
        QCOMPARE(bytes(applicationSettingsPath()), application);
        QCOMPARE(bytes(driverSettingsPath("first")), firstBytes);
        QVERIFY(driverSettingsPath("../outside").isEmpty());
        QVERIFY(!loadDriverSettings("../outside", defaults, SettingsAccess::Migrate, &error));
        QVERIFY(!loadDriverSettings("first", { { "bad/key", "" } }, SettingsAccess::Migrate, &error));
    }
    void migratedValuesRetainTheirEffectiveBounds()
    {
        QVERIFY(legacy({ { "BottomPercent", 500 }, { "Brightness", -5.0 }, { "Saturation", 10.0 } }));
        const auto settings = loadApplicationSettings(SettingsAccess::Migrate, &error);
        QVERIFY2(settings, qPrintable(error));
        QCOMPARE(settings->capture.value("bottomPercent").toInt(), 100);
        QCOMPARE(settings->brightness, 0.0);
        QCOMPARE(settings->saturation, 2.0);
    }
    void completedRecordsUseDefaultsForMissingValuesWithoutImportingAgain()
    {
        QVERIFY(legacy({ { "ScreenName", "legacy" }, { "Brightness", 0.2 } }));
        const QByteArray record = R"({"version":1,"legacyImported":true,"values":{}})";
        QVERIFY(write(applicationSettingsPath(), record));
        const auto settings = loadApplicationSettings(SettingsAccess::Migrate, &error);
        QVERIFY2(settings, qPrintable(error));
        QVERIFY(settings->capture.value("screen").toString().isEmpty());
        QCOMPARE(settings->brightness, 1.0);
        QCOMPARE(bytes(applicationSettingsPath()), record);
    }
};
int main(int argc, char** argv)
{
    QTemporaryDir directory;
    if (!directory.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", directory.path().toUtf8());
    QCoreApplication app(argc, argv);
    ApplicationSettingsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_application_settings.moc"
