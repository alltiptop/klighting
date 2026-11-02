// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
#include <algorithm>

namespace Ambient
{
inline bool isAmbientEffectName(const QString& name)
{
    if (name == QLatin1String("ambientled"))
        return true;
    const auto prefix = QLatin1String("ambientled_dev");
    if (!name.startsWith(prefix) || name.size() == prefix.size())
        return false;
    return std::all_of(name.cbegin() + prefix.size(), name.cend(),
        [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); });
}
inline bool hasConflictingAmbientEffects(const QStringList& names)
{
    return std::count_if(names.cbegin(), names.cend(), isAmbientEffectName) > 1;
}
}
