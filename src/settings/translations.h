// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
#include <QTranslator>
namespace Ambient
{
// Owned by each KCM: destroying a module removes only its own translator.
class Translations final : public QTranslator
{
public:
    explicit Translations(const QStringList& languages);
    Translations();
};
}
