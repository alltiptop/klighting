// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_driver.h"
#include <QFile>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QTranslator>
#include <QXmlStreamReader>
#include <QtTest>
struct CatalogEntry {
    QString context, source, translation;
};
static QList<CatalogEntry> catalog(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    QXmlStreamReader xml(&file);
    QList<CatalogEntry> entries;
    QString context;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QLatin1String("name"))
            context = xml.readElementText();
        else if (xml.name() == QLatin1String("message")) {
            CatalogEntry entry { context, { }, { } };
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("source"))
                    entry.source = xml.readElementText();
                else if (xml.name() == QLatin1String("translation")) {
                    if (xml.attributes().hasAttribute("type"))
                        return { };
                    entry.translation = xml.readElementText();
                } else
                    xml.skipCurrentElement();
            }
            entries.append(entry);
        }
    }
    return xml.hasError() ? QList<CatalogEntry>() : entries;
}
static QStringList placeholders(const QString& text)
{
    QStringList result;
    auto matches = QRegularExpression("%(?:L?\\d+|n)").globalMatch(text);
    while (matches.hasNext())
        result << matches.next().captured();
    result.sort();
    return result;
}
class TranslationsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void driverOwnsTranslations()
    {
        // The driver catalog becomes available only when its module loads.
        QVERIFY(!QFile::exists(":/ambient-led/drivers/asus-g615lw/i18n/asus-g615lw_ru.qm"));
        const auto original = QLocale();
        QLocale::setDefault(QLocale("ru_RU"));
        QPluginLoader plugin(ASUS_PLUGIN_PATH);
        auto* driver = qobject_cast<Ambient::DeviceDriver*>(plugin.instance());
        QVERIFY2(driver, qPrintable(plugin.errorString()));
        const auto devices = driver->discover({ QStringLiteral("G615LW") });
        QCOMPARE(devices.size(), 1);
        for (const auto* language : { "ru", "fr", "it", "es", "uk", "pl", "de", "zh_CN", "ar", "he" }) {
#ifdef HOST_TRANSLATIONS_DIR
            QTranslator host;
            QVERIFY(host.load(QString(HOST_TRANSLATIONS_DIR) + "/ambient-led_" + language + ".qm"));
            QVERIFY(host.translate("Ambient::AsusG615lwLabels", "Lightbar").isEmpty());
            const auto hostEntries
                = catalog(QString(HOST_SOURCE_DIR) + "/translations/ambient-led_" + language + ".ts");
            QVERIFY(!hostEntries.isEmpty());
            for (const auto& entry : hostEntries)
                QVERIFY(!entry.context.contains("Asus"));
#endif
            QTranslator translator;
            QVERIFY(translator.load(
                QString(":/ambient-led/drivers/asus-g615lw/i18n/asus-g615lw_%1.qm").arg(language)));
            const auto entries
                = catalog(QString(DRIVER_SOURCE_DIR) + "/translations/asus-g615lw_" + language + ".ts");
            QVERIFY(entries.size() >= 11);
            for (const auto& entry : entries) {
                QVERIFY(entry.context.startsWith("Ambient::Asus"));
                QVERIFY(!entry.translation.isEmpty());
                QCOMPARE(placeholders(entry.translation), placeholders(entry.source));
                QCOMPARE(
                    translator.translate(entry.context.toUtf8(), entry.source.toUtf8()), entry.translation);
                if (QString(language) == "ru" && entry.source == "Lightbar")
                    QCOMPARE(devices[0].description.strips[0].name, entry.translation);
            }
            QVERIFY(translator.translate("AmbientLed.main", "Lighting").isEmpty());
        }
        QLocale::setDefault(original);
    }
};
QTEST_GUILESS_MAIN(TranslationsTest)
#include "test_translations.moc"
