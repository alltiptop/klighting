// SPDX-License-Identifier: GPL-3.0-or-later
#include "translations.h"
#include <KLocalizedString>
#include <QCoreApplication>
#include <QLocale>

namespace Ambient
{
Translations::Translations()
    : Translations(KLocalizedString::languages())
{
}
Translations::Translations(const QStringList& languages)
{
    for (const auto& language : languages) {
        const QLocale locale(language);
        // English is the source language; do not skip it for a lower preference.
        if (locale.language() == QLocale::English || language == QLatin1String("C"))
            break;
        if (locale.language() == QLocale::C)
            continue;
        if (load(locale, QStringLiteral("ambient-led"), QStringLiteral("_"),
                QStringLiteral(":/ambient-led/i18n"))) {
            QCoreApplication::installTranslator(this);
            break;
        }
    }
}
}
