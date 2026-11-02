// SPDX-License-Identifier: GPL-3.0-or-later
#include "asus_translations.h"
#include <QCoreApplication>
#include <QLocale>
namespace Ambient
{
AsusTranslations::AsusTranslations()
{
    // Qt honors LANGUAGE and the process locale without a KDE dependency.
    for (const auto& language : QLocale().uiLanguages()) {
        const QLocale locale(language);
        if (locale.language() == QLocale::English || locale.language() == QLocale::C)
            break;
        if (load(locale, QStringLiteral("asus-g615lw"), QStringLiteral("_"),
                QStringLiteral(":/ambient-led/drivers/asus-g615lw/i18n"))) {
            QCoreApplication::installTranslator(this);
            break;
        }
    }
}
}
