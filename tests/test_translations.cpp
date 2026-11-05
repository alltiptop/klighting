// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_driver.h"
#include "translations.h"
#include <KCMultiDialog>
#include <KLocalizedString>
#include <KPluginMetaData>
#include <KQuickConfigModule>
#include <QApplication>
#include <QFile>
#include <QPluginLoader>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QTemporaryDir>
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
static QString expected(const QString& language, const QString& context, const QString& source)
{
    for (const auto& entry : catalog(QString(SOURCE_DIR) + "/translations/ambient-led_" + language + ".ts"))
        if (entry.context == context && entry.source == source)
            return entry.translation;
    return { };
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
    void catalogs_data()
    {
        QTest::addColumn<QString>("language");
        for (const auto* language : { "ru", "fr", "it", "es", "uk", "pl", "de", "zh_CN", "ar", "he" })
            QTest::newRow(language) << QString(language);
    }
    void catalogs()
    {
        QFETCH(QString, language);
        Ambient::Translations translator({ language });
        QVERIFY(!translator.isEmpty());
        const auto entries = catalog(QString(SOURCE_DIR) + "/translations/ambient-led_" + language + ".ts");
        QVERIFY(entries.size() > 250);
        for (const auto& entry : entries) {
            QVERIFY(!entry.context.startsWith("Ambient::TestDriver"));
            QVERIFY2(!entry.translation.isEmpty(), qPrintable(entry.source));
            QCOMPARE(placeholders(entry.translation), placeholders(entry.source));
            QCOMPARE(translator.translate(entry.context.toUtf8(), entry.source.toUtf8()), entry.translation);
        }
        // Installing the KCM catalog must not translate unrelated modules.
        QVERIFY(translator.translate("main", "Lighting").isEmpty());
        QVERIFY(translator.translate("Ambient::TestDriver", "Lightbar").isEmpty());
        // Namespaced QML contexts are also used at runtime, not just by lupdate.
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(SOURCE_DIR) + "/ui/HexColor.qml"));
        std::unique_ptr<QObject> item(component.createWithInitialProperties({ { "label", "RGB" } }));
        QVERIFY2(item, qPrintable(component.errorString()));
        auto* button = item->findChild<QObject*>("ColorSwatch");
        QVERIFY(button);
        QQmlExpression expression(QQmlEngine::contextForObject(button), button, "Accessible.name");
        QCOMPARE(expression.evaluate().toString(),
            expected(language, "AmbientLed.HexColor", "Choose %1").arg("RGB"));
        QQmlComponent microphone(
            &engine, QUrl::fromLocalFile(QString(SOURCE_DIR) + "/ui/MicrophoneForm.qml"));
        const auto controller = [](const QVariantMap& state) {
            return QVariantMap { { "service", QVariantMap { { "microphoneStatus", state } } } };
        };
        std::unique_ptr<QObject> form(microphone.createWithInitialProperties(
            { { "controller", controller({ }) }, { "options", QVariantMap { { "type", "microphone" } } } }));
        QVERIFY2(form, qPrintable(microphone.errorString()));
        auto* status = form->findChild<QObject*>("MicrophoneStatus");
        auto* reason = form->findChild<QObject*>("MicrophoneReason");
        QVERIFY(status && reason);
        const QList<QPair<QVariantMap, QString>> states { { { }, "Microphone status unavailable" },
            { { { "available", true } }, "Microphone mute state unknown" },
            { { { "available", true }, { "muteKnown", true }, { "muted", true } }, "Microphone muted" },
            { { { "available", true }, { "muteKnown", true }, { "muted", false } }, "Microphone unmuted" } };
        for (const auto& [state, source] : states) {
            form->setProperty("controller", controller(state));
            QCOMPARE(
                status->property("text").toString(), expected(language, "AmbientLed.MicrophoneForm", source));
        }
        form->setProperty("controller", controller({ { "reason", "<b>Synthetic unavailable reason</b>" } }));
        QCOMPARE(reason->property("text").toString(), QString("<b>Synthetic unavailable reason</b>"));
        QCOMPARE(reason->property("textFormat").toInt(), int(Qt::PlainText));
    }
    void preferenceFallbackAndLifetime()
    {
        const auto original = KLocalizedString::languages();
        KLocalizedString::setLanguages({ "fr_CA", "de" });
        {
            Ambient::Translations first;
            QCOMPARE(first.language(), QString("fr"));
            {
                Ambient::Translations second({ "fr" });
                QCOMPARE(QCoreApplication::translate("AmbientLed.main", "Lighting"),
                    expected("fr", "AmbientLed.main", "Lighting"));
            }
            QCOMPARE(QCoreApplication::translate("AmbientLed.main", "Lighting"),
                expected("fr", "AmbientLed.main", "Lighting"));
        }
        QCOMPARE(QCoreApplication::translate("AmbientLed.main", "Lighting"), QString("Lighting"));
        {
            Ambient::Translations english({ "en_US", "ru" });
            QVERIFY(english.isEmpty());
        }
        {
            Ambient::Translations unsupported({ "ja_JP" });
            QVERIFY(unsupported.isEmpty());
        }
        {
            Ambient::Translations chinese({ "zh_Hans_CN" });
            QCOMPARE(chinese.language(), QString("zh_CN"));
        }
        KLocalizedString::setLanguages(original);
    }
    void nativeModule_data() { catalogs_data(); }
    void nativeModule()
    {
        QFETCH(QString, language);
        const auto originalLanguages = KLocalizedString::languages();
        const auto originalLocale = QLocale();
        KLocalizedString::setLanguages({ language });
        QLocale::setDefault(QLocale(language));
        QApplication::setLayoutDirection(QLocale(language).textDirection());
        {
            KCMultiDialog dialog;
            const KPluginMetaData metadata(CONFIG_PLUGIN_PATH);
            QVERIFY(dialog.addModule(metadata));
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            auto* host = dialog.findChild<QQuickWidget*>();
            QVERIFY(host && host->rootObject());
            auto* module
                = qobject_cast<KQuickConfigModule*>(host->rootObject()->property("kcm").value<QObject*>());
            QVERIFY(module && module->mainUi());
            QCOMPARE(module->mainUi()->property("title").toString(),
                expected(language, "AmbientLed.DevicesPage", "Devices"));
            auto* controller = module->property("controller").value<QObject*>();
            QVERIFY(controller);
            bool added = false;
            QVERIFY(QMetaObject::invokeMethod(
                controller, "addPreset", Q_RETURN_ARG(bool, added), Q_ARG(bool, false)));
            QVERIFY(added);
            QCOMPARE(controller->property("preset").toMap().value("name").toString(),
                expected(language, "Ambient::SettingsController", "New preset"));
            const auto preview = qEnvironmentVariable("AMBIENT_QUICK_PREVIEW");
            if (!preview.isEmpty()) {
                dialog.resize(1000, 850);
                QTest::qWait(50);
                QVERIFY(dialog.grab().save(preview + "/translated-" + language + ".png"));
            }
            // Metadata is used for the outer System Settings navigation.
            QCOMPARE(metadata.name(), expected(language, "AmbientLed.DevicesPage", "Devices"));
        }
        KLocalizedString::setLanguages(originalLanguages);
        QLocale::setDefault(originalLocale);
        QApplication::setLayoutDirection(Qt::LeftToRight);
    }
};
int main(int argc, char** argv)
{
    QTemporaryDir dir;
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", dir.path().toUtf8());
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QApplication app(argc, argv);
    TranslationsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_translations.moc"
